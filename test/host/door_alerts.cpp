#include <cassert>
#include <initializer_list>
#include "door_alerts.h"

bool pendingDoorCommand = true;
unsigned queries = 0;
void send_get_status()
{
    ++queries;
}
#define ESP_LOGW(...) ((void)0)
#include "close_timeout.inc"

int main()
{
    close_completion_timeout();
    assert(queries == 1 &&
           !pendingDoorCommand); // Only query; no synthetic door notification or GPIO change.
    int minutes = -1;
    assert(parseLeftOpenMinutes("0", minutes) && minutes == 0);
    assert(parseLeftOpenMinutes("1440", minutes) && minutes == 1440);
    for (const char *bad : {"", "-1", "1.5", "1441", "99999999999999", "15oops"})
        assert(!parseLeftOpenMinutes(bad, minutes));
    DoorAlerts a;
    a.closeDidNotStart();
    assert(a.closeFailed());
    a.update(DoorAlerts::Closed, 0, 1);
    assert(!a.closeFailed());
    a.update(DoorAlerts::Unknown, 0, 1);
    a.update(DoorAlerts::Unknown, 900000, 1);
    assert(!a.leftOpen() && !a.closeFailed());
    a.update(DoorAlerts::Opening, 900000, 1);
    a.update(DoorAlerts::Stopped, 959999, 1);
    assert(!a.leftOpen());
    a.update(DoorAlerts::Stopped, 960000, 1);
    assert(a.leftOpen() && !a.closeFailed());
    a.update(DoorAlerts::Closing, 960001, 1);
    a.update(DoorAlerts::Closed, 973000, 1);
    assert(!a.leftOpen() && !a.closeFailed());

    for (auto stopped : {DoorAlerts::Stopped, DoorAlerts::Opening, DoorAlerts::Open})
    {
        a = DoorAlerts();
        a.update(DoorAlerts::Closing, 0, 0);
        a.update(stopped, 2000, 0);
        assert(a.closeFailed() && !a.leftOpen());
        a.update(DoorAlerts::Closing, 3000, 0);
        assert(a.closeFailed()); // A retry does not clear the alert.
        a.update(DoorAlerts::Closed, 16000, 0);
        assert(!a.closeFailed());
    }

    a = DoorAlerts();
    a.update(DoorAlerts::Closing, 0xfffffff0u, 1);
    a.update(DoorAlerts::Closing, uint32_t(0xfffffff0u + 59999u), 1);
    assert(!a.closeFailed() && !a.leftOpen());
    a.update(DoorAlerts::Unknown, uint32_t(0xfffffff0u + 60000u), 1);
    assert(a.closeFailed() && a.leftOpen());

    a = DoorAlerts();
    a.update(DoorAlerts::Open, 0, 15);
    a.update(DoorAlerts::Open, 60000, 15);
    assert(!a.leftOpen());
    a.update(DoorAlerts::Open, 60000, 1);
    assert(a.leftOpen()); // Changing duration uses elapsed time.
    a.update(DoorAlerts::Open, 60000, 0);
    assert(!a.leftOpen());
    a.update(DoorAlerts::Open, 60000, 15);
    assert(!a.leftOpen());
    for (uint64_t time = 60000; time < 2ull * 0xffffffffu; time += 60000)
        a.update(DoorAlerts::Open, uint32_t(time), 15);
    assert(a.leftOpen()); // Remains asserted across multiple millis wraps.
}
