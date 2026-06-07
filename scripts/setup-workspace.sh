#!/bin/sh
set -eu

APP_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
WORKSPACE_DIR=$(dirname "$APP_DIR")

cd "$WORKSPACE_DIR"

if [ ! -d .west ]; then
    west init -l "$APP_DIR"
fi

west update
west zephyr-export

printf '\nWorkspace ready at %s\n' "$WORKSPACE_DIR"
printf 'Build with: %s/scripts/build.sh\n' "$APP_DIR"
