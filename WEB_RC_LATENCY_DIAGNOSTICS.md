# Web RC response latency diagnosis

## Current evidence

The 2026-10-02 repeat is reported as one 328.2 ms `/web_rc` client round trip. The supplied `data/attitude/prop-off-route-20261002-022253.jsonl` does not contain a `web_latency_warning` or an individual `/web_rc` timing record, so that packet cannot be assigned retrospectively to TCP setup, request delivery, server acceptance, handler work, or response delivery.

The board-side measurements available in that capture show `http_rc_max_request_us=6028` and `http_rc_slow_requests=0`. The corresponding flight log has no armed sample gaps, maximum sampled `dt_s=1.065 ms`, and the retained worst loop is `1.163 ms`. These rule against a long `/web_rc` handler and a correlated control-loop stall for the reported event. They do not separate Wi-Fi/TCP from WebServer accept/read/queue delay or client scheduling.

The current server is Arduino `WebServer`, which services one client at a time. `ResponsiveWebServer` applies a 150 ms bound while waiting for the first byte and while parsing request lines and headers for the current text/form routes, then restores the normal timeout before dispatch. Dropped incomplete requests increment `http_idle_drops`. Bounding only the empty-socket state is insufficient because one received byte makes the upstream parser wait for its five-second stream timeout. The project has no multipart upload route; one must not be added without defining a separate, longer upload timeout. The route handler timer includes response sending. The client bench sends `Connection: close` and serializes its requests, so it avoids reusing a possibly stale TCP connection and does not create concurrent requests itself.

Board validation on 2026-10-07 used a one-byte stalled TCP request against the deployed ESP32-D build while a second client requested `/web_rc/status`. The second request completed in `299.7 ms`; the same stimulus had blocked it for more than seven seconds before the parser bound. The extra scheduling cycles above the 150 ms parser limit come from closing the rejected client and accepting the queued client on later Web task iterations.

## Next capture

`tools/run_prop_off_route_bench.py` now writes one `http_timing` JSONL record for each request, with these elapsed-time buckets:

- `tcp_connect_ms`: TCP connect call duration (includes Wi-Fi/TCP setup as seen by the client).
- `request_send_ms`: time spent in the HTTP request operation after subtracting measured connect time.
- `response_headers_ms`: wait for response headers after request transmission; it combines server accept/queue/parse/handler time and network return time.
- `response_body_ms`: response body read time.
- `total_ms`: full client round trip.

Warnings over 300 ms include the same fields. Correlate a warning with the nearest `/web_rc/status` snapshot's `http_rc_max_request_us`, `http_rc_slow_requests`, `http_max_handle_us`, `http_slow_handles`, `http_idle_drops`, Wi-Fi disconnect counters, and stick/packet ages. These server values are cumulative since boot, so a before/after status pair is needed to attribute a counter or maximum change to one request. The client buckets narrow the delay to connection setup, request transmission, response wait, or body transfer; `response_headers_ms` alone cannot distinguish WebServer queueing from network delay.

This is host-side diagnostics only. It does not change request semantics, control-loop behavior, arming, failsafe, or motor output. Any on-device reproduction remains subject to the existing propeller-off fixed-rig and exclusive-control preflight.
