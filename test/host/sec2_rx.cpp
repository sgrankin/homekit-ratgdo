#include "sec2_rx.h"
#include <assert.h>
#include <deque>
#include <stdio.h>

struct Serial
{
    std::deque<int> bytes;
    bool lost = false;
    int read()
    {
        if (bytes.empty())
            return -1;
        int value = bytes.front();
        bytes.pop_front();
        return value;
    }
    bool overflow()
    {
        bool value = lost;
        lost = false;
        return value;
    }
};

int main()
{
    Serial serial;
    Sec2RxBuffer rx;
    // Two seconds of continuous 9600-baud 8N1 reception while the application
    // cannot consume bytes. Yield service still runs every five milliseconds.
    for (unsigned t = 0; t < 400; ++t)
    {
        for (unsigned j = 0; j < 5; ++j)
            serial.bytes.push_back((t * 5 + j) % 256);
        rx.pump(serial, t * 5);
    }
    assert(rx.overflows == 0 && rx.maxGapMs == 5);
    for (unsigned i = 0; i < 2000; ++i)
        assert(rx.read() == int(i % 256));
    assert(rx.read() == -1);
    // Longer stalls overflow bounded RAM, discard the broken byte stream,
    // and request resynchronization rather than joining pre/post-loss bytes.
    for (unsigned i = 0; i < 33; ++i)
    {
        for (unsigned j = 0; j < 64; ++j)
            serial.bytes.push_back(j);
        rx.pump(serial, 2000 + i * 5);
    }
    assert(rx.overflows == 1 && rx.takeLoss() && !rx.takeLoss());
    assert(rx.read() == -1);
    serial.bytes.push_back(42);
    rx.pump(serial, 2200);
    assert(rx.read() == 42);
    serial.bytes.push_back(1);
    serial.lost = true;
    rx.pump(serial, 2205);
    assert(rx.overflows == 2 && rx.takeLoss() && rx.read() == -1);

    Sec2StatusRefresh poll;
    poll.reset(0);
    poll.received(10);
    assert(!poll.due(300009, false));
    assert(poll.due(300010, false)); // works with no incoming traffic at all
    poll.queried(300010);
    assert(!poll.due(300011, false));
    assert(poll.due(600010, false)); // retry an unanswered query
    poll.received(600010);
    assert(!poll.due(605009, true));
    assert(poll.due(605010, true)); // missing movement completion
    poll.queried(605010);
    poll.lost();
    assert(!poll.due(610009, false));
    assert(poll.due(610010, false));
    poll.received(610010);
    assert(!poll.due(610010, false));
    poll.reset(UINT32_MAX - 1000);
    poll.received(UINT32_MAX - 1000);
    assert(!poll.due(3998, true));
    assert(poll.due(3999, true));
    poll.reset(0);
    poll.received(0);
    poll.lost();
    for (unsigned t = 5000; t <= 15000; t += 5000)
    {
        assert(poll.due(t, true));
        poll.queried(t);
    }
    assert(!poll.due(20000, true));
    assert(poll.due(315000, true)); // disconnected opener backs off
    puts("Sec+2.0: stalled consumer, overflow recovery, silent-bus polling, moving retry and "
         "rollover passed");
}
