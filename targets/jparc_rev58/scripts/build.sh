#!/bin/sh
set -eu
TARGET_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
WORKSPACE_DIR=$(CDPATH= cd -- "$TARGET_DIR/../../.." && pwd)
if [ "$#" -gt 1 ]; then
    printf 'Usage: %s [bringup|sdram|ethernet|spi|spi4_regs|combined|adc_dummy]\n' "$0" >&2
    exit 2
fi
PROFILE=${1:-bringup}
case "$PROFILE" in
    bringup|sdram|ethernet|spi|spi4_regs|combined|adc_dummy) ;;
    *) printf 'Unknown rev58 profile: %s\n' "$PROFILE" >&2; exit 2 ;;
esac
BUILD_DIR=${BUILD_DIR:-$WORKSPACE_DIR/build-jparc-rev58-$PROFILE}
case "$BUILD_DIR" in /*) ;; *) BUILD_DIR=$PWD/$BUILD_DIR ;; esac
set --
if [ "$PROFILE" != bringup ]; then
    CONF_FILES=$TARGET_DIR/apps/bringup/profiles/$PROFILE.conf
    if [ "$PROFILE" = combined ] || [ "$PROFILE" = adc_dummy ]; then
        CONF_FILES="$TARGET_DIR/apps/bringup/profiles/spi4_regs.conf;$TARGET_DIR/apps/bringup/profiles/sdram.conf"
    fi
    if [ "$PROFILE" = adc_dummy ]; then
        CONF_FILES="$CONF_FILES;$TARGET_DIR/apps/bringup/profiles/adc_dummy.conf"
    fi
    set -- "-DDTC_OVERLAY_FILE=$TARGET_DIR/apps/bringup/profiles/$PROFILE.overlay" \
        "-DEXTRA_CONF_FILE=$CONF_FILES"
fi
cd "$WORKSPACE_DIR"
west build -p always -b jparc_rev58/stm32h745xx/m7 \
    "$TARGET_DIR/apps/bringup" -d "$BUILD_DIR" -- "$@"
