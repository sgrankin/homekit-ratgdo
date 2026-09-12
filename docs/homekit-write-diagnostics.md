# HomeKit write diagnostics

## Current diagnostics (local3 changes, deployed in local4)

The original `HKTX short` example below is from local2. In local3, temporary short
writes are retried with the same bytes. `HKTX recovered` is emitted at most once
per connection and includes phase, endpoint, total bytes, write-call count and
duration. Successful single-call sends stay quiet.

Terminal failures use `HKTX failed`, with `sent` now the cumulative accepted byte
count and `tries` the number of write calls. `ack=0` means buffer acknowledgement
was not confirmed, even if all bytes were accepted. TCP/send-buffer/heap fields
are captured before aborting. The `step` and `enc` fields are omitted to keep the
expanded record compact. Further sends on the failed context produce no cascade
of socket-closed messages. See the [release and transport review](wifi-release-review.md)
for retry budgets, ACK ownership and remaining limitations.

Local4 additionally aborts sockets on teardown and emits one `OTA idle timeout`
record with receive counts and socket state. See the
[OTA field descriptions](ota-transfer-investigation.md#changes-deployed-in-local4).
These use the existing logger; a controlled reboot may save its log as before.

## Historical local2 diagnostic baseline

In local2, a failed write produced one `HKTX short` error record before the library
closes its socket. Successful writes produce no additional log records. The
then-existing timeout and disconnect policy was unchanged. No request body, query
string, pairing identifier, encryption key or payload is included.

Example from the host regression (not a hardware observation):

```
HKTX short phase=response ep=4 step=5 enc=1 want=530 sent=17 ms=501 tcp=4->4 conn=1 snd=17->0 heap=20000 max=12000 frag=40
```

| Field | Meaning |
| --- | --- |
| `phase` | `response` or unsolicited `event` notification |
| `ep` | Parsed endpoint: 0 unknown, 1 pair setup, 2 pair verify, 3 identify, 4 accessories, 5 read characteristics, 6 update characteristics, 7 pairings, 8 resource |
| `step` | Pairing state: 0 none, 1–3 setup, 4 first verification stage, 5 secure session established, 6 ended |
| `enc` | Whether this session uses encryption |
| `want`, `sent` | Requested bytes and WiFiClient's returned count; accepted bytes do not prove delivery or acknowledgement |
| `ms` | Entire write-call duration, including synchronous ACK waiting; not a direct measurement of the 500 ms progress timeout alone |
| `tcp` | lwIP state before and after write, before local stop: 0 CLOSED, 4 ESTABLISHED, 7 CLOSE_WAIT (other values follow lwIP's tcp_state enum) |
| `conn` | WiFiClient.connected() immediately after write, before local stop |
| `snd` | availableForWrite() before and after write (send-buffer capacity, not peer receive window) |
| `heap`, `max`, `frag` | ESP heap free bytes, largest free block bytes and fragmentation percent, sampled only on failure |

For notifications, `phase=event` takes precedence; `ep` may be the last request.
The existing client prefix and connection log associate the record with a peer.
An established connection with exhausted send capacity is consistent with
backpressure, but does not distinguish packet loss, slow ACKs or local resource
pressure. A closed connection does not identify which peer initiated closure.
Packet capture at the AP/router is still needed to establish wire-level cause.
Heap is sampled after the failed call and does not reveal a transient minimum.

After USB installation, reproduce ordinary Home access and retrieve a bounded log
sample. Retain the firmware ELF with the log. Do not enable verbose payload/crypto
logging or keep multiple streaming browser tabs open for this investigation.

Host ASan/UBSan tests compile the actual write function and check pre-close
observations, partial and zero writes, quiet success, event classification and
unsigned clock rollover. The notification regression also checks that the event
label is scoped to notification sending.
