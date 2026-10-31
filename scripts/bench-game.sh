#!/usr/bin/env bash
#
# bench-game.sh — Start a game without input, measure frame pacing and CPU,
# stop it again. Steam must already be running (scripts/launch-steam.sh).
#
#   DXMT_CONFIG="d3d11.preferredMaxFrameRate=60;dxgi.maxFrameLatency=1" \
#       bash scripts/bench-game.sh latency1
#
# The game is started directly with this script's environment, so DXMT
# settings can change per run without restarting Steam. It only sits in its
# main menu; no input is sent. The save folder and registry key are
# snapshotted before and compared after the run; if anything changed, the
# snapshot is restored and the script says so.
#
# Frame data comes from DXMT_STATS_FILE (one line per 2 s, written by the DXMT
# fork in ~/dev/dxmt). CPU and energy come from top. Results are appended to
# $BENCH_DIR/results.tsv.
#
# Environment:
#   GAME_EXE       Windows exe        (default: Moon Sweepers)
#   GAME_APPID     Steam app id       (default: 5335510, Moon Sweepers)
#   GAME_ARGS      game arguments     (default: -force-d3d11, as Steam passes)
#   GAME_SAVE_DIR  folder to protect  (default: its LocalLow folder)
#   GAME_REG_KEY   registry key       (default: HKCU\Software\DragDropGames)
#   WARMUP         seconds before measuring (default 40: load + shader cache)
#   DURATION       seconds measured         (default 30)
#   BENCH_DIR      output folder (default ~/.cache/steam-on-m1-wine/bench)

# shellcheck source=lib/common.sh
source "$(dirname "$0")/lib/common.sh"
require_wine_installed
require_prefix_initialised

label=${1:-run}
: "${GAME_EXE:=$WINEPREFIX/drive_c/Program Files (x86)/Steam/steamapps/common/Moon Sweepers/MoonSweepers.exe}"
: "${GAME_APPID:=5335510}"
: "${GAME_ARGS:=-force-d3d11}"
: "${GAME_SAVE_DIR:=$WINEPREFIX/drive_c/users/$USER/AppData/LocalLow/DragDropGames}"
: "${GAME_REG_KEY:=HKCU\\Software\\DragDropGames}"
: "${WARMUP:=40}"
: "${DURATION:=30}"
: "${BENCH_DIR:=$HOME/.cache/steam-on-m1-wine/bench}"
: "${DXMT_CONFIG:=d3d11.preferredMaxFrameRate=60}"
export DXMT_CONFIG

exe_name=$(basename "$GAME_EXE")
exe_pattern="[${exe_name:0:1}]${exe_name:1}"
exe_pattern=${exe_pattern//./\\.}

pgrep -if "$exe_pattern" >/dev/null && die "$exe_name is already running; close it first."
pgrep -if '[s]team\.exe' >/dev/null || die "Steam is not running; start it with scripts/launch-steam.sh first."

run_id="$(date +%Y%m%d-%H%M%S)-$label"
run_dir="$BENCH_DIR/$run_id"
mkdir -p "$run_dir/save"

# -- Snapshot the save ---------------------------------------------------------

# Logs and crash reports change on every start and are not part of the save.
manifest() {
    (cd "$GAME_SAVE_DIR" && find . -type f ! -name 'Player*.log' ! -path './Crash reports/*' -print0 \
        | sort -z | xargs -0 shasum -a 256)
    wine_run reg export "$GAME_REG_KEY" "Z:${1//\//\\}" /y >/dev/null 2>&1 || true
    { grep -av "^$" "$1" 2>/dev/null || echo "no registry export"; } | shasum -a 256
}

log_step "Snapshotting save ($GAME_SAVE_DIR)"
rsync -a "$GAME_SAVE_DIR/" "$run_dir/save/"
manifest "$run_dir/reg-before.reg" > "$run_dir/manifest-before"
log_ok "Snapshot in $run_dir/save"

# -- Run -----------------------------------------------------------------------

stats_file="$run_dir/dxmt-stats.txt"
export WINEDLLOVERRIDES="dxgi,d3d11,d3d10core=n,b;bcrypt=b;ncrypt=b;gameoverlayrenderer,gameoverlayrenderer64=d"
export DXMT_STATS_FILE="Z:${stats_file//\//\\}"

# A sleeping display throttles presents; keep it awake while this script runs.
caffeinate -d -w $$ &

# Without steam_appid.txt the Steam API exits the game and has Steam relaunch
# it, with Steam's environment instead of ours. Only present during the run.
appid_file="$(dirname "$GAME_EXE")/steam_appid.txt"
appid_created=0
if [[ ! -f "$appid_file" ]]; then
    echo "$GAME_APPID" > "$appid_file"
    appid_created=1
fi

log_step "Starting $exe_name (DXMT_CONFIG=$DXMT_CONFIG)"
# shellcheck disable=SC2086
(cd "$(dirname "$GAME_EXE")" && arch -x86_64 "$WINE_BIN" "$exe_name" $GAME_ARGS >"$run_dir/wine.log" 2>&1) &

stop_game() {
    pkill -i -f "$exe_pattern" 2>/dev/null || true
    for _ in $(seq 20); do
        pgrep -if "$exe_pattern" >/dev/null || break
        sleep 0.5
    done
    pkill -9 -i -f "$exe_pattern" 2>/dev/null || true
    if (( appid_created )); then rm -f "$appid_file"; fi
}
trap stop_game EXIT

pid=""
for _ in $(seq 60); do
    pid=$(pgrep -if "$exe_pattern" | head -1 || true)
    [[ -n "$pid" ]] && break
    sleep 1
done
[[ -n "$pid" ]] || die "$exe_name did not start; see $run_dir/wine.log"
log_info "PID $pid, warming up ${WARMUP}s"
sleep "$WARMUP"
pgrep -if "$exe_pattern" >/dev/null || die "$exe_name exited during warm-up (Steam relaunch?); see $run_dir/wine.log"

skip_lines=$(wc -l < "$stats_file" 2>/dev/null || echo 0)
ws_pid=$(pgrep -x WindowServer | head -1)
log_info "Measuring ${DURATION}s"
samples=$(( DURATION / 3 + 1 ))
top -l "$samples" -s 3 -pid "$pid" -pid "$ws_pid" -stats pid,cpu,power > "$run_dir/top.txt"

stop_game
trap - EXIT

# -- Check the save ------------------------------------------------------------

manifest "$run_dir/reg-after.reg" > "$run_dir/manifest-after"
if cmp -s "$run_dir/manifest-before" "$run_dir/manifest-after"; then
    log_ok "Save unchanged"
    save_state=unchanged
else
    log_warn "Save changed during the run; restoring snapshot"
    diff "$run_dir/manifest-before" "$run_dir/manifest-after" >&2 || true
    rsync -a --delete --exclude 'Player*.log' --exclude 'Crash reports' "$run_dir/save/" "$GAME_SAVE_DIR/"
    wine_run reg import "Z:${run_dir//\//\\}\\reg-before.reg" >/dev/null 2>&1 || log_warn "reg import failed"
    save_state=restored
fi

# -- Summary -------------------------------------------------------------------

[[ -s "$stats_file" ]] || die "No DXMT stats written; is the DXMT fork with DXMT_STATS_FILE installed?"

# Median of each column over the measured windows.
frame_summary=$(tail -n +"$((skip_lines + 1))" "$stats_file" | grep -v '^#' | awk '
    { n++; for (i = 3; i <= NF; i++) col[i, n] = $i; nf = NF }
    END {
        if (n == 0) exit 1
        for (i = 3; i <= nf; i++) {
            m = 0; for (j = 1; j <= n; j++) v[++m] = col[i, j]
            # insertion sort; n is small
            for (a = 2; a <= m; a++) { x = v[a]; b = a - 1; while (b > 0 && v[b] > x) { v[b + 1] = v[b]; b-- } v[b + 1] = x }
            printf "%s%s", (i > 3 ? "\t" : ""), v[int((m + 1) / 2)]
        }
        printf "\n"
    }') || die "No stats lines in the measured window"

# top: first sample has no CPU delta, skip it.
cpu_summary=$(awk -v gp="$pid" -v wp="$ws_pid" '
    /^PID/ { s++; next }
    s > 1 && $1 == gp { gc += $2; ge += $3; gn++ }
    s > 1 && $1 == wp { wc += $2; wn++ }
    END { printf "%.0f\t%.0f\t%.0f", gc / gn, ge / gn, wc / wn }' "$run_dir/top.txt")

results="$BENCH_DIR/results.tsv"
[[ -f "$results" ]] || printf 'run\tdxmt_config\tfps\tp50_ms\tp95_ms\tp99_ms\tmax_ms\tboundary_ms\tprepare_ms\tflush_ms\tdrawable_ms\tcommit_ms\tsync_ms\tlatency\tsync_interval\tgame_cpu\tgame_energy\tws_cpu\tsave\n' > "$results"
printf '%s\t%s\t%s\t%s\t%s\n' "$run_id" "$DXMT_CONFIG" "$frame_summary" "$cpu_summary" "$save_state" >> "$results"

log_ok "Result appended to $results"
head -1 "$results" | cut -f3-18 >&2
tail -1 "$results" | cut -f3-18 >&2
