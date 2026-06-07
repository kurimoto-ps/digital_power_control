#!/bin/sh
set -eu

APP_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
WORKSPACE_DIR=$(dirname "$APP_DIR")
BUILD_DIR=${BUILD_DIR:-$WORKSPACE_DIR/build-digital-power-control}

west flash -d "$BUILD_DIR"
