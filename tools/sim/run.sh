#!/bin/sh
# Builds one sketch for this computer and runs it in the simulator.
#   tools/sim/run.sh stages/03_imu_read "end@6000"
#   tools/sim/run.sh stages/08_sswingg_bat "connect@3000,swing@5000:400:1100:1,end@8000" -DHAS_BATTERY_SENSE=1
# Needs a C++ compiler: c++, clang++ or g++. See sim_main.cpp for the scenario words.
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
STAGE=$(cd "$1" && pwd)
NAME=$(basename "$STAGE")
SCENARIO=${2:-}
[ $# -ge 2 ] && shift 2 || shift 1
OUT=${SIM_BUILD_DIR:-${TMPDIR:-/tmp}/sswingg-sim}
mkdir -p "$OUT"
${CXX:-c++} -std=c++17 -O1 -Wall -Wextra -Wno-unused-parameter -Wno-unused-function \
  -I "$HERE/include" -I "$STAGE" -DSKETCH="\"$STAGE/$NAME.ino\"" "$@" \
  "$HERE/sim_main.cpp" -o "$OUT/$NAME"
exec "$OUT/$NAME" "$SCENARIO"
