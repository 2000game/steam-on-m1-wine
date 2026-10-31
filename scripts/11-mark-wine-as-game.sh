#!/usr/bin/env bash
#
# 11-mark-wine-as-game.sh — Run Windows processes from an app bundle with
# the games category, so macOS turns on Game Mode in native fullscreen.
#
# ntdll starts every Windows process as lib/wine/x86_64-unix/wine (bin/wine
# only starts it). That file is a bare executable, so macOS sees no bundle
# and no category; an embedded Info.plist is not enough. This script:
#
#   1. moves the original loader into x86_64-unix/Wine Game.app (Info.plist
#      from scripts/assets/wine-game-Info.plist, ntdll.so symlinked next to
#      it, because the loader looks for ntdll.so in its own folder);
#   2. puts a small trampoline (scripts/assets/wine-game-trampoline.c) at
#      the old path, which re-executes the loader inside the bundle.
#
# The original loader stays as wine.gcenx-backup. Re-run after every Wine
# update; it is a no-op when the bundle and trampoline are current.

# shellcheck source=lib/common.sh
source "$(dirname "$0")/lib/common.sh"
require_wine_installed
require_cmd clang

unix_dir="$WINE_APP/Contents/Resources/wine/lib/wine/x86_64-unix"
loader="$unix_dir/wine"
backup="$loader.gcenx-backup"
bundle="$unix_dir/Wine Game.app"
plist_src="$REPO_ROOT/scripts/assets/wine-game-Info.plist"
tramp_src="$REPO_ROOT/scripts/assets/wine-game-trampoline.c"
[[ -f "$loader" ]] || die "Wine loader not found: $loader"

log_step "Running Windows processes from Wine Game.app"

# A loader without the trampoline marker is a fresh Wine install: keep it.
if ! grep -q "wine trampoline" "$loader"; then
    cp -p "$loader" "$backup"
    log_info "Saved original loader as $(basename "$backup")"
fi
[[ -f "$backup" ]] || die "No original loader found ($backup)"

# codesign changes the copied loader, so compare the inputs via a stamp.
stamp_file="$unix_dir/wine.game-stamp"
stamp=$(cat "$backup" "$plist_src" "$tramp_src" | shasum -a 256 | awk '{print $1}')
if [[ -d "$bundle" && "$(cat "$stamp_file" 2>/dev/null)" == "$stamp" ]] && grep -q "wine trampoline" "$loader"; then
    log_ok "Bundle and trampoline are current. Nothing to do."
    exit 0
fi

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

mkdir -p "$work/Wine Game.app/Contents/MacOS"
cp "$plist_src" "$work/Wine Game.app/Contents/Info.plist"
cp -p "$backup" "$work/Wine Game.app/Contents/MacOS/wine"
ln -s ../../../ntdll.so "$work/Wine Game.app/Contents/MacOS/ntdll.so"
codesign -f -s - "$work/Wine Game.app" 2>/dev/null || die "codesign of the bundle failed"

clang -arch x86_64 -O2 -o "$work/wine" "$tramp_src" || die "Trampoline build failed"
codesign -f -s - "$work/wine" 2>/dev/null || die "codesign of the trampoline failed"

rm -rf "$bundle"
mv "$work/Wine Game.app" "$bundle"
mv "$work/wine" "$loader"

restore() {
    cp -p "$backup" "$loader"
    rm -rf "$bundle" "$stamp_file"
}
wine_run cmd /c ver >/dev/null 2>&1 || { restore; die "Wine failed with the trampoline; original loader restored."; }
echo "$stamp" > "$stamp_file"
log_ok "Windows processes now run from Wine Game.app. Game Mode needs native fullscreen (Ctrl+Cmd+F)."
