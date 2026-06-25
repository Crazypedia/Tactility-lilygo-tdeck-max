#pragma once

#include <Tactility/hal/radio/RadioDevice.h>
#include <Tactility/Thread.h>

class RadiolibThreadedDevice : public tt::hal::radio::RadioDevice {

private:
    std::string threadName;
    size_t threadSize;
    std::unique_ptr<tt::Thread> _Nullable thread;
    bool threadInterrupted = false;

protected:
    virtual int32_t threadMain();
    bool isThreadInterrupted() const;

    virtual void interruptSignal() = 0;

    virtual int doBegin(const Modulation modulation) = 0;
    virtual void doEnd() = 0;
    virtual void doTransmit() = 0;
    virtual bool doListen() = 0;
    virtual void doReceive() = 0;

public:
    explicit RadiolibThreadedDevice(const std::string& threadName, const size_t threadSize)
    : threadName(threadName)
    , threadSize(threadSize)
    {}

    ~RadiolibThreadedDevice() override = default;

    virtual bool start() override;
    virtual bool stop() override;
};
