#include <cassert>
#include <cstdlib>
#include <cstring>
#include <set>
#include "homekit_decl.h"

extern "C" void *test_malloc(size_t n)
{
    return malloc(n);
}
extern "C" void *test_calloc(size_t n, size_t s)
{
    return calloc(n, s);
}
extern "C" void test_free(void *p)
{
    free(p);
}
extern "C" char *test_strdup(const char *s)
{
    return strdup(s);
}

int main(int argc, char **argv)
{
    assert(argc == 2);
    int mask = atoi(argv[1]);
    bool show_light = mask & 1, show_motion = mask & 2;
    // The exact service filtering block used by setup_homekit().
#include "homekit_service_list.inc"
    homekit_accessories_init(config.accessories);
    assert(current_door_state.id == 10 && target_door_state.id == 11);
    assert(garage_left_open.id == 102 && garage_close_failed.id == 112);
    assert(garage_left_open.value.uint8_value == 0 && garage_close_failed.value.uint8_value == 0);
    assert(config.config_number == 5);
    std::set<unsigned> ids;
    unsigned contacts = 0;
    for (auto **service = config.accessories[0]->services; *service; ++service)
    {
        assert(ids.insert((*service)->id).second);
        if (!strcmp((*service)->type, HOMEKIT_SERVICE_CONTACT_SENSOR))
            ++contacts;
        for (auto **ch = (*service)->characteristics; *ch; ++ch)
            assert(ids.insert((*ch)->id).second);
    }
    assert(contacts == 2);
}
