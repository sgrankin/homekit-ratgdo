#include <cassert>
#include <cstdint>
#include <cstdio>
#include <vector>

constexpr unsigned SSE_MAX_CHANNELS = 8;
struct SSESubscription
{
    bool clientIP = false;
    bool SSEconnected = false;
    uint32_t heartbeatInterval = 0;
    uint32_t lastHeartbeatMs = 0;
};
SSESubscription subscription[SSE_MAX_CHANNELS];
struct
{
    bool running = true;
    bool servicesRunning() { return running; }
} otaSession;
uint32_t now = 0;
uint32_t millis() { return now; }
std::vector<unsigned> sent;
bool disconnectOnSend = false;
void SSEheartbeat(SSESubscription *s)
{
    sent.push_back(s - subscription);
    if (disconnectOnSend)
        s->SSEconnected = false;
}

#include "sse_heartbeat.inc"

int main()
{
    for (auto &s : subscription)
    {
        s.clientIP = true;
        s.SSEconnected = true;
        s.heartbeatInterval = 1;
    }
    now = 999;
    serviceSSEheartbeats();
    assert(sent.empty());
    now = 1000;
    for (unsigned i = 0; i < SSE_MAX_CHANNELS; ++i)
    {
        serviceSSEheartbeats();
        assert(sent.size() == i + 1 && sent.back() == i);
    }
    serviceSSEheartbeats();
    assert(sent.size() == SSE_MAX_CHANNELS);

    // A long pause produces one update per active stream, never a catch-up burst.
    now = 60000;
    otaSession.running = false;
    serviceSSEheartbeats();
    assert(sent.size() == SSE_MAX_CHANNELS);
    otaSession.running = true;
    subscription[0].heartbeatInterval = 0;
    subscription[1].SSEconnected = false;
    subscription[2].clientIP = false;
    sent.clear();
    for (unsigned i = 3; i < SSE_MAX_CHANNELS; ++i)
    {
        serviceSSEheartbeats();
        assert(sent.size() == i - 2 && sent.back() == i);
    }
    serviceSSEheartbeats();
    assert(sent.size() == 5);

    // Millisecond wrap and a socket removed by its heartbeat handler.
    for (auto &s : subscription)
        s.SSEconnected = false;
    auto &s = subscription[3];
    s.SSEconnected = true;
    s.lastHeartbeatMs = UINT32_MAX - 499;
    now = 499;
    sent.clear();
    serviceSSEheartbeats();
    assert(sent.empty());
    now = 500;
    disconnectOnSend = true;
    serviceSSEheartbeats();
    assert(sent.size() == 1 && sent.back() == 3);
    now += 1000;
    serviceSSEheartbeats();
    assert(sent.size() == 1);
    puts("SSE: loop pacing, fairness, disabled/disconnected streams, OTA pause and rollover passed");
}
