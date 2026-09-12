#pragma once

#include <stddef.h>
#include <stdint.h>

// Single cooperative execution context: neither pump nor read may yield.
// ISR edges remain owned by SoftwareSerial. Only complete bytes enter here.
class Sec2RxBuffer
{
  public:
    static constexpr size_t capacity = 2048;
    uint32_t overflows = 0;
    uint32_t maxGapMs = 0;

    template <class Serial> void pump(Serial &serial, uint32_t now)
    {
        if (started && uint32_t(now - lastPump) > maxGapMs)
            maxGapMs = now - lastPump;
        started = true;
        lastPump = now;
        bool dropped = false;
        for (unsigned i = 0; i < 64; ++i)
        {
            const int value = serial.read();
            if (value < 0)
                break;
            if (count == capacity)
                dropped = true;
            else
            {
                data[tail] = static_cast<uint8_t>(value);
                tail = (tail + 1) % capacity;
                ++count;
            }
        }
        // read() also transfers the ISR overflow flag into overflow().
        if (serial.overflow() || dropped)
        {
            ++overflows;
            clear();
            loss = true;
        }
    }

    int read()
    {
        if (!count)
            return -1;
        const int value = data[head];
        head = (head + 1) % capacity;
        --count;
        return value;
    }

    void clear()
    {
        head = tail = count = 0;
    }

    bool takeLoss()
    {
        const bool result = loss;
        loss = false;
        return result;
    }

  private:
    uint8_t data[capacity] = {};
    size_t head = 0, tail = 0, count = 0;
    uint32_t lastPump = 0;
    bool started = false, loss = false;
};

class Sec2StatusRefresh
{
  public:
    bool hasStatus = false;

    void reset(uint32_t now)
    {
        lastStatus = lastQuery = now;
        hasStatus = recovery = false;
        unanswered = 0;
    }

    void received(uint32_t now)
    {
        lastStatus = now;
        hasStatus = true;
        recovery = false;
        unanswered = 0;
    }

    void lost()
    {
        recovery = true;
    }
    void queried(uint32_t now)
    {
        lastQuery = now;
        if (unanswered < 3)
            ++unanswered;
    }
    uint32_t age(uint32_t now) const
    {
        return now - lastStatus;
    }

    bool due(uint32_t now, bool moving) const
    {
        const uint32_t interval = ((moving || recovery) && unanswered < 3) ? 5000 : 300000;
        return uint32_t(now - lastQuery) >= interval && (recovery || age(now) >= interval);
    }

  private:
    uint32_t lastStatus = 0, lastQuery = 0;
    bool recovery = false;
    unsigned unanswered = 0;
};
