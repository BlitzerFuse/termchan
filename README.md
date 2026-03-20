# term-chan

A LAN chat application for the terminal, written in C. No internet, no server, no accounts — just people on the same network.

---

## Features

- **Automatic peer discovery** — broadcasts a UDP beacon every 500ms; peers appear in the connect screen as soon as they launch the app
- **Multi-user rooms** — host creates a session, peers join through a lobby, host starts when ready (up to 7 peers)
- **Optional password protection** — none, auto-generated, or manually set
- **ncurses TUI** — full-terminal chat window with scrollback, input bar, and status messages
- **Slash commands** — see [Commands](#commands) below
- **Configurable port** — set at launch via flag or in the menu

---

## Dependencies

| Package | Arch | Debian/Ubuntu | Fedora |
|---|---|---|---|
| gcc | `gcc` | `gcc` | `gcc` |
| make | `make` | `make` | `make` |
| ncurses | `ncurses` | `libncurses-dev` | `ncurses-devel` |

---

## Install

```bash
curl -fsSL https://raw.githubusercontent.com/BlitzerFuse/termchan/main/scripts/bootstrap.sh | bash
source ~/.bashrc
```

Or manually:

```bash
git clone git@github.com:BlitzerFuse/termchan.git
cd termchan
make
./termchan
```

---

## Usage

```
termchan [-p <port>]
```

| Flag | Default | Description |
|---|---|---|
| `-p`, `--port` | `5000` | TCP port to listen or connect on |
| `-h`, `--help` | — | Print usage |

Running two instances on the same machine requires different ports:

```bash
# Terminal 1
./termchan -p 5000

# Terminal 2
./termchan -p 5001
```

---

## How it works

**Listen mode** — You become the host. The app opens a TCP listener and waits for connections. Peers who are in Connect mode will see you in their peer list within a second. You see a lobby showing everyone who has joined. Press Enter to start the chat session, which sends all peers a start signal simultaneously.

**Connect mode** — The app starts a background beacon thread that broadcasts your presence over UDP and listens for others doing the same. The peer list updates live. Select a peer with arrow keys and Enter, or press Tab/i to type an IP manually. After connecting you wait on a holding screen until the host starts the session.

---

## Commands

| Command | Description |
|---|---|
| `/help` | List all commands |
| `/nick <name>` | Change your nickname — notifies all peers |
| `/me` | Show your name, IP, role, and peer count |
| `/ip` | Show your local IP address |
| `/reply <msg>` | Reply to the last person who sent a message |
| `/pass` | Show the session password (host only) |
| `/clear` | Clear the chat window |
| `/quit` | Leave the session |

---

## Project structure

```
termchan/
├── include/
│   ├── protocol.h       # Packet struct, MsgType enum
│   ├── session.h        # Session struct (peers, fds, nicks)
│   ├── chat.h           # start_chat()
│   ├── room.h           # room_broadcast / add / remove
│   ├── network.h        # TCP connect / accept / handshake
│   ├── discovery.h      # UDP beacon API
│   ├── commands.h       # Command dispatch
│   ├── tui.h            # Public TUI API
│   └── tui_internal.h   # TUI-internal helpers
├── src/
│   ├── main.c
│   ├── chat/
│   │   ├── chat.c       # Per-peer recv threads, input loop
│   │   ├── commands.c   # /command handlers
│   │   └── room.c       # Peer list + mutex-safe broadcast
│   ├── network/
│   │   ├── network.c    # TCP listener, connect, handshake
│   │   └── discovery.c  # UDP beacon thread + peer table
│   └── tui/
│       ├── tui.c        # ncurses init, resize signal
│       ├── tui_menu.c   # Main menu, peer list, waiting screens
│       ├── tui_chat.c   # Chat window, input bar, message display
│       └── tui_lobby.c  # Host lobby screen
├── scripts/
│   └── bootstrap.sh     # Install dependencies, clone, build
└── Makefile
```

---

## Ports

| Port | Protocol | Purpose |
|---|---|---|
| 5000 (default) | TCP | Chat connections |
| 5051 | UDP | Peer discovery beacons |

Make sure your firewall allows these if peers can't find or connect to each other.

---

## Notes

- Both peers must be on the same local network
- Nicknames cannot contain spaces (they are replaced with underscores)
- Passwords are 6 characters, A–Z and 0–9 only
- The session supports up to 7 peers plus the host

## NOTICE

Many parts of the project WERE made in fact with AI. I'm not saying that the project is pure AI but without it, the project would probably be worse in my opinion. Take that as you want, I'll continue using AI for this project until I have perfected my skills in C (<- will probably never happen).
