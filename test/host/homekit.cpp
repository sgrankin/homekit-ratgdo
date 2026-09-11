#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <set>
#include <vector>
#include <utility>
#include "allocator.h"
#include "homekit_event_queue.h"
#undef malloc
#undef calloc
#undef free
#undef strdup

// These track every allocation in the real C queue and HomeKit value helpers.
static std::set<void *> allocations;
static int failAfter = -1;
extern "C" void *test_malloc(size_t n)
{
    if (failAfter == 0)
        return nullptr;
    if (failAfter > 0)
        --failAfter;
    void *p = std::malloc(n);
    if (p)
        allocations.insert(p);
    return p;
}
extern "C" void *test_calloc(size_t n, size_t size)
{
    auto p = test_malloc(n * size);
    if (p)
        std::memset(p, 0, n * size);
    return p;
}
extern "C" void test_free(void *p)
{
    if (p)
    {
        assert(allocations.erase(p) == 1);
        std::free(p);
    }
}
extern "C" char *test_strdup(const char *s)
{
    auto p = static_cast<char *>(test_malloc(strlen(s) + 1));
    if (p)
        strcpy(p, s);
    return p;
}
// The inline queue helper also uses the tracked allocator.
// Included by compiler in a wrapper after standard headers (see run.py).
#define CLIENT_DEBUG(...) ((void)0)
#define CLIENT_ERROR(...) ((void)0)
#define ERROR(...) ((void)0)
#define HOMEKIT_CLIENT_STEP_PAIR_VERIFY_2OF2 5
struct Socket
{
    bool stopped = false;
    void stop()
    {
        stopped = true;
    }
};
struct client_context_t
{
    bool disconnect = false;
    bool sending_events = false;
    Queue_t *event_queue = nullptr;
    Socket *socket = nullptr;
    int step = HOMEKIT_CLIENT_STEP_PAIR_VERIFY_2OF2;
    homekit_characteristic_t *current_characteristic = nullptr;
    homekit_value_t *current_value = nullptr;
    client_context_t *next = nullptr;
};
struct homekit_server_t
{
    client_context_t *clients = nullptr;
};
// Exact client_event_t definition is extracted from the patched library header.
#include "homekit_client_event.inc"
static std::vector<std::pair<const homekit_characteristic_t *, int>> sent;
void send_client_events(client_context_t *context, client_event_t *head)
{
    assert(context->sending_events);
    for (auto p = head; p; p = p->next)
        sent.push_back({p->characteristic, p->value.int_value});
}
static homekit_server_t *activeServer;
static int processedClients;
void homekit_server_accept_client(homekit_server_t *) {}
void homekit_client_process(client_context_t *client)
{
    ++processedClients;
    activeServer->clients = client->next;
    test_free(client); // test traversal when processing destroys its current node
}
#define malloc test_malloc
#define calloc test_calloc
#define free test_free
#define strdup test_strdup
#include "homekit_handlers.inc"

int main()
{
    homekit_characteristic_t characteristics[5]{};
    homekit_value_t value{};
    value.format = homekit_format_int;
    for (int iteration = 0; iteration < 1000; iteration++)
    {
        Queue_t queue{};
        assert(q_init(&queue, sizeof(characteristic_event_t *), 4, FIFO, false));
        Socket socket;
        client_context_t client;
        client.socket = &socket;
        client.event_queue = &queue;
        homekit_server_t server;
        server.clients = &client;
        for (int i = 0; i < 100; i++)
        {
            value.int_value = i;
            client_notify_characteristic(&characteristics[0], value, &client);
        }
        assert(q_getCount(&queue) == 1); // bursts coalesce before allocating more events
        assert(allocations.size() == 2); // queue backing store plus one owned event
        sent.clear();
        homekit_server_process_notifications(&server);
        assert(!client.sending_events);
        assert(sent.size() == 1 && sent[0].second == 99); // newest, not oldest
        assert(allocations.size() == 1);
        // Distinct characteristic overflow requests reconnect instead of leaking/dropping silently.
        for (auto &ch : characteristics)
            client_notify_characteristic(&ch, value, &client);
        assert(q_getCount(&queue) == 4 && socket.stopped && client.disconnect);
        homekit_event_queue_clear(&queue);
        q_kill(&queue);
        assert(allocations.empty());
    }
    for (int failPoint : {0, 1, 2})
    {
        Queue_t queue{};
        q_init(&queue, sizeof(characteristic_event_t *), 4, FIFO, false);
        Socket socket;
        client_context_t client;
        client.socket = &socket;
        client.event_queue = &queue;
        homekit_server_t server;
        server.clients = &client;
        for (int i = 0; i < 3; i++)
            client_notify_characteristic(&characteristics[i], value, &client);
        failAfter = failPoint;
        sent.clear();
        homekit_server_process_notifications(&server);
        failAfter = -1;
        assert(client.disconnect && socket.stopped && sent.empty());
        assert(q_getCount(&queue) == 0 && allocations.size() == 1);
        q_kill(&queue);
        assert(allocations.empty());
    }
    Queue_t queue{};
    q_init(&queue, sizeof(characteristic_event_t *), 4, FIFO, false);
    Socket socket;
    client_context_t client;
    client.socket = &socket;
    client.event_queue = &queue;
    failAfter = 0;
    client_notify_characteristic(&characteristics[0], value, &client);
    failAfter = -1;
    assert(client.disconnect && socket.stopped && q_getCount(&queue) == 0);
    // Deep-copy ownership survives coalescing and disconnect cleanup too.
    value.format = homekit_format_string;
    value.string_value = const_cast<char *>("one");
    assert(homekit_event_enqueue(&queue, &characteristics[0], &value));
    value.string_value = const_cast<char *>("two");
    assert(homekit_event_enqueue(&queue, &characteristics[0], &value));
    assert(allocations.size() == 3); // backing store, event, copied string
    homekit_event_queue_clear(&queue);
    q_kill(&queue);
    assert(allocations.empty());
    // Copy failure while replacing a string must preserve the prior notification.
    q_init(&queue, sizeof(characteristic_event_t *), 4, FIFO, false);
    assert(homekit_event_enqueue(&queue, &characteristics[0], &value));
    value.string_value = const_cast<char *>("replacement");
    failAfter = 0;
    assert(!homekit_event_enqueue(&queue, &characteristics[0], &value));
    failAfter = -1;
    characteristic_event_t *pending;
    assert(q_peekIdx(&queue, &pending, 0));
    assert(strcmp(pending->value.string_value, "two") == 0);
    homekit_event_queue_clear(&queue);
    q_kill(&queue);
    assert(allocations.empty());
    // Allocation failure at each level of a nested TLV copy must release partial copies.
    uint8_t data[] = {1, 2};
    tlv_t item{};
    item.type = 1;
    item.size = 2;
    item.value = data;
    tlv_values_t tlv{};
    tlv.head = &item;
    value.format = homekit_format_tlv;
    value.tlv_values = &tlv;
    for (int failure = 0; failure < 4; failure++)
    {
        q_init(&queue, sizeof(characteristic_event_t *), 4, FIFO, false);
        failAfter = failure;
        assert(!homekit_event_enqueue(&queue, &characteristics[0], &value));
        failAfter = -1;
        assert(q_getCount(&queue) == 0 && allocations.size() == 1);
        q_kill(&queue);
        assert(allocations.empty());
    }
    // Exercise the production traversal with callbacks that free clients immediately.
    homekit_server_t server;
    client_context_t *first = new (test_malloc(sizeof(client_context_t))) client_context_t;
    client_context_t *second = new (test_malloc(sizeof(client_context_t))) client_context_t;
    first->next = second;
    server.clients = first;
    activeServer = &server;
    homekit_server_process(&server);
    assert(processedClients == 2 && allocations.empty());
    puts("HomeKit: 1000 bursts/disconnects, newest values, bounded queue, allocation failures: "
         "passed");
}
