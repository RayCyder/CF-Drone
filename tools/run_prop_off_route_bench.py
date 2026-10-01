#!/usr/bin/env python3
"""Run one bounded, propeller-off local control sequence on a fixed airframe.

The named phases are command phases. Without props, height or contact sensing,
this cannot verify physical takeoff, position hold or touchdown.
"""

import argparse
import csv
import datetime as dt
import http.client
import json
import signal
import time
from pathlib import Path
from urllib.parse import urlencode, urlsplit

from capture_motor_imu import SerialConsole, preflight
from run_attitude_calibration_bench import classify_log_gaps

ROOT = Path(__file__).resolve().parents[1]
PLAN = "# phase: simulated takeoff, hold, descent\n1.0 20 0 0 0\n0.8 20 0 0 0\n0.8 10 0 0 0\n"


def request(host, port, method, path, body=None, content_type=None, timeout=0.6):
    timings = {'tcp_connect_ms': 0.0}

    class TimedHTTPConnection(http.client.HTTPConnection):
        def connect(self):
            started = time.monotonic()
            super().connect()
            timings['tcp_connect_ms'] = round((time.monotonic() - started) * 1000, 1)

    connection = TimedHTTPConnection(host, port, timeout=timeout)
    headers = {'Connection': 'close'}
    if content_type:
        headers['Content-Type'] = content_type
    start = time.monotonic()
    try:
        connection.request(method, path, body=body, headers=headers)
        request_done = time.monotonic()
        timings['request_send_ms'] = round(max(0, request_done - start - timings['tcp_connect_ms'] / 1000) * 1000, 1)
        response = connection.getresponse()
        headers_done = time.monotonic()
        timings['response_headers_ms'] = round((headers_done - request_done) * 1000, 1)
        data = response.read()
        finished = time.monotonic()
        timings['response_body_ms'] = round((finished - headers_done) * 1000, 1)
        timings['total_ms'] = round((finished - start) * 1000, 1)
        if response.status not in (200, 202):
            raise RuntimeError(f'{path}: HTTP {response.status} {data[:160]!r}')
        return json.loads(data), timings['total_ms'], timings
    finally:
        connection.close()


def record(stream, kind, **fields):
    stream.write(json.dumps({
        'at': dt.datetime.now(dt.timezone.utc).isoformat(timespec='milliseconds'),
        'kind': kind, **fields,
    }, ensure_ascii=False) + '\n')
    stream.flush()


def download(host, port, path, destination):
    connection = http.client.HTTPConnection(host, port, timeout=15)
    try:
        connection.request('GET', path, headers={'Connection': 'close'})
        response = connection.getresponse()
        data = response.read()
        if response.status != 200:
            raise RuntimeError(f'{path}: HTTP {response.status} {data[:160]!r}')
        destination.write_bytes(data)
    finally:
        connection.close()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--url', default='http://192.168.31.189/web_rc/status')
    parser.add_argument('--serial-port', default='/dev/cu.usbserial-10')
    parser.add_argument('--output-dir', type=Path, default=ROOT / 'data/attitude')
    parser.add_argument('--confirm-no-props-fixed', action='store_true', required=True)
    parser.add_argument('--confirm-exclusive-control', action='store_true', required=True)
    args = parser.parse_args()
    url = urlsplit(args.url)
    if url.scheme != 'http' or not url.hostname or url.path != '/web_rc/status' or url.query or url.fragment:
        parser.error('--url must be a plain HTTP /web_rc/status URL')
    host, port = url.hostname, url.port or 80
    args.output_dir.mkdir(parents=True, exist_ok=True)
    output = args.output_dir / ('prop-off-route-' + dt.datetime.now().strftime('%Y%m%d-%H%M%S') + '.jsonl')
    serial = SerialConsole(args.serial_port)
    token = None
    arm_command_sent = False
    route_error = None
    landing_seen = False

    def timed_request(method, path, body=None, content_type=None):
        result, elapsed_ms, timing = request(host, port, method, path, body, content_type)
        record(stream, 'http_timing', path=path.split('?', 1)[0], **timing)
        if arm_command_sent and elapsed_ms > 300:
            record(stream, 'web_latency_warning', path=path.split('?', 1)[0], elapsed_ms=elapsed_ms, **timing)
        return result, elapsed_ms

    def stick_zero(stream):
        body = json.dumps({'t': 1, 'th': -100, 'r': 0, 'p': 0, 'y': 0, 'lease': token}, separators=(',', ':'))
        result, elapsed_ms = timed_request('POST', '/web_rc', body, 'application/json')
        record(stream, 'neutral_stick', elapsed_ms=elapsed_ms, response=result)
        if result.get('s') != 'ok':
            raise RuntimeError(f'neutral stick rejected: {result}')

    def hard_deadline(_signum, _frame):
        raise TimeoutError('route bench hard deadline')

    signal.signal(signal.SIGALRM, hard_deadline)
    with output.open('w', encoding='utf-8') as stream:
        try:
            preflight(serial)
            serial.send('mot')
            motors = serial.wait_for(r'^front-right ', 3, echo=False)
            record(stream, 'motor_preflight', line=motors)
            if motors != 'front-right 0 front-left 0 rear-right 0 rear-left 0':
                raise RuntimeError('motor output is not zero before route')
            for sample in range(10):
                start = time.monotonic()
                state, elapsed_ms = timed_request('GET', '/web_rc/status')
                record(stream, 'web_preflight', sample=sample + 1, elapsed_ms=elapsed_ms, state=state)
                if (state.get('armed') is not False or state.get('active') is not False or
                    state.get('throttle') != 0 or state.get('faults') != 0 or
                    state.get('voltage', 0) < 3.8 or state.get('arm_ready') is not True):
                    raise RuntimeError(f'Web preflight failed: {state}')
                time.sleep(max(0, 1 - (time.monotonic() - start)))
            log_status, _ = timed_request('GET', '/logs/status')
            record(stream, 'log_preflight', state=log_status)
            if log_status.get('state') != 'ROLLING' or log_status.get('missedSamples') != 0:
                raise RuntimeError('flight recorder needs rolling zero-miss baseline')

            lease, _ = timed_request('POST', '/web_rc/lease', '{}', 'application/json')
            token = lease['lease']
            lease_query = '?' + urlencode({'lease': token})
            uploaded, _ = timed_request('POST', '/route/upload' + lease_query, PLAN, 'text/plain')
            record(stream, 'uploaded', plan=PLAN, response=uploaded)
            revision = uploaded.get('plan_revision')
            route, _ = timed_request('GET', '/route/status')
            if route.get('state') != 'ready' or route.get('count') != 3 or route.get('plan_revision') != revision:
                raise RuntimeError(f'uploaded route mismatch: {route}')
            stick_zero(stream)
            state, _ = timed_request('GET', '/web_rc/status')
            if state.get('armed') or state.get('throttle') != 0 or state.get('control_source') != 2:
                raise RuntimeError(f'neutral route preflight failed: {state}')

            signal.alarm(15)
            serial.send('arm')
            arm_command_sent = True
            time.sleep(0.2)
            state, _ = timed_request('GET', '/web_rc/status')
            record(stream, 'armed_check', state=state)
            if state.get('armed') is not True or state.get('control_source') != 2:
                raise RuntimeError('route arming rejected')
            started, _ = timed_request('POST', '/route/start' + lease_query,
                                       urlencode({'revision': revision}), 'application/x-www-form-urlencoded')
            record(stream, 'start', response=started)
            if started.get('state') != 'start_pending':
                raise RuntimeError(f'route start rejected: {started}')

            last_stick = time.monotonic()
            seen_steps = set()
            while True:
                route, elapsed_ms = timed_request('GET', '/route/status')
                record(stream, 'route', elapsed_ms=elapsed_ms, state=route)
                phase = route.get('state')
                if phase == 'running':
                    seen_steps.add(route.get('step'))
                    if route.get('plan_revision') != revision:
                        raise RuntimeError('route revision changed while running')
                elif phase == 'landing':
                    landing_seen = True
                    time.sleep(0.2)
                    break
                elif phase not in ('start_pending',):
                    raise RuntimeError(f'unexpected route state: {route}')
                if time.monotonic() - last_stick >= 0.5:
                    stick_zero(stream)
                    last_stick = time.monotonic()
                    state, elapsed_ms = timed_request('GET', '/web_rc/status')
                    record(stream, 'web_running', elapsed_ms=elapsed_ms, state=state)
                    if state.get('armed') is not True or state.get('faults') != 0 or state.get('voltage', 0) < 3.5:
                        raise RuntimeError(f'route safety gate failed: {state}')
                time.sleep(0.12)
            if seen_steps != {1, 2, 3}:
                raise RuntimeError(f'not all three route steps observed: {sorted(seen_steps)}')
        except (OSError, TimeoutError, RuntimeError, ValueError) as error:
            route_error = error
        finally:
            signal.alarm(0)
            serial.send('disarm')
            time.sleep(0.3)
            if token:
                for _ in range(2):
                    try:
                        stick_zero(stream)
                    except (OSError, TimeoutError, RuntimeError, ValueError):
                        break
            serial.send('stab')
            time.sleep(0.1)
            serial.send('mot')
            motors = serial.wait_for(r'^front-right ', 3, echo=False)
            record(stream, 'motor_final', line=motors)
            serial.send('diag brief')
            brief = serial.wait_for(r'PREFLIGHT ', 3, echo=False)
            record(stream, 'serial_final', line=brief)
            serial.close()
            if motors != 'front-right 0 front-left 0 rear-right 0 rear-left 0' or 'armed=0' not in brief:
                raise RuntimeError('final serial safe state not verified')
            state, _, _ = request(host, port, 'GET', '/web_rc/status', timeout=2)
            record(stream, 'web_final', state=state)
            route, _, _ = request(host, port, 'GET', '/route/status', timeout=2)
            record(stream, 'route_final', state=route)
            if (state.get('armed') is not False or state.get('throttle') != 0 or
                route.get('arm') != 0 or route.get('mode') != 2):
                raise RuntimeError('final Web safe state not verified')

        if arm_command_sent:
            deadline = time.monotonic() + 3
            while True:
                status, _, _ = request(host, port, 'GET', '/logs/status', timeout=2)
                if status.get('state') == 'FROZEN' or time.monotonic() >= deadline:
                    break
                time.sleep(0.1)
            record(stream, 'log_final', state=status)
            if status.get('state') == 'FROZEN':
                for path, suffix in (('/logs.csv', 'flight-log.csv'), ('/diag/trace.csv', 'loop-trace.csv'),
                                     ('/diag/trace/worst', 'loop-worst.json')):
                    download(host, port, path, output.with_name(output.stem + '-' + suffix))
                rows = list(csv.DictReader(output.with_name(output.stem + '-flight-log.csv').open()))
                sequence_rows = [row for row in rows if row['control_source'] == '3' and row['armed'] == '1']
                armed_rows = [row for row in rows if row['armed'] == '1']
                gaps = classify_log_gaps(rows)
                record(stream, 'flight_log_summary', rows=len(rows), sequence_rows=len(sequence_rows),
                       max_armed_dt_ms=max((float(row['dt_s']) * 1000 for row in armed_rows), default=0),
                       missed_samples=status.get('missedSamples'), gaps=gaps)
                total_min = sum(bounds[0] for bounds in gaps.values())
                total_max = sum(bounds[1] for bounds in gaps.values())
                if not route_error and (
                    not landing_seen or route.get('state') != 'complete' or len(sequence_rows) < 100 or
                    any(int(row['fault_mask']) != 0 or float(row['dt_s']) > 0.005 or
                        float(row['battery_v']) < 3.5 for row in armed_rows) or
                    gaps['armed'][1] or gaps['transition'][1] or
                    not total_min <= status.get('missedSamples', -1) <= total_max):
                    route_error = RuntimeError('route evidence failed armed-loop or phase-localized log acceptance')
            elif not route_error:
                route_error = RuntimeError('flight log did not freeze')
    print(output)
    if route_error:
        raise RuntimeError(f'route bench stopped safely: {route_error}') from route_error


if __name__ == '__main__':
    main()
