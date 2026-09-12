# TCP retry history

The custom archive was removed in local4; the pinned Arduino framework now
provides the low-memory, low-flash lwIP variant with its standard retry limit.

Upstream commits 99f1d634 and 64e137c8 (2024-03-26) reduced TCP data retries to
two because vanished peers could retain roughly 4 KiB of heap for about thirty
minutes. They also disabled fragmentation/reassembly to avoid oversized mDNS
traffic. These are the commit author's observations, not measurements here.

We retain PIO_FRAMEWORK_ARDUINO_LWIP2_LOW_MEMORY_LOW_FLASH, including the
no-fragmentation configuration. Local3 bounds HomeKit writes and aborts failed
connections; local4 also aborts HomeKit teardown to release remaining TCP state.
Other TCP paths still warrant heap observation under loss. Restoring normal
retries is not a proven cure for the captured OTA receive stall.

Original change: https://github.com/ratgdo/homekit-ratgdo/commit/64e137c861129110128dcb6a66f03268d9b14f8e
