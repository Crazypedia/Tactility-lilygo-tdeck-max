#include "RadiolibThreadedDevice.h"

#include <tactility/log.h>
#include <cstring>

constexpr const char* TAG = "RadiolibThreadedDevice";

bool RadiolibThreadedDevice::start() {
    auto lock = getMutex().asScopedLock();
    lock.lock();

    if ((thread != nullptr) && (thread->getState() != tt::Thread::State::Stopped)) {
        LOG_W(TAG, "Already started");
        return true;
    }

    threadInterrupted = false;

    LOG_I(TAG, "Starting thread");
    setState(State::PendingOn);

    thread = std::make_unique<tt::Thread>(
        threadName,
        threadSize,
        [this]() {
            return this->threadMain();
        }
    );
    thread->setPriority(tt::Thread::Priority::High);
    thread->start();

    LOG_I(TAG, "Starting finished");
    return true;
}

bool RadiolibThreadedDevice::stop() {
    auto lock = getMutex().asScopedLock();
    lock.lock();

    setState(State::PendingOff);

    if (thread != nullptr) {
        threadInterrupted = true;
        interruptSignal();

        // Detach thread, it will auto-delete when leaving the current scope
        auto old_thread = std::move(thread);

        if (old_thread->getState() != tt::Thread::State::Stopped) {
            // Unlock so thread can lock
            lock.unlock();
            // Wait for thread to finish
            old_thread->join();
            // Re-lock to continue logic below
            lock.lock();
        }
    }

    setState(State::Off);

    return true;
}

bool RadiolibThreadedDevice::isThreadInterrupted() const {
    auto lock = getMutex().asScopedLock();
    lock.lock();
    return threadInterrupted;
}

int32_t RadiolibThreadedDevice::threadMain() {

    int rc = doBegin(getModulation());
    bool hasRx = false;
    if (rc != 0) {
        return rc;
    }
    setState(State::On);

    while (!isThreadInterrupted()) {
        hasRx = doListen();

        // Thread might've been interrupted in the meanwhile
        if (isThreadInterrupted()) {
            break;
        }

        if (getTxQueueSize() > 0) {
            doTransmit();
        } else {
            if (hasRx) {
                doReceive();
            }
        }
    }

    doEnd();
    return 0;
}
