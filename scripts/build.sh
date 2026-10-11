#!/bin/sh
set -eu

REPO_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
WORKSPACE_DIR=$(dirname "$REPO_DIR")
if [ "$#" -gt 2 ]; then
    printf 'Usage: %s [nucleo_h755zi_q|jparc_rev58] [rev58-profile]\n' "$0" >&2
    exit 2
fi
TARGET=${1:-nucleo_h755zi_q}
case "$TARGET" in
    nucleo_h755zi_q)
        if [ "$#" -gt 1 ]; then
            printf 'Nucleo does not accept a rev58 profile.\n' >&2
            exit 2
        fi ;;
    jparc_rev58)
        shift
        exec "$REPO_DIR/targets/jparc_rev58/scripts/build.sh" "$@" ;;
    *) printf 'Unknown target: %s\n' "$TARGET" >&2; exit 2 ;;
esac
APP_DIR=$REPO_DIR/targets/$TARGET
BUILD_DIR=${BUILD_DIR:-$WORKSPACE_DIR/build-nucleo-h755zi-q}
# Resolve relative overrides against the caller's directory for build and flash.
case "$BUILD_DIR" in
    /*) ;;
    *) BUILD_DIR=$PWD/$BUILD_DIR ;;
esac

cd "$WORKSPACE_DIR"
west build -p always --sysbuild \
    -b nucleo_h755zi_q/stm32h755xx/m7 \
    "$APP_DIR" \
    -d "$BUILD_DIR"
