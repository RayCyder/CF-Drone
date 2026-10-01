#!/usr/bin/env python3
"""Build/run actual firmware math and state-machine regressions without hardware."""
from pathlib import Path
import shutil
import subprocess
import tempfile

root = Path(__file__).resolve().parent
compiler = shutil.which('clang++') or shutil.which('g++')
if not compiler:
    raise SystemExit('A C++17 compiler is required')
with tempfile.TemporaryDirectory(prefix='cf-drone-tests-') as tmp:
    for test in sorted(root.glob('test_*.cpp')):
        variants = [('', [])]
        if test.name == 'test_estimator_resources.cpp':
            variants.append(('_innovation_max_25', ['-DESTIMATE_ACCEL_INNOVATION_MAX_DEG=25.0f']))
            variants.append(('_adaptive_floor_00025', ['-DESTIMATE_ACCEL_MIN_ADAPTIVE_WEIGHT=0.00025f']))
            variants.append(('_fusion_filter_alpha_010', [
                '-DEST_ACCEL_FUSION_FILTER_ALPHA=0.1f',
                '-DTEST_EXPECT_EST_ACCEL_FUSION_FILTER_ALPHA=0.1f',
            ]))
        if test.name == 'test_flight_log.cpp':
            variants.append(('_armed_loop_trace', ['-DCF_DRONE_CAPTURE_ARMED_LOOP_TRACE']))
        for suffix, defines in variants:
            binary = str(Path(tmp) / f'{test.stem}{suffix}')
            subprocess.run([compiler, '-std=c++17', '-Wall', '-Wextra', '-Werror', '-Wno-vla',
                            '-I', str(root / 'stubs'), *defines, str(test), '-o', binary], check=True)
            subprocess.run([binary], check=True)
