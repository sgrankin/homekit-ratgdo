#pragma once

#include <cstdint>

// Independent of Arduino so the same lifecycle can be exercised on a host.
// Services stay stopped after a successful upload until the caller reboots.
// A failed upload schedules recovery only if it actually stopped services.
class OtaSession
{
public:
    static constexpr uint32_t recoveryDelayMs = 1500;
    static constexpr uint32_t uploadIdleMs = 30000;
    void receiving(uint32_t now) { receiving_ = true; lastProgress_ = now; }
    void endReceiving() { receiving_ = false; }
    bool uploadTimedOut(uint32_t now) const
    {
        return receiving_ && uint32_t(now - lastProgress_) >= uploadIdleMs;
    }

    bool servicesRunning() const { return !stopped_; }
    bool complete() const { return complete_; }
    bool recovering() const { return recovering_; }

    bool stopServices()
    {
        if (stopped_)
            return false;
        stopped_ = true;
        return true;
    }

    void finish() { complete_ = true; receiving_ = false; }

    void fail(uint32_t now)
    {
        complete_ = false;
        if (stopped_ && !recovering_)
        {
            recovering_ = true;
            failedAt_ = now;
        }
    }

    bool rebootDue(uint32_t now) const
    {
        return recovering_ && uint32_t(now - failedAt_) >= recoveryDelayMs;
    }

private:
    bool receiving_ = false;
    uint32_t lastProgress_ = 0;
    bool stopped_ = false;
    bool complete_ = false;
    bool recovering_ = false;
    uint32_t failedAt_ = 0;
};
