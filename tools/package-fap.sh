#!/usr/bin/env bash
# Package the committed app assets into one universal or board-specific FAP.
# This does not rebuild firmware/web content; tools/build-fap.sh refreshes those first.
set -euo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
APP_DIR="$REPO/flipper/hotspot-arcade"
ASSETS="$APP_DIR/assets"
VARIANT_ASSETS="$APP_DIR/.fap-assets"
VARIANT="universal"

if [[ "${1:-}" == "--variant" ]]; then
    [ "$#" -ge 2 ] || { echo "ERROR: --variant needs a value" >&2; exit 2; }
    VARIANT="$2"
    shift 2
elif [[ "${1:-}" == --variant=* ]]; then
    VARIANT="${1#--variant=}"
    shift
fi
case "$VARIANT" in
    s2|wroom|c5|all|universal) ;;
    *) echo "ERROR: variant must be s2, wroom, c5, all, or universal" >&2; exit 2 ;;
esac

UFBT="${UFBT:-ufbt}"
prepare_variant_assets() {
    local variant="$1" board="$2" root="$VARIANT_ASSETS/$variant"
    rm -rf "$root"
    mkdir -p "$root/firmware/$board"
    cp -R "$ASSETS/firmware/$board/." "$root/firmware/$board/"
    cp -R "$ASSETS/web" "$root/web"
    cp -R "$ASSETS/packs" "$root/packs"
    if [ -d "$ASSETS/trivia" ]; then cp -R "$ASSETS/trivia" "$root/trivia"; fi
}

build_variant_fap() {
    local variant="$1" board="$2"
    shift 2
    prepare_variant_assets "$variant" "$board"
    echo "==> Running ufbt for $variant"
    (
        cd "$APP_DIR"
        HA_FAP_ASSETS=".fap-assets/$variant" "$UFBT" "$@"
    )
    cp "$APP_DIR/dist/hotspot_arcade.fap" "$APP_DIR/dist/hotspot_arcade-$variant.fap"
    echo "==> Done: flipper/hotspot-arcade/dist/hotspot_arcade-$variant.fap"
}

case "$VARIANT" in
    s2) build_variant_fap s2 official_devboard "$@" ;;
    wroom) build_variant_fap wroom wroom "$@" ;;
    c5) build_variant_fap c5 c5 "$@" ;;
    all)
        build_variant_fap s2 official_devboard "$@"
        build_variant_fap wroom wroom "$@"
        build_variant_fap c5 c5 "$@"
        ;;
    universal)
        echo "==> Running ufbt for universal package"
        (cd "$APP_DIR" && HA_FAP_ASSETS="assets" "$UFBT" "$@")
        cp "$APP_DIR/dist/hotspot_arcade.fap" "$APP_DIR/dist/hotspot_arcade-universal.fap"
        echo "==> Done: flipper/hotspot-arcade/dist/hotspot_arcade-universal.fap"
        ;;
esac
