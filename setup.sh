#!/bin/bash
mkdir -p ~/bin
cp termchat ~/bin/
if ! grep -q 'export PATH="$HOME/bin:$PATH"' ~/.bashrc; then
  echo 'export PATH="$HOME/bin:$PATH"' >>~/.bashrc
fi
echo "Installed termchat to ~/bin. Restart terminal or source ~/.bashrc to use."
