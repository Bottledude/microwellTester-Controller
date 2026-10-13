#!/usr/bin/env bash
# Host verification for rotate/revolve waveforms and tilted-axis synthesis.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

python3 monitor/test_field_waveforms.py

gcc -O2 -std=c11 -Wall -Wextra -Iinclude -o /tmp/test_field_tilt \
  monitor/test_field_tilt.c src/field_tilt.c -lm
/tmp/test_field_tilt
