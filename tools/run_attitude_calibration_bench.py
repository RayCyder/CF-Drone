#!/usr/bin/env python3
"""Bounded four-motor 30% bench run after a level, propeller-off preflight.

This collects control timing and the frozen flight log. It does not estimate
absolute attitude accuracy or automatically change fusion gains.
"""
import argparse
import csv
import datetime as dt
import http.client
import json
import math
import signal
import time
from pathlib import Path
from urllib.parse import urlsplit

from capture_motor_imu import SerialConsole, preflight

ROOT = Path(__file__).resolve().parents[1]
HOST = '192.168.4.1'
PORT = 80
OUT = Path()
WEB_RC_SOURCE = 2

def request(method, path, payload=None, timeout=0.8):
    conn = http.client.HTTPConnection(HOST, PORT, timeout=timeout)
    body = None if payload is None else json.dumps(payload, separators=(',', ':'))
    try:
        conn.request(method, path, body=body,
                     headers={'Content-Type':'application/json','Connection':'close'})
        response = conn.getresponse()
        data = response.read()
        if response.status != 200:
            raise RuntimeError(f'{path}: HTTP {response.status} {data[:160]!r}')
        return json.loads(data)
    finally:
        conn.close()

def stick(token, throttle_raw):
    return request('POST', '/web_rc',
                   {'t':1,'th':throttle_raw,'r':0,'p':0,'y':0,'lease':token}, 0.35)

def record(stream, kind, **fields):
    item={'at':dt.datetime.now(dt.timezone.utc).isoformat(timespec='milliseconds'),
          'kind':kind,**fields}
    stream.write(json.dumps(item,ensure_ascii=False)+'\n')
    stream.flush()

def run_stage(token, raw, seconds, stream):
    due=begin=time.monotonic()
    count=0
    while time.monotonic()-begin < seconds:
        start=time.monotonic()
        response=stick(token,raw)
        elapsed=time.monotonic()-start
        record(stream,'stick',raw=raw,elapsed_ms=round(elapsed*1000,1),response=response)
        if elapsed > 0.25 or response.get('arm') != 1:
            raise RuntimeError(f'control response delayed or disarmed: {elapsed:.3f}s {response}')
        count+=1
        due+=0.05
        time.sleep(max(0,due-time.monotonic()))
    return count

def check_web_link(stream):
    latencies=[]
    for _ in range(10):
        begin=time.monotonic()
        state=request('GET','/web_rc/status',timeout=0.8)
        elapsed=time.monotonic()-begin
        if (elapsed>0.3 or state.get('armed') is not False or
            state.get('active') is not False or state.get('faults') != 0):
            raise RuntimeError(f'10-second Web preflight failed: {elapsed:.3f}s {state}')
        latencies.append(elapsed*1000)
        time.sleep(max(0,1-(time.monotonic()-begin)))
    record(stream,'web_link_preflight',samples=len(latencies),
           min_ms=round(min(latencies),1),max_ms=round(max(latencies),1))

def classify_log_gaps(rows):
    gaps={phase:[0,0] for phase in ('armed','disarmed','transition')}
    for previous,current in zip(rows,rows[1:]):
        delta=float(current['t'])-float(previous['t'])
        if delta < 0.015:
            continue
        # Samples are scheduled at 100 Hz, but their recorded timestamps can
        # slip within a slot. A 57 ms gap may represent four or five misses.
        minimum=max(0,math.floor(delta/0.01)-1)
        maximum=max(0,math.ceil(delta/0.01)-1)
        phase=('armed' if previous['armed']=='1' and current['armed']=='1' else
               'disarmed' if previous['armed']=='0' and current['armed']=='0' else
               'transition')
        gaps[phase][0]+=minimum
        gaps[phase][1]+=maximum
    return gaps

def validate_log(path, trace_path, expected_missed=None):
    with path.open(newline='',encoding='utf-8') as stream:
        rows=list(csv.DictReader(stream))
    armed=[row for row in rows if row['armed']=='1']
    platform=[row for row in armed if abs(float(row['rc_throttle'])-0.3)<0.01]
    if len(platform)<100:
        raise RuntimeError(f'30% platform too short in frozen log: {len(platform)} samples')
    for row in armed:
        if (int(row['fault_mask']) != 0 or float(row['dt_s'])>0.005 or
            float(row['battery_v'])<3.5 or float(row['mix_scale'])<0.8 or
            any(float(row[f'motor_{motor}'])<=0 for motor in ('rl','rr','fr','fl'))):
            raise RuntimeError(f'armed flight-log acceptance failed at t={row["t"]}')
    with trace_path.open(newline='',encoding='utf-8') as stream:
        trace_rows=list(csv.DictReader(stream))
    # The armed-loop diagnostic image records a rolling window of ordinary
    # loops as well. Only long intervals indicate an overrun in this export.
    long_trace_rows=[row for row in trace_rows if int(row['dt_us'])>1500]
    if long_trace_rows:
        raise RuntimeError(f'{len(long_trace_rows)} long loop trace rows require review')
    confidence=[float(row['accel_correction_confidence']) for row in platform]
    accel_norms=[math.sqrt(sum(float(row[f'acc_{axis}'])**2 for axis in 'xyz'))
                 for row in platform]
    in_gravity_band=sum(9.80665*0.95<=value<=9.80665*1.05
                        for value in accel_norms)
    gaps=classify_log_gaps(rows)
    if gaps['armed'][1] or gaps['transition'][1]:
        raise RuntimeError(f'flight-log gaps during or near arming require review: {gaps}')
    minimum_total=sum(bounds[0] for bounds in gaps.values())
    maximum_total=sum(bounds[1] for bounds in gaps.values())
    if expected_missed is not None and not minimum_total<=expected_missed<=maximum_total:
        raise RuntimeError(f'flight-log missed samples cannot be fully located: '
                           f'file={gaps}, status={expected_missed}')
    return {'rows':len(rows),'armed_rows':len(armed),'platform_rows':len(platform),
            'max_armed_dt_ms':round(max(float(row['dt_s']) for row in armed)*1000,3),
            'min_armed_voltage_v':min(float(row['battery_v']) for row in armed),
            'min_armed_mix_scale':min(float(row['mix_scale']) for row in armed),
            'platform_zero_confidence_rows':sum(value==0 for value in confidence),
            'platform_mean_confidence':round(sum(confidence)/len(confidence),4),
            'platform_accel_norm_in_1g_5pct_rows':in_gravity_band,
            'possible_missing_in_armed_gaps':gaps['armed'],
            'possible_missing_in_disarmed_gaps':gaps['disarmed'],
            'possible_missing_across_arm_transition':gaps['transition'],
            'trace_rows':len(trace_rows),
            'max_trace_dt_us':max((int(row['dt_us']) for row in trace_rows),default=0)}

def main():
    global HOST, PORT, OUT
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--url',default='http://192.168.4.1/web_rc/status')
    parser.add_argument('--serial-port',default='/dev/cu.usbserial-10')
    parser.add_argument('--output-dir',type=Path,default=ROOT/'data/attitude')
    parser.add_argument('--confirm-no-props-fixed',action='store_true',required=True,
                        help='operator confirms all propellers removed and airframe fixed')
    parser.add_argument('--confirm-exclusive-control',action='store_true',required=True,
                        help='operator confirms other Web RC clients closed')
    args=parser.parse_args()
    url=urlsplit(args.url)
    if url.scheme!='http' or not url.hostname or url.path!='/web_rc/status' or url.query or url.fragment:
        parser.error('--url must be a plain HTTP /web_rc/status URL')
    HOST,PORT=url.hostname,url.port or 80
    OUT=args.output_dir/('four-motor-30pct-'+dt.datetime.now().strftime('%Y%m%d-%H%M%S')+'.jsonl')
    serial=SerialConsole(args.serial_port)
    token=None
    armed_started=False
    run_error=None
    OUT.parent.mkdir(parents=True,exist_ok=True)
    def deadline(_signum,_frame): raise TimeoutError('bench run hard deadline')
    signal.signal(signal.SIGALRM,deadline)
    try:
        with OUT.open('w',encoding='utf-8') as stream:
            preflight(serial)
            serial.send('mot')
            motor_line=serial.wait_for(r'^front-right ',2,echo=False)
            record(stream,'motor_preflight',line=motor_line)
            if motor_line!='front-right 0 front-left 0 rear-right 0 rear-left 0':
                raise RuntimeError('motor output is not zero before arming')
            state=request('GET','/web_rc/status')
            record(stream,'preflight',state=state)
            if (state.get('armed') is not False or state.get('active') is not False or
                state.get('throttle') != 0 or state.get('faults') != 0 or
                state.get('voltage',0) < 3.8 or state.get('arm_ready') is not True):
                raise RuntimeError('Web preflight failed')
            check_web_link(stream)
            log_status=request('GET','/logs/status')
            record(stream,'log_preflight',state=log_status)
            if log_status.get('state')!='ROLLING' or log_status.get('missedSamples')!=0:
                raise RuntimeError('flight log must be rolling with zero missed samples before arming')
            lease=request('POST','/web_rc/lease',{})
            token=lease['lease']
            record(stream,'lease',timeout_ms=lease.get('timeout_ms'))
            stick(token,-100)
            time.sleep(0.15)
            state=request('GET','/web_rc/status')
            record(stream,'neutral',state=state)
            if (state.get('armed') or state.get('throttle') != 0 or
                state.get('faults') != 0 or state.get('control_source') != WEB_RC_SOURCE):
                raise RuntimeError('neutral stick preflight failed')
            signal.alarm(10)
            serial.send('arm')
            time.sleep(0.2)
            state=request('GET','/web_rc/status')
            record(stream,'armed_check',state=state)
            if state.get('armed') is not True or state.get('control_source') != WEB_RC_SOURCE:
                raise RuntimeError('arming rejected')
            armed_started=True
            for raw, seconds in ((-80,0.4),(-60,0.4),(-40,1.5)):
                count=run_stage(token,raw,seconds,stream)
                state=request('GET','/web_rc/status')
                record(stream,'stage',raw=raw,count=count,state=state)
                if (state.get('armed') is not True or state.get('faults') != 0 or
                    state.get('control_source') != WEB_RC_SOURCE or
                    state.get('voltage',0) < 3.5 or
                    abs(state.get('throttle',-1)-(raw+100)/2) > 3.0):
                    raise RuntimeError(f'stage gate failed: {raw} {state}')
            for _ in range(3): stick(token,-100)
            record(stream,'neutral_sent')
    except (OSError,TimeoutError,RuntimeError,ValueError) as error:
        run_error=error
    finally:
        signal.alarm(0)
        try:
            if token:
                for _ in range(2):
                    try: stick(token,-100)
                    except Exception: break
            serial.send('disarm')
            time.sleep(0.3)
            serial.send('mot')
            motor_line=serial.wait_for(r'^front-right ',2,echo=False)
            with OUT.open('a',encoding='utf-8') as stream:
                record(stream,'motor_final',line=motor_line)
            if motor_line!='front-right 0 front-left 0 rear-right 0 rear-left 0':
                raise RuntimeError('motor output is not zero after disarm')
        finally:
            serial.close()
        state=request('GET','/web_rc/status',timeout=2)
        with OUT.open('a',encoding='utf-8') as stream:
            record(stream,'final',state=state)
        if state.get('armed') is not False or state.get('throttle') != 0:
            raise RuntimeError('final safe state not verified')
    print(OUT)
    if not armed_started:
        if run_error: raise run_error
        return
    try:
        # The rolling trace can overwrite the triggering loop while the
        # post-disarm flight log finishes. Preserve the separate peak first.
        try:
            worst=request('GET','/diag/trace/worst',timeout=2)
            with OUT.open('a',encoding='utf-8') as stream:
                record(stream,'loop_worst',trace=worst)
            if worst.get('available'):
                (OUT.parent/(OUT.stem+'-loop-worst.json')).write_text(
                    json.dumps(worst,separators=(',',':'))+'\n',encoding='utf-8')
        except (OSError,TimeoutError,RuntimeError,ValueError) as error:
            with OUT.open('a',encoding='utf-8') as stream:
                record(stream,'loop_worst_unavailable',error=str(error))
        # Disarm starts a one-second post-trigger capture. Allow it to finish
        # before exporting; an immediate status request may still say POST_TRIGGER.
        deadline=time.monotonic()+3
        while True:
            status=request('GET','/logs/status',timeout=2)
            if status.get('state')=='FROZEN' or time.monotonic()>=deadline:
                break
            time.sleep(0.1)
        if status.get('state')=='FROZEN':
            saved={}
            for path,suffix in (('/logs.csv','flight-log.csv'),
                                ('/diag/trace.csv','loop-trace.csv')):
                conn=http.client.HTTPConnection(HOST,PORT,timeout=15)
                try:
                    conn.request('GET',path,headers={'Connection':'close'})
                    response=conn.getresponse()
                    if response.status!=200:raise RuntimeError(f'{path}: HTTP {response.status}')
                    saved[suffix]=OUT.parent/(OUT.stem+'-'+suffix)
                    saved[suffix].write_bytes(response.read())
                finally:conn.close()
            with OUT.open('a',encoding='utf-8') as stream:
                if run_error:
                    record(stream,'aborted',reason=str(run_error),log_status=status)
                else:
                    summary=validate_log(saved['flight-log.csv'],saved['loop-trace.csv'],
                                         status.get('missedSamples'))
                    record(stream,'acceptance',**summary)
            if not run_error:
                print('frozen flight log and loop trace passed acceptance:',summary)
        else:
            raise RuntimeError('flight log did not freeze after disarm')
    except (OSError,TimeoutError,RuntimeError,ValueError) as error:
        raise RuntimeError(f'post-disarm evidence/acceptance failed: {error}; preserve frozen log') from error
    if run_error:
        raise RuntimeError(f'bench stopped safely after arming: {run_error}; frozen evidence saved') from run_error

if __name__=='__main__': main()
