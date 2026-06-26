#pragma once

#include "tt_hal_device.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void* RadioHandle;

enum RadioState {
    RADIO_PENDING_ON,
    RADIO_ON,
    RADIO_ERROR,
    RADIO_PENDING_OFF,
    RADIO_OFF
};

enum Modulation {
    MODULATION_NONE,
    MODULATION_LORA,
    MODULATION_FSK,
    MODULATION_LRFHSS
};

enum RadioParameter {
    RADIO_POWER,
    RADIO_BOOSTEDGAIN,
    RADIO_FREQUENCY,
    RADIO_BANDWIDTH,
    RADIO_SPREADFACTOR,
    RADIO_CODINGRATE,
    RADIO_SYNCWORD,
    RADIO_PREAMBLES,
    RADIO_FREQDIV,
    RADIO_DATARATE,
    RADIO_ADDRWIDTH,
    RADIO_NARROWGRID
};

enum RadioParameterStatus {
    RADIO_PARAM_UNAVAILABLE,
    RADIO_PARAM_VALERROR,
    RADIO_PARAM_SUCCESS
};

enum RadioTxState {
    RADIO_TX_QUEUED,
    RADIO_TX_PENDING_TRANSMIT,
    RADIO_TX_TRANSMITTED,
    RADIO_TX_TIMEOUT,
    RADIO_TX_ERROR
};

typedef int32_t RadioRxSubscriptionId;
typedef int32_t RadioStateSubscriptionId;
typedef int32_t RadioTxId;

struct RadioRxPacket {
    const uint8_t *data;
    uint32_t size;
    float rssi;
    float snr;
};

struct RadioTxPacket {
    uint8_t *data;
    uint32_t size;
    uint32_t address;
};

typedef void (*RadioStateCallback)(DeviceId id, RadioState state, void* ctx);
typedef void (*RadioTxStateCallback)(RadioTxId id, RadioTxState state, void* ctx);
typedef void (*RadioOnReceiveCallback)(DeviceId id, const RadioRxPacket* packet, void* ctx);

/**
 * Allocate a radio driver object for the specified radioId.
 * @param[in] radioId the identifier of the radio device
 * @return the radio handle
 */
RadioHandle tt_hal_radio_alloc(DeviceId radioId);

/**
 * Free the memory for the radio driver object.
 * @param[in] handle the radio driver handle
 */
void tt_hal_radio_free(RadioHandle handle);

/**
 * Get the device identifier for the radio driver object.
 * @param[in] handle the radio driver handle
 * @return the device identifier
 */
DeviceId tt_hal_radio_get_device_id(RadioHandle handle);

/**
 * Get the name for the radio driver object.
 * @param[in] handle the radio driver handle
 * @return the name of the radio
 */
const char* tt_hal_radio_get_name(RadioHandle handle);

/**
 * Get the description for the radio driver object.
 * @param[in] handle the radio driver handle
 * @return the description for the radio
 */
const char* tt_hal_radio_get_desc(RadioHandle handle);


/**
 * Get the state for the radio driver object.
 * @param[in] handle the radio driver handle
 * @return the state of the radio
 */
RadioState tt_hal_radio_get_state(RadioHandle handle);

/**
 * Set the modulation for the radio driver object.
 * The radio must not be started and it must be either capable
 * of reception or transmission of passed modulation.
 * @param[in] handle the radio driver handle
 * @param[in] modulation the modulation type
 * @return true if the modulation could be set, false otherwise
 */
bool tt_hal_radio_set_modulation(RadioHandle handle, Modulation modulation);

/**
 * Get the modulation for the radio driver object.
 * @param[in] handle the radio driver handle
 * @return the modulation type
 */
Modulation tt_hal_radio_get_modulation(RadioHandle handle);

/**
 * Try to set a parameter for the radio driver object.
 * The radio must not be started and it must be in the modulation mode
 * for the respective parameter.
 * @param[in] handle the radio driver handle
 * @param[in] parameter the parameter
 * @param[in] value the value to set
 * @return status of the parameter set operation
 */
RadioParameterStatus tt_hal_radio_set_parameter(RadioHandle handle, RadioParameter parameter, float value);

/**
 * Try to get a parameter for the radio driver object.
 * The radio must be in the modulation mode for the respective parameter,
 * else the parameter is unavailable.
 * @param[in] handle the radio driver handle
 * @param[in] parameter the parameter
 * @param[out] value retrieved value will be stored at this pointer
 * @return status of the parameter get operation
 */
RadioParameterStatus tt_hal_radio_get_parameter(RadioHandle handle, RadioParameter parameter, float *value);

/**
 * Get the unit string of a parameter.
 * If the parameter isn't available, the returned string is empty.
 * If the unit string does not fit into the provided character array,
 * it is truncated but always null terminated.
 * @param[in] handle the radio driver handle
 * @param[in] parameter the parameter
 * @param[out] str character array to store the unit string into
 * @param[in] maxSize the maximum size the array str can hold
 */
void tt_hal_radio_get_parameter_unit_str(RadioHandle handle, RadioParameter parameter, char str[], unsigned maxSize);

/**
 * Check whenever the radio driver object can transmit a certain modulation.
 * @param[in] handle the radio driver handle
 * @param[in] modulation the modulation type
 * @return true if capable, false otherwise
 */
bool tt_hal_radio_can_transmit(RadioHandle handle, Modulation modulation);

/**
 * Check whenever the radio driver object can receive a certain modulation.
 * @param[in] handle the radio driver handle
 * @param[in] modulation the modulation type
 * @return true if capable, false otherwise
 */
bool tt_hal_radio_can_receive(RadioHandle handle, Modulation modulation);

/**
 * Starts the radio driver object.
 * @param[in] handle the radio driver handle
 * @return true if the radio could be started, false otherwise
 */
bool tt_hal_radio_start(RadioHandle handle);

/**
 * Stops the radio driver object.
 * @param[in] handle the radio driver handle
 * @return true if the radio could be stopped, false otherwise
 */
bool tt_hal_radio_stop(RadioHandle handle);

/**
 * Put a packet in the transmission queue of the radio driver object.
 * @param[in] handle the radio driver handle
 * @param[in] packet packet to send (no special requirement for memory of data)
 * @param[in] callback function to call on transmission state change for the packet
 * @param[in] ctx context which will be passed to the callback function
 * @return the identifier for the transmission
 */
RadioTxId tt_hal_radio_transmit(RadioHandle handle, RadioTxPacket packet, RadioTxStateCallback callback, void* ctx);

/**
 * Subscribe for any received packet that the radio driver object receives.
 * @param[in] handle the radio driver handle
 * @param[in] callback function to call on reception of a packet
 * @param[in] ctx context which will be passed to the callback function
 * @return the identifier for the subscription
 */
RadioRxSubscriptionId tt_hal_radio_subscribe_receive(RadioHandle handle, RadioOnReceiveCallback callback, void* ctx);

/**
 * Subscribe for any state change of the radio driver object.
 * @param[in] handle the radio driver handle
 * @param[in] callback function to call when the state of the radio changes
 * @param[in] ctx context which will be passed to the callback function
 * @return the identifier for the subscription
 */
RadioStateSubscriptionId tt_hal_radio_subscribe_state(RadioHandle handle, RadioStateCallback callback, void* ctx);

/**
 * Unsubscribe for any received packet that the radio driver object receives.
 * @param[in] handle the radio driver handle
 * @param[in] id the identifier for the subscription
 */
void tt_hal_radio_unsubscribe_receive(RadioHandle handle, RadioRxSubscriptionId id);

/**
 * Unsubscribe for any state change of the radio driver object.
 * @param[in] handle the radio driver handle
 * @param[in] id the identifier for the subscription
 */
void tt_hal_radio_unsubscribe_state(RadioHandle handle, RadioStateSubscriptionId id);

#ifdef __cplusplus
}
#endif
