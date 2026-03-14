#!/bin/bash

set -e
REPO="git@github.com:BlitzerFuse/termchat.git"
DIR="$HOME/termchat"
BIN_DIR="$HOME/bin"

echo "Starting termchat bootstrap..."
echo "Checking dependencies..."
for pkg in git make gcc; do
  if ! command -v $pkg &>/dev/null; then
    echo "$pkg not found, installing..."
    sudo pacman -Syu --noconfirm $pkg
  else
    echo "$pkg found."
  fi
done

if [ ! -d "$DIR" ]; then
  echo "Cloning termchat repo..."
  git clone "$REPO" "$DIR"
else
  echo "Repo already exists, pulling latest changes..."
  cd "$DIR"
  git pull origin main
fi

cd "$DIR"
make

mkdir -p "$BIN_DIR"
cp termchat "$BIN_DIR/"
if ! grep -q 'export PATH="$HOME/bin:$PATH"' ~/.bashrc; then
  echo 'export PATH="$HOME/bin:$PATH"' >>~/.bashrc
fi

echo ""
echo "termchat installed successfully!"
echo "Restart your terminal or run: source ~/.bashrc"
echo "You can run termchat using:"
echo "   termchat listen        # to wait for connection"
echo "   termchat connect <ip>  # to connect to peer"
