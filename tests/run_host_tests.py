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
        binary = str(Path(tmp) / test.stem)
        subprocess.run([compiler, '-std=c++17', '-Wall', '-Wextra', '-Werror',
                        '-I', str(root / 'stubs'), str(test), '-o', binary], check=True)
        subprocess.run([binary], check=True)
