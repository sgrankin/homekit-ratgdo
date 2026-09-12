// Compile production send/encryption paths; fake only transport, clock, crypto and heap APIs.
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdarg>
#include <cstring>
#include <string>
#include <vector>

using byte = unsigned char;
constexpr uint8_t ESTABLISHED = 4;
static uint32_t nowMs;
uint32_t millis()
{
    return nowMs;
}
void delay(uint32_t ms)
{
    nowMs += ms;
}
void esp_yield()
{
    ++nowMs;
}
void system_soft_wdt_feed() {}
static std::vector<std::string> logs;
static bool abortedAtLog;
struct WriteStep
{
    size_t accepted;
    uint32_t duration;
    uint8_t state;
};
struct Borrowed
{
    const byte *data;
    size_t size;
};
struct Socket
{
    bool aborted = false;
    uint8_t state = ESTABLISHED;
    unsigned calls = 0, flushCalls = 0;
    unsigned timeout = 500;
    unsigned delayedFlushes = 0;
    bool neverAck = false, closeOnFlush = false;
    std::vector<WriteStep> steps;
    std::vector<Borrowed> pending;
    std::vector<byte> received;
    uint8_t status()
    {
        return state;
    }
    // Model unread RX bytes: connected() can remain true in CLOSE_WAIT.
    bool connected()
    {
        return state != 0;
    }
    unsigned getTimeout()
    {
        return timeout;
    }
    int availableForWrite()
    {
        return pending.empty() ? 1072 : 0;
    }
    size_t write(const byte *data, size_t size)
    {
        assert(!aborted);
        WriteStep step = calls < steps.size() ? steps[calls] : WriteStep{size, 1, ESTABLISHED};
        ++calls;
        nowMs += step.duration;
        state = step.state;
        const size_t accepted = std::min(size, step.accepted);
        // Sync mode borrows the source; defer reading it until acknowledgement.
        if (accepted)
            pending.push_back({data, accepted});
        return accepted;
    }
    bool flush(unsigned wait)
    {
        assert(wait > 0 && wait <= 50);
        ++flushCalls;
        if (closeOnFlush)
        {
            state = 7;
            return true;
        }
        if (neverAck || delayedFlushes)
        {
            if (delayedFlushes)
                --delayedFlushes;
            nowMs += wait;
            return false;
        }
        for (const auto &part : pending)
            received.insert(received.end(), part.data, part.data + part.size);
        pending.clear();
        return true;
    }
    void abort()
    {
        aborted = true;
        state = 0;
        pending.clear();
    }
};
struct client_context_t
{
    explicit client_context_t(Socket *value) : socket(value) {}
    Socket *socket;
    bool disconnect = false, error_write = false, sending_events = false;
    bool reported_write_recovery = false;
    unsigned endpoint = 4, step = 5;
    bool encrypted = true;
    byte read_key[32]{};
    int count_reads = 0;
};
struct
{
    unsigned calls = 0;
    unsigned getFreeHeap()
    {
        ++calls;
        return 20000;
    }
    unsigned getMaxFreeBlockSize()
    {
        ++calls;
        return 12000;
    }
    unsigned getHeapFragmentation()
    {
        ++calls;
        return 40;
    }
} ESP;
void logMessage(client_context_t *context, const char *format, ...)
{
    abortedAtLog = context->socket && context->socket->aborted;
    char buffer[512];
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    logs.emplace_back(buffer);
}
#define CLIENT_ERROR(context, ...) logMessage(context, __VA_ARGS__)
#define CLIENT_INFO(context, ...) logMessage(context, __VA_ARGS__)
#define CLIENT_VERBOSE(...) ((void)0)
#define ERROR(...) ((void)0)
static byte encryptedBuffer[1042];
static unsigned encryptions;
int crypto_chacha20poly1305_encrypt(const byte *, const byte *nonce, const byte *, size_t,
                                    const byte *payload, size_t size, byte *out, size_t *available)
{
    assert(*available >= size + 16);
    ++encryptions;
    for (size_t i = 0; i < size; ++i)
        out[i] = payload[i] ^ byte(0xa5 + nonce[4]);
    memset(out + size, nonce[4], 16);
    *available = size + 16;
    return 0;
}
#include "homekit_write.inc"

int main()
{
    std::vector<byte> payload(536);
    for (size_t i = 0; i < payload.size(); ++i)
        payload[i] = byte(i);

    // Ordinary success drains borrowed storage and does not log or inspect heap.
    Socket socket;
    client_context_t context(&socket);
    write(&context, payload.data(), payload.size());
    assert(socket.received == payload && socket.pending.empty());
    assert(logs.empty() && ESP.calls == 0 && !context.error_write);

    // Reproduce observed 421/536-byte short write with TCP still established.
    logs.clear();
    socket = Socket();
    context = client_context_t(&socket);
    socket.steps = {{421, 1121, ESTABLISHED}};
    nowMs = UINT32_MAX - 100; // budget must survive millis rollover
    write(&context, payload.data(), payload.size());
    assert(socket.calls == 2 && socket.received == payload);
    assert(!socket.aborted && !context.error_write && socket.pending.empty());
    assert(logs.size() == 1 && logs[0].find("HKTX recovered") == 0);
    assert(logs[0].find("tries=2") != std::string::npos);
    // Another recovery on this connection adds no extra recovery log.
    socket.steps.push_back({536, 1, ESTABLISHED});
    socket.steps.push_back({17, 1, ESTABLISHED});
    write(&context, payload.data(), payload.size());
    assert(logs.size() == 1);

    // Zero progress followed by partial progress: preserve bytes and order exactly.
    logs.clear();
    socket = Socket();
    context = client_context_t(&socket);
    socket.steps = {{0, 100, ESTABLISHED}, {17, 100, ESTABLISHED}, {421, 100, ESTABLISHED}};
    write(&context, payload.data(), payload.size());
    assert(socket.received == payload && socket.calls == 4 && !context.error_write);

    // Fast zero returns are capped even if the lower layer consumes no time.
    logs.clear();
    socket = Socket();
    context = client_context_t(&socket);
    context.sending_events = true;
    socket.steps.assign(20, {0, 0, ESTABLISHED});
    write(&context, payload.data(), payload.size());
    assert(socket.calls == 8 && socket.aborted && context.error_write);
    assert(logs.size() == 1 && !abortedAtLog);
    assert(logs[0].find("phase=event") != std::string::npos);
    assert(logs[0].find("tcp=4->4 conn=1") != std::string::npos);
    write(&context, payload.data(), payload.size());
    assert(socket.calls == 8 && logs.size() == 1); // suppress failed-session cascades

    // Time budget expires before the attempt cap for stalled writes.
    logs.clear();
    socket = Socket();
    context = client_context_t(&socket);
    socket.steps.assign(20, {0, 500, ESTABLISHED});
    const uint32_t start = nowMs;
    write(&context, payload.data(), payload.size());
    assert(socket.calls == 4 && socket.aborted && uint32_t(nowMs - start) >= 2000);

    // Initial pairing retains its existing longer socket timeout allowance.
    logs.clear();
    socket = Socket();
    context = client_context_t(&socket);
    socket.timeout = 90000;
    socket.steps = {{17, 3000, ESTABLISHED}};
    write(&context, payload.data(), payload.size());
    assert(socket.received == payload && !context.error_write);

    // A peer close after accepting a prefix must abort borrowed data immediately.
    logs.clear();
    socket = Socket();
    context = client_context_t(&socket);
    socket.steps = {{17, 10, 7}};
    write(&context, payload.data(), payload.size());
    assert(socket.calls == 1 && socket.flushCalls == 0 && socket.aborted);
    assert(socket.pending.empty() && logs[0].find("tcp=4->7 conn=1") != std::string::npos);

    // A full write count is insufficient: retain the buffer until ACKs arrive.
    logs.clear();
    socket = Socket();
    context = client_context_t(&socket);
    socket.delayedFlushes = 3;
    write(&context, payload.data(), payload.size());
    assert(socket.flushCalls == 4 && socket.received == payload && !socket.aborted);

    logs.clear();
    socket = Socket();
    context = client_context_t(&socket);
    socket.neverAck = true;
    write(&context, payload.data(), payload.size());
    assert(socket.calls == 1 && socket.aborted && socket.pending.empty());
    assert(socket.received.empty() && context.error_write && logs.size() == 1);
    assert(logs[0].find("sent=536") != std::string::npos);
    assert(logs[0].find("ack=0") != std::string::npos);

    // Core flush() also returns true when TCP leaves ESTABLISHED; that is not an ACK.
    logs.clear();
    socket = Socket();
    context = client_context_t(&socket);
    socket.closeOnFlush = true;
    write(&context, payload.data(), payload.size());
    assert(socket.aborted && socket.pending.empty() && context.error_write);

    // Exercise actual encrypted sender twice: retries must not re-encrypt/advance nonce.
    logs.clear();
    socket = Socket();
    context = client_context_t(&socket);
    socket.steps = {{17, 100, ESTABLISHED}};
    encryptions = 0;
    assert(client_send_encrypted_(&context, payload.data(), payload.size()) == 0);
    assert(client_send_encrypted_(&context, payload.data(), payload.size()) == 0);
    assert(encryptions == 2 && context.count_reads == 2 && socket.pending.empty());
    std::vector<byte> expected;
    for (unsigned nonce = 0; nonce < 2; ++nonce)
    {
        expected.push_back(byte(payload.size()));
        expected.push_back(byte(payload.size() >> 8));
        for (byte b : payload)
            expected.push_back(b ^ byte(0xa5 + nonce));
        expected.insert(expected.end(), 16, byte(nonce));
    }
    assert(socket.received == expected);
    // A failed encrypted frame stops serialization before another nonce/buffer reuse.
    logs.clear();
    socket = Socket();
    context = client_context_t(&socket);
    socket.neverAck = true;
    encryptions = 0;
    std::vector<byte> largePayload(2000, 0x6b);
    assert(client_send_encrypted_(&context, largePayload.data(), largePayload.size()) == -1);
    assert(encryptions == 1 && context.count_reads == 1);
    assert(socket.aborted && socket.pending.empty());

    write(nullptr, payload.data(), payload.size());
    context.socket = nullptr;
    write(&context, payload.data(), payload.size());
    puts("HomeKit transport: byte-exact retries, bounded stalls, ACK lifetime, peer close, "
         "encrypted frame continuity: passed");
}
