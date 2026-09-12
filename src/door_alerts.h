#pragma once

#include <cstdint>

inline bool parseLeftOpenMinutes(const char *text, int &minutes)
{
    if (!text || !*text)
        return false;
    unsigned value = 0;
    for (const char *p = text; *p; ++p)
    {
        if (*p < '0' || *p > '9')
            return false;
        value = value * 10 + unsigned(*p - '0');
        if (value > 1440)
            return false;
    }
    minutes = static_cast<int>(value);
    return true;
}

// Observation only: this state machine never issues an opener command.
class DoorAlerts
{
  public:
    enum State : uint8_t
    {
        Open = 0,
        Closed = 1,
        Opening = 2,
        Closing = 3,
        Stopped = 4,
        Unknown = 255
    };
    static constexpr uint32_t closeTimeoutMs = 60000;

    void update(uint8_t state, uint32_t now, uint32_t leftOpenMinutes)
    {
        if (away_)
        {
            const uint32_t delta = uint32_t(now - lastUpdate_);
            const uint32_t remaining = 1440 * 60000 - awayElapsed_;
            awayElapsed_ += delta < remaining ? delta : remaining;
        }
        lastUpdate_ = now;
        if (state == Closed)
        {
            away_ = closing_ = leftOpen_ = closeFailed_ = false;
            awayElapsed_ = 0;
            return;
        }
        // Unknown at startup is not evidence of an open door. After a known
        // opening it must not erase the timer or clear a latched failure.
        if (state <= Stopped && !away_)
        {
            away_ = true;
            awayElapsed_ = 0;
        }
        if (state == Closing && !closing_ && !closeFailed_)
        {
            closing_ = true;
            closingSince_ = now;
        }
        if (closing_ && ((state <= Stopped && state != Closing) ||
                         uint32_t(now - closingSince_) >= closeTimeoutMs))
        {
            closeFailed_ = true;
            closing_ = false;
        }
        const uint32_t minutes = leftOpenMinutes > 1440 ? 1440 : leftOpenMinutes;
        leftOpen_ = away_ && minutes != 0 && awayElapsed_ >= minutes * 60000;
    }

    void closeDidNotStart()
    {
        closeFailed_ = true;
        closing_ = false;
    }

    bool leftOpen() const
    {
        return leftOpen_;
    }
    bool closeFailed() const
    {
        return closeFailed_;
    }

  private:
    bool away_ = false, closing_ = false, leftOpen_ = false, closeFailed_ = false;
    uint32_t lastUpdate_ = 0, awayElapsed_ = 0, closingSince_ = 0;
};
