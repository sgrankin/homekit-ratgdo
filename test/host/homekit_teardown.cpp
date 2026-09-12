// Exercise real teardown with a socket whose peer never acknowledges closure.
#include <cassert>
#include <cstdlib>

static unsigned released;
struct Socket
{
    bool pending = true;
    void stop() {} // Graceful close can leave TCP buffers pending.
    void abort()
    {
        pending = false;
        ++released;
    }
    ~Socket()
    {
        assert(!pending);
    }
};
struct client_context_t
{
    void *verify_context, *event_queue, *endpoint_params, *body;
    Socket *socket;
};
void pair_verify_context_free(void *p)
{
    free(p);
}
void homekit_event_queue_clear(void *) {}
void q_kill(void *) {}
void query_params_free(void *p)
{
    free(p);
}
#include "homekit_teardown.inc"

int main()
{
    for (unsigned i = 0; i < 1000; ++i)
    {
        auto *c = static_cast<client_context_t *>(calloc(1, sizeof(client_context_t)));
        c->socket = new Socket;
        client_context_free(c);
    }
    assert(released == 1000);
    client_context_free(static_cast<client_context_t *>(calloc(1, sizeof(client_context_t))));
}
