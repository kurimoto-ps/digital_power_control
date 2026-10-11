#!/bin/sh
set -eu
TARGET_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
WORKSPACE_DIR=$(CDPATH= cd -- "$TARGET_DIR/../../.." && pwd)
if [ "$#" -gt 1 ]; then
    printf 'Usage: %s [bringup|sdram|ethernet|spi|spi4_regs]\n' "$0" >&2
    exit 2
fi
PROFILE=${1:-bringup}
case "$PROFILE" in
    bringup|sdram|ethernet|spi|spi4_regs) ;;
    *) printf 'Unknown rev58 profile: %s\n' "$PROFILE" >&2; exit 2 ;;
esac
BUILD_DIR=${BUILD_DIR:-$WORKSPACE_DIR/build-jparc-rev58-$PROFILE}
case "$BUILD_DIR" in /*) ;; *) BUILD_DIR=$PWD/$BUILD_DIR ;; esac
west flash -d "$BUILD_DIR"
