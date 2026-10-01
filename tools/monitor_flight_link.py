#!/usr/bin/env python3
"""Record read-only Web RC status and connection timing for a flight session."""

from __future__ import annotations

import argparse
import datetime as dt
import http.client
import json
import math
import time
from pathlib import Path
from urllib.parse import urlsplit


def poll(host: str, port: int, path: str, timeout: float) -> dict:
    start = time.monotonic()
    connection = http.client.HTTPConnection(host, port, timeout=timeout)
    result: dict = {"state": None}
    try:
        connection.connect()
        result["connect_ms"] = round((time.monotonic() - start) * 1000, 1)
        connection.request("GET", path, headers={"Connection": "close"})
        response = connection.getresponse()
        result["first_byte_ms"] = round((time.monotonic() - start) * 1000, 1)
        body = response.read()
        result["http_status"] = response.status
        if response.status == 200:
            result["state"] = json.loads(body)
        else:
            result["error"] = body[:160].decode("utf-8", "replace")
    except (OSError, TimeoutError, ValueError, http.client.HTTPException) as error:
        result["error"] = f"{type(error).__name__}: {error}"
    finally:
        result["total_ms"] = round((time.monotonic() - start) * 1000, 1)
        connection.close()
    return result


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--url", default="http://192.168.4.1/web_rc/status")
    parser.add_argument("--output", required=True, type=Path, help="JSONL output path")
    parser.add_argument("--interval", type=float, default=1.0, help="poll interval in seconds")
    parser.add_argument("--timeout", type=float, default=3.0, help="per-request timeout in seconds")
    parser.add_argument("--duration", type=float, default=0.0, help="seconds; 0 runs until Ctrl-C")
    args = parser.parse_args()
    if (not all(math.isfinite(value) for value in (args.interval, args.timeout, args.duration)) or
            args.interval <= 0 or args.timeout <= 0 or args.duration < 0):
        parser.error("interval and timeout must be positive; duration must be nonnegative")
    url = urlsplit(args.url)
    if url.scheme != "http" or not url.hostname or url.username or url.password or url.query or url.fragment:
        parser.error("--url must be a plain HTTP status URL without credentials or query")
    path = url.path or "/"
    if path != "/web_rc/status":
        parser.error("--url must point to /web_rc/status")

    args.output.parent.mkdir(parents=True, exist_ok=True)
    started = next_poll = time.monotonic()
    previous_uptime: int | None = None
    try:
        with args.output.open("a", encoding="utf-8") as stream:
            while not args.duration or time.monotonic() - started < args.duration:
                time.sleep(max(0.0, next_poll - time.monotonic()))
                if args.duration and time.monotonic() - started >= args.duration:
                    break
                row = {"at": dt.datetime.now(dt.timezone.utc).isoformat(timespec="milliseconds")}
                row.update(poll(url.hostname, url.port or 80, path, args.timeout))
                state = row["state"]
                if isinstance(state, dict) and isinstance(state.get("uptime_ms"), int):
                    uptime = state["uptime_ms"]
                    row["reboot_suspected"] = (previous_uptime is not None and
                                               previous_uptime > uptime + 1000 and
                                               previous_uptime < 0xFFFF0000)
                    previous_uptime = uptime
                stream.write(json.dumps(row, ensure_ascii=False) + "\n")
                stream.flush()
                next_poll = max(next_poll + args.interval, time.monotonic())
    except KeyboardInterrupt:
        pass


if __name__ == "__main__":
    main()
