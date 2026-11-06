#!/bin/bash
# Re-apply the Cheshire/ZCU208 modifications to the third-party submodules.
# Run from the repository root after `git submodule update --init --recursive`.
#
# Generated build outputs (benchmark binaries, benchmark_scripts/*.sh,
# benchmark_output/, rootfs/usr/bin/*) are not included: the build regenerates them.

set -e
cd "$(dirname "$0")/.."
P="$PWD/patches"

apply() { # $1 = submodule dir, $2 = patch file
    if git -C "$1" apply --check "$P/$2" 2>/dev/null; then
        git -C "$1" apply "$P/$2"
        echo "applied  $2 -> $1"
    elif git -C "$1" apply --check --reverse "$P/$2" 2>/dev/null; then
        echo "skipped  $2 (already applied)"
    else
        echo "ERROR    $2 does not apply to $1" >&2
        exit 1
    fi
}

apply sw/tests/A_security_RISC       A_security_RISC.patch
apply sw/tests/SACTV                 SACTV.patch
apply sw/deps/cva6-sdk               cva6-sdk.patch
apply sw/deps/cva6-sdk/vitetris      cva6-sdk_vitetris.patch
