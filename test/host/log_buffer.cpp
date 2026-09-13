#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
using std::min;
#define LOG_BUFFER_SIZE (1024 * 6)
// Struct and append block extracted verbatim from production sources.
#include "log_buffer.inc"

std::string contents(const logBuffer &log)
{
    if (!log.wrapped)
        return std::string(log.buffer);
    const size_t start = log.head + 1;
    return std::string(log.buffer + start, sizeof(log.buffer) - start) + log.buffer;
}

int main()
{
    std::unique_ptr<logBuffer> log(new logBuffer{});
    const size_t capacity = sizeof(log->buffer);
    std::string history;
    auto add = [&](const std::string &line)
    {
        append(log.get(), line.c_str());
        history += line;
        if (history.size() >= capacity)
            history.erase(0, history.size() - capacity + 1);
        assert(log->head < capacity);
        assert(log->buffer[log->head] == 0);
        assert(contents(*log) == history);
    };
    // Hit the exact end (formerly an out-of-bounds NUL write), then continue.
    for (size_t i = 0; i < capacity / 2; ++i)
        add("x\n");
    assert(log->wrapped && log->head == 0);
    add("");
    add("after exact fit\n");
    // Exercise short lines, maximum formatted lines and repeated wraparound.
    for (unsigned i = 0; i < 512; ++i)
        add(std::string(1 + (i * 37) % 254, char('a' + i % 26)) + "\n");
    puts("Log buffer: exact fill, retained history, empty append and repeated wrap passed");
}
