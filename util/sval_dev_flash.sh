#!/usr/bin/env bash
# Build no-chord dev firmware and install it with the in-firmware updater,
# with no prompts and no chord (docs/updater.md, "Dev builds", D43). Never
# released: release builds refuse these flags.
#
#   util/sval_dev_flash.sh [-n] SERIAL [left|right|both] [keep|fresh|blank]
#
# Run in WSL from a worktree. SERIAL is the board's USB serial (sval:...); the
# updater talks only to it. -n builds without flashing (SERIAL may be -).
#
# Modes (default fresh):
#   fresh  the keymap below; stored settings reset once to it (D39)
#   keep   the same keymap; stored settings are kept (SVAL_DEV_KEEP_SETTINGS)
#   blank  the blank keymap; stored settings reset once to it
# fresh and blank are ordinary builds: like every build, each resets stored
# settings on its first boot (build_reset.c, D39); installing the same file
# again does not. keep builds skip that, so the next keep install keeps the
# settings again. sval_update.py reset-settings resets them on any build (D42).
#
# The keymap: sval, or SVAL_DEV_KEYMAP=<name>. With SVAL_DEV_KEYMAP_DIR=<dir>,
# a private keymap kept outside the repository is copied in as
# keyboards/svalboard/keymaps/<name> for the build and never tracked (git's
# info/exclude); a verify_keymap.py beside that dir, if any, then checks the ELF.
#
# Output goes to $SVAL_HOME (default /mnt/c/Users/$USER/Svalboard):
#   firmware/dev/   the .uf2 and .svup of each build
#   tools/          sval_update.py and make_update.py
# Installing runs sval_update.py with Windows Python ($SVAL_PY), which has
# hidapi. Both halves need a no-chord build running for an install with no
# chord (the first one needs the chord, or a .uf2 copied in BOOTSEL). The .svup
# files are flagged DIAGNOSTIC, so a board on a release build refuses them.
set -euo pipefail

build_only=0
if [ "${1:-}" = "-n" ]; then build_only=1; shift; fi
serial=${1:-}
side=${2:-both}
mode=${3:-fresh}
usage="usage: $0 [-n] SERIAL [left|right|both] [keep|fresh|blank]"
case "$serial" in
    sval:?*) ;;
    -) [ "$build_only" = 1 ] || { echo "$usage" >&2; exit 2; } ;;
    *) echo "$usage" >&2; exit 2 ;;
esac
case "$side" in left|right) sides=$side ;; both) sides="left right" ;;
    *) echo "$usage" >&2; exit 2 ;; esac
case "$mode" in keep|fresh) km=${SVAL_DEV_KEYMAP:-sval} ;; blank) km=blank ;;
    *) echo "$usage" >&2; exit 2 ;; esac

root=$(cd "$(dirname "$0")/.." && pwd)
home=${SVAL_HOME:-/mnt/c/Users/$USER/Svalboard}
out=$home/firmware/dev
tools=$home/tools
mkdir -p "$out" "$tools"
cd "$root"
tc=$(ls -d ~/arm-toolchain/arm-gnu-toolchain-*/bin 2>/dev/null | tail -1 || true)
export PATH="$HOME/.venvs/sval-qmk/bin:${tc:+$tc:}$PATH"

# A private keymap: copied in, never tracked.
private=${SVAL_DEV_KEYMAP_DIR:-}
if [ -n "$private" ] && [ "$km" != blank ]; then
    [ -f "$private/keymap.c" ] || { echo "no $private/keymap.c" >&2; exit 1; }
    exclude=$(git rev-parse --git-path info/exclude)
    rel=/keyboards/svalboard/keymaps/$km/
    grep -qxF "$rel" "$exclude" 2>/dev/null || echo "$rel" >> "$exclude"
    rm -rf "keyboards/svalboard/keymaps/$km"
    cp -r "$private" "keyboards/svalboard/keymaps/$km"
fi

commit=$(git rev-parse --short=10 HEAD)
flags=(SVAL_UPDATER=yes SVAL_UPDATE_NO_CHORD=yes)
flags+=(SVAL_FW_VERSION_STRING="dev-$commit")
if [ "$mode" = keep ]; then
    flags+=(SVAL_DEV_KEEP_SETTINGS=yes)
fi

svups=()
for s in $sides; do
    log=${TMPDIR:-/tmp}/sval_dev_build_$s.log
    make -j"$(nproc)" "svalboard/$s:$km" "${flags[@]}" > "$log" 2>&1 || {
        tail -20 "$log" >&2; exit 1; }
    name=$s-$mode-$commit
    cp "svalboard_${s}_$km.uf2" "$out/$name.uf2"
    python3 -I keyboards/svalboard/tools/make_update.py \
        "svalboard_${s}_$km.uf2" -o "$out/$name.svup" \
        --kb "svalboard/$s" --keymap "$km" --diagnostic >/dev/null  # release boards refuse it
    if [ -n "$private" ] && [ -f "$private/../verify_keymap.py" ] && [ "$km" != blank ]; then
        python3 -I "$private/../verify_keymap.py" keyboards/svalboard/tools \
            ".build/svalboard_${s}_$km.elf"
    fi
    echo "$out/$name.uf2"
    svups+=("$(wslpath -w "$out/$name.svup")")
done
[ "$build_only" = 1 ] && exit 0

cp keyboards/svalboard/tools/sval_update.py \
    keyboards/svalboard/tools/make_update.py "$tools/"
py=${SVAL_PY:-$(ls /mnt/c/Users/"$USER"/AppData/Local/Programs/Python/Python3*/python.exe | tail -1)}
up=$(wslpath -w "$tools/sval_update.py")
if [ "${#svups[@]}" = 2 ]; then
    "$py" -u "$up" --serial "$serial" pair "${svups[@]}" --yes
else
    "$py" -u "$up" --serial "$serial" update "${svups[0]}" --yes
fi
