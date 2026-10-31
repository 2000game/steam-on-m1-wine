#!/usr/bin/env bash
#
# 11-mark-wine-as-game.sh — Declare Wine's loader a game, so macOS can turn
# on Game Mode while a game runs in native fullscreen.
#
# Every Windows process runs as lib/wine/x86_64-unix/wine (ntdll re-executes
# that file; bin/wine only starts it). Its Info.plist is embedded in the
# binary (__TEXT,__info_plist) and has no LSApplicationCategoryType, so
# macOS never treats it as a game. This script adds
# LSApplicationCategoryType=public.app-category.games in place and re-signs
# the loader ad hoc. The original stays next to it as wine.gcenx-backup.
#
# Re-run after every Wine update; it is a no-op when the key is present.

# shellcheck source=lib/common.sh
source "$(dirname "$0")/lib/common.sh"
require_wine_installed

loader="$WINE_APP/Contents/Resources/wine/lib/wine/x86_64-unix/wine"
[[ -f "$loader" ]] || die "Wine loader not found: $loader"

log_step "Declaring the Wine loader a game"

work="$loader.new"
cp -p "$loader" "$work"
result=$(python3 -I "$REPO_ROOT/scripts/lib/set-loader-plist.py" "$work" \
    LSApplicationCategoryType=public.app-category.games) \
    || { rm -f "$work"; die "Could not rewrite the loader's Info.plist"; }

if [[ "$result" == unchanged ]]; then
    rm -f "$work"
    log_ok "Loader already declares the games category. Nothing to do."
    exit 0
fi

codesign -f -s - "$work" 2>/dev/null || { rm -f "$work"; die "codesign failed"; }
[[ -f "$loader.gcenx-backup" ]] || cp -p "$loader" "$loader.gcenx-backup"
mv "$work" "$loader"

wine_run cmd /c ver >/dev/null 2>&1 || {
    cp -p "$loader.gcenx-backup" "$loader"
    die "Wine failed with the patched loader; original restored."
}
log_ok "Loader patched ($result). Game Mode needs native fullscreen (Ctrl+Cmd+F)."
