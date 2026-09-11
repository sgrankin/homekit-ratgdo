// Compile the production write path; inject only socket, clock and heap APIs.
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdarg>
#include <string>
#include <vector>

using byte = unsigned char;
static uint32_t nowMs;
uint32_t millis()
{
    return nowMs;
}
static std::vector<std::string> logs;
static bool stoppedAtLog;
struct Socket
{
    bool stopped = false;
    bool connected_ = true;
    bool closeDuringWrite = false;
    unsigned calls = 0;
    size_t result = 17;
    uint32_t duration = 501;
    uint8_t status()
    {
        return connected_ ? 4 : 0;
    }
    bool connected()
    {
        return connected_;
    }
    int availableForWrite()
    {
        return calls ? 0 : 17;
    }
    size_t write(byte *, int)
    {
        ++calls;
        nowMs += duration;
        if (closeDuringWrite)
            connected_ = false;
        return result;
    }
    void stop()
    {
        stopped = true;
        connected_ = false;
    }
};
struct client_context_t
{
    explicit client_context_t(Socket *value) : socket(value) {}
    Socket *socket;
    bool disconnect = false;
    bool error_write = false;
    bool sending_events = false;
    unsigned endpoint = 4;
    unsigned step = 5;
    bool encrypted = true;
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
void logError(client_context_t *context, const char *format, ...)
{
    stoppedAtLog = context->socket && context->socket->stopped;
    char buffer[512];
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    logs.emplace_back(buffer);
}
#define CLIENT_ERROR(context, ...) logError(context, __VA_ARGS__)
#define CLIENT_VERBOSE(...) ((void)0)
#include "homekit_write.inc"

int main()
{
    byte payload[530]{};
    Socket socket;
    client_context_t context{&socket};
    nowMs = UINT32_MAX - 100;
    write(&context, payload, sizeof(payload));
    assert(socket.stopped && context.error_write && !stoppedAtLog);
    assert(logs.size() == 1);
    assert(logs[0] == "HKTX short phase=response ep=4 step=5 enc=1 want=530 sent=17 ms=501 "
                      "tcp=4->4 conn=1 snd=17->0 heap=20000 max=12000 frag=40");

    logs.clear();
    socket = Socket();
    context = client_context_t{&socket};
    context.sending_events = true;
    socket.closeDuringWrite = true;
    socket.result = 0;
    write(&context, payload, sizeof(payload));
    assert(logs.size() == 1 && !stoppedAtLog);
    assert(logs[0].find("phase=event") != std::string::npos);
    assert(logs[0].find("sent=0") != std::string::npos);
    assert(logs[0].find("tcp=4->0 conn=0") != std::string::npos);
    assert(context.error_write && socket.stopped);

    logs.clear();
    socket = Socket();
    socket.result = sizeof(payload);
    context = client_context_t{&socket};
    ESP.calls = 0;
    write(&context, payload, sizeof(payload));
    assert(logs.empty() && ESP.calls == 0);
    assert(!context.error_write && !socket.stopped && socket.calls == 1);

    context.error_write = true;
    write(&context, payload, sizeof(payload));
    assert(socket.calls == 1); // no new write after a failed frame
    write(nullptr, payload, sizeof(payload));
    context.socket = nullptr;
    write(&context, payload, sizeof(payload));
    puts("HomeKit write diagnostics: short/zero writes, pre-close state, event phase, rollover, "
         "quiet success: passed");
}
