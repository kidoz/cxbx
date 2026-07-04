#!/bin/sh
# Build one conformance probe to an XBE using MSYS2 make + the scoop toolchain.
#
# Usage (from an MSYS2 shell):
#   sh build-probe.sh <probe-dir> [make-args...]
# e.g.
#   sh build-probe.sh tests/suite/probes/smoke -j4
#
# See tests/suite/README.md and <xbox-hello>\README.md for why this
# specific toolchain mix (MSYS2 cygwin make + scoop clang/lld + scoop g++ for
# host tools) is required on Windows.
set -e

export MSYSTEM=MINGW64
export NXDK_DIR="../nxdk"

SCOOP="/c/Users/USER/scoop"
NXDK_BIN_POSIX="../nxdk/bin"
export PATH="$PATH:$NXDK_BIN_POSIX:$SCOOP/apps/llvm/current/bin:$SCOOP/apps/gcc/current/bin:$SCOOP/shims"

PROBE_DIR="$1"
shift || true
if [ -z "$PROBE_DIR" ]; then
    echo "usage: build-probe.sh <probe-dir> [make-args...]" >&2
    exit 2
fi

cd "$PROBE_DIR"
exec /usr/bin/make NXDK_DIR="$NXDK_DIR" "$@"
