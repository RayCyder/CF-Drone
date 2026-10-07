#!/usr/bin/env python3
"""Explicit, read-only board acceptance: refreshed SSE works while old socket lives.

Run after deploying the rebuilt firmware:
  python3 tests/test_sse_refresh_device.py --host 192.168.31.203
No control commands, lease acquisition, reset, or motor operation is performed.
"""
import argparse
import socket
import time


def receive_until(client, marker, timeout):
    deadline = time.monotonic() + timeout
    received = bytearray()
    while marker not in received:
        remaining = deadline - time.monotonic()
        if remaining <= 0:
            raise TimeoutError(f"SSE did not emit {marker!r} within {timeout}s")
        client.settimeout(remaining)
        chunk = client.recv(4096)
        if not chunk:
            raise ConnectionError("SSE closed before the expected event")
        received.extend(chunk)
        if len(received) > 65536:
            raise AssertionError("SSE acceptance exceeded the receive budget")
    return received


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--host', required=True)
    parser.add_argument('--port', type=int, default=81)
    parser.add_argument('--cycles', type=int, default=30)
    args = parser.parse_args()
    if args.cycles < 2:
        parser.error('--cycles must be at least 2 to exercise overlapping connections')
    previous = current = None
    latencies = []
    try:
        for _ in range(args.cycles):
            started = time.monotonic()
            current = socket.create_connection((args.host, args.port), timeout=2)
            current.sendall(b'GET /stream HTTP/1.1\r\nHost: drone\r\n\r\n')
            # Keep the old browser generation open until the replacement has schema.
            initial = receive_until(current, b'event: schema\n', 1.5)
            assert initial.startswith(b'HTTP/1.1 200 OK\r\n'), 'SSE HTTP response failed'
            latencies.append((time.monotonic() - started) * 1000)
            if b'event: sample\n' not in initial:
                receive_until(current, b'event: sample\n', 2)
            if previous:
                previous.close()
            previous, current = current, None
        print(f'SSE refresh device acceptance: PASS; {args.cycles} overlapping '
              f'connections, worst schema latency {max(latencies):.1f}ms')
    finally:
        for client in (previous, current):
            if client:
                client.close()


if __name__ == '__main__':
    main()
