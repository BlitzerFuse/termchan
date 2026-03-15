## Termchat 

A very bad terminal messaging app i made just to test my skills in C.
You probably shouldn't use (or even download) this app. It's purely made
for me and a couple of friends just to have fun in the computer science lab
in our school. Part of the code HAS been written with AI. It's just a fun 
project and I don't recommend using it. I'm just happy it works. 

## Port

Default Port is set to 5000, you can edit the source files to change the port. 

## Features

- **Peer Discovery** — Automatically detects other users on the local network
- **Real-time Messaging** — Send and receive messages over LAN
- **ncurses TUI** — Clean terminal interface with chat window, peer list, and input bar
- **Lightweight** — Pure C with minimal dependencies

## Project Structure

```
termchat_plus/
├── include/
│   ├── chat.h
│   ├── discovery.h
│   ├── network.h
│   ├── protocol.h
│   └── tui.h
├── src/
│   ├── main.c
│   ├── chat/
│   │   └── chat.c
│   ├── network/
│   │   ├── discovery.c
│   │   └── network.c
│   └── tui/
│       └── tui.c
├── tools/
│   └── disc_debug.c
├── scripts/
│   ├── bootstrap.sh
│   └── check_firewall.sh
└── Makefile
```

## Dependencies

- gcc
- ncurces
- inetutils

On Arch Linux:
```bash
sudo pacman -S ncurses
```

On Debian/Ubuntu:
```bash
sudo apt install libncurses-dev
```

## Building

```bash
make
```

To clean build artifacts:
```bash
make clean
```

## Usage

```bash
./termchat_plus
```

On first run, the app will scan for peers on the local network. Once a peer is found, select them from the list and start chatting.

## Tools

`disc_debug` is a standalone debug utility for testing peer discovery independently:

```bash
gcc -Wall -Iinclude -o tools/disc_debug tools/disc_debug.c
./tools/disc_debug
```

## Scripts

- `scripts/bootstrap.sh` — Sets up the environment
- `scripts/check_firewall.sh` — Checks that the necessary ports are open for LAN communication

## Notes

- Both peers must be on the same local network
- Make sure your firewall allows the required ports (run `scripts/check_firewall.sh` to verify)
