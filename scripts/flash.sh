#!/bin/sh
set -eu

REPO_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
WORKSPACE_DIR=$(dirname "$REPO_DIR")
if [ "$#" -gt 1 ]; then
    printf 'Usage: %s [nucleo_h755zi_q|jparc_rev58]\n' "$0" >&2
    exit 2
fi
TARGET=${1:-nucleo_h755zi_q}
case "$TARGET" in
    nucleo_h755zi_q) ;;
    jparc_rev58)
        printf 'jparc_rev58: board support and firmware are not implemented yet.\n' >&2
        exit 2 ;;
    *) printf 'Unknown target: %s\n' "$TARGET" >&2; exit 2 ;;
esac
BUILD_DIR=${BUILD_DIR:-$WORKSPACE_DIR/build-nucleo-h755zi-q}
case "$BUILD_DIR" in
    /*) ;;
    *) BUILD_DIR=$PWD/$BUILD_DIR ;;
esac

west flash -d "$BUILD_DIR"
