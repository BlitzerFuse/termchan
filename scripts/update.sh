#!/bin/bash
# update.sh — pull the latest termchan and rebuild

set -e

DIR="$HOME/.termchan"
BIN_DIR="$HOME/.bin"

RED='\033[0;31m'; GREEN='\033[0;32m'; YELLOW='\033[1;33m'; NC='\033[0m'
info()    { echo -e "${GREEN}[termchan]${NC} $*"; }
warn()    { echo -e "${YELLOW}[termchan]${NC} $*"; }
die()     { echo -e "${RED}[termchan] error:${NC} $*" >&2; exit 1; }

[ -d "$DIR/.git" ] || die "Repo not found at $DIR. Run the bootstrap script first."
command -v make &>/dev/null || die "'make' not found."
command -v gcc  &>/dev/null || die "'gcc' not found."

info "Pulling latest changes from origin/main..."
OLD_HASH=$(git -C "$DIR" rev-parse HEAD)
git -C "$DIR" pull origin main

NEW_HASH=$(git -C "$DIR" rev-parse HEAD)

if [ "$OLD_HASH" = "$NEW_HASH" ]; then
    warn "Already up to date ($(git -C "$DIR" log -1 --format='%h %s'))."
    exit 0
fi

info "Updated: $OLD_HASH → $NEW_HASH"
echo ""
git -C "$DIR" log --oneline "${OLD_HASH}..${NEW_HASH}"
echo ""

info "Rebuilding..."
make -C "$DIR" clean
if ! make -C "$DIR"; then
    die "Build failed. Run 'git -C $DIR log' to see what changed."
fi

[ -f "$DIR/termchan" ] || die "Binary not found after build."

mkdir -p "$BIN_DIR"
cp "$DIR/termchan" "$BIN_DIR/termchan"
info "Installed to $BIN_DIR/termchan"

if ! echo "$PATH" | tr ':' '\n' | grep -qF "$BIN_DIR"; then
    warn "$BIN_DIR is not in your PATH."
    warn "Add this to your shell rc:  export PATH=\"\$HOME/.bin:\$PATH\""
fi

info "Update complete. Run 'termchan' to start."
