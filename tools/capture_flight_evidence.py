#!/usr/bin/env python3
"""Watch a flight session and save frozen evidence after an observed disarm."""

from __future__ import annotations

import argparse
import datetime as dt
import http.client
import json
import math
import time
from pathlib import Path
from urllib.parse import urlsplit

from monitor_flight_link import poll


def get(host: str, port: int, path: str, timeout: float) -> tuple[int, bytes]:
    connection = http.client.HTTPConnection(host, port, timeout=timeout)
    try:
        connection.request("GET", path, headers={"Connection": "close"})
        response = connection.getresponse()
        return response.status, response.read()
    finally:
        connection.close()


def save_frozen_snapshot(host: str, port: int, output: Path, timeout: float) -> bool:
    status_code, body = get(host, port, "/logs/status", timeout)
    if status_code != 200:
        return False
    log_status = json.loads(body)
    if log_status.get("state") != "FROZEN":
        return False

    generation = log_status.get("generation")
    output.mkdir(parents=True, exist_ok=True)
    (output / "logs-status.json").write_text(
        json.dumps(log_status, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    for path, name in (("/logs.csv", "flight-log.csv"),
                       ("/diag/trace.csv", "loop-trace.csv"),
                       ("/diag/trace/worst", "loop-worst.json")):
        code, data = get(host, port, path, timeout)
        if code != 200:
            raise RuntimeError(f"{path} returned HTTP {code}")
        (output / name).write_bytes(data)

    code, body = get(host, port, "/logs/status", timeout)
    if code != 200 or json.loads(body).get("generation") != generation:
        raise RuntimeError("flight log generation changed during export")
    return True


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--url", default="http://192.168.4.1/web_rc/status")
    parser.add_argument("--output-dir", required=True, type=Path)
    parser.add_argument("--interval", type=float, default=1.0)
    parser.add_argument("--timeout", type=float, default=3.0)
    parser.add_argument("--duration", type=float, default=0.0,
                        help="seconds; 0 runs until Ctrl-C")
    args = parser.parse_args()
    if (not all(math.isfinite(x) for x in (args.interval, args.timeout, args.duration)) or
            args.interval <= 0 or args.timeout <= 0 or args.duration < 0):
        parser.error("interval and timeout must be positive; duration must be nonnegative")
    url = urlsplit(args.url)
    if (url.scheme != "http" or not url.hostname or url.username or url.password or
            url.query or url.fragment or url.path != "/web_rc/status"):
        parser.error("--url must be a plain HTTP /web_rc/status URL")
    host, port = url.hostname, url.port or 80
    args.output_dir.mkdir(parents=True, exist_ok=True)

    start = next_poll = time.monotonic()
    saw_armed = False
    pending_snapshot = False
    session = 0
    with (args.output_dir / "status.jsonl").open("a", encoding="utf-8") as stream:
        try:
            while not args.duration or time.monotonic() - start < args.duration:
                time.sleep(max(0.0, next_poll - time.monotonic()))
                if args.duration and time.monotonic() - start >= args.duration:
                    break
                row = {"at": dt.datetime.now(dt.timezone.utc).isoformat(timespec="milliseconds")}
                row.update(poll(host, port, url.path, args.timeout))
                state = row.get("state")
                if isinstance(state, dict):
                    if state.get("armed") is True:
                        saw_armed = True
                    elif state.get("armed") is False and saw_armed:
                        saw_armed = False
                        pending_snapshot = True
                        session += 1
                    if state.get("armed") is False and pending_snapshot:
                        try:
                            if save_frozen_snapshot(host, port,
                                                    args.output_dir / f"session-{session:03d}",
                                                    args.timeout):
                                pending_snapshot = False
                                row["snapshot"] = f"session-{session:03d}"
                        except (OSError, TimeoutError, ValueError, http.client.HTTPException,
                                RuntimeError) as error:
                            row["snapshot_error"] = f"{type(error).__name__}: {error}"
                stream.write(json.dumps(row, ensure_ascii=False) + "\n")
                stream.flush()
                next_poll = max(next_poll + args.interval, time.monotonic())
        except KeyboardInterrupt:
            pass


if __name__ == "__main__":
    main()
