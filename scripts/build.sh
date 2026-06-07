#!/bin/sh
set -eu

APP_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
WORKSPACE_DIR=$(dirname "$APP_DIR")
BUILD_DIR=${BUILD_DIR:-$WORKSPACE_DIR/build-digital-power-control}

cd "$WORKSPACE_DIR"
west build -p always --sysbuild \
    -b nucleo_h755zi_q/stm32h755xx/m7 \
    "$APP_DIR" \
    -d "$BUILD_DIR"
