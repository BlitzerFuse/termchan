#include "tui.h"
#include "tui_internal.h"
#include "discovery.h"
#include <ncurses.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <net/if.h>
#include <ifaddrs.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/random.h>
#include <sys/select.h>
#include <unistd.h>

/* ── shared helpers ───────────────────────────────────────────────────────── */

static const char PASS_CHARS[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
#define PASS_LEN 6

void tui_get_local_ip(char *buf, size_t len) {
    strncpy(buf, "unavailable", len - 1);
    buf[len - 1] = '\0';
    struct ifaddrs *ifap, *ifa;
    if (getifaddrs(&ifap) < 0) return;
    for (ifa = ifap; ifa; ifa = ifa->ifa_next) {
        if (!ifa->ifa_addr || ifa->ifa_addr->sa_family != AF_INET) continue;
        if (ifa->ifa_flags & IFF_LOOPBACK)    continue;
        if (!(ifa->ifa_flags & IFF_UP))        continue;
        if (!(ifa->ifa_flags & IFF_BROADCAST)) continue;
        const char *ip = inet_ntoa(
            ((struct sockaddr_in *)ifa->ifa_addr)->sin_addr);
        strncpy(buf, ip, len - 1);
        buf[len - 1] = '\0';
        break;
    }
    freeifaddrs(ifap);
}

static void draw_section(WINDOW *w, int row, int bw, const char *label) {
    mvwaddch(w, row, 0,      ACS_LTEE);
    mvwhline(w, row, 1,      ACS_HLINE, bw - 2);
    mvwaddch(w, row, bw - 1, ACS_RTEE);
    if (label && label[0])
        mvwprintw(w, row, 2, " %s ", label);
}

/* ── field input ──────────────────────────────────────────────────────────────
 *
 * Reads text into buf (NUL-terminated, max bufsz-1 chars) at window position
 * (row, col) with display width maxw.  Works entirely with wgetch + noecho so
 * that KEY_UP/DOWN/TAB/ENTER can break out of the field for section navigation.
 *
 * Never call echo() before this — we handle our own display.
 *
 * Returns:
 *   FIELD_NEXT  — Enter, Down, or Tab  → advance to next section
 *   FIELD_PREV  — Up                   → go back to previous section
 *   FIELD_ABORT — Escape               → quit
 */
#define FIELD_NEXT  1
#define FIELD_PREV  2
#define FIELD_ABORT 3

static int read_field(WINDOW *w, int row, int col, int maxw,
                      char *buf, int bufsz) {
    int len = (int)strlen(buf);
    int cur = len;
    curs_set(1);

    for (;;) {
        /* Render field content padded to maxw so stale chars are erased. */
        mvwprintw(w, row, col, "%-*.*s", maxw, maxw, buf);
        wmove(w, row, col + cur);
        wrefresh(w);

        int ch = wgetch(w);
        switch (ch) {
            /* ── navigation out of field ── */
            case '\n': case '\r': case KEY_DOWN: case '\t':
                curs_set(0);
                return FIELD_NEXT;
            case KEY_UP:
                curs_set(0);
                return FIELD_PREV;
            case 27:   /* Escape */
                curs_set(0);
                return FIELD_ABORT;

            /* ── in-field editing ── */
            case KEY_BACKSPACE: case 127: case '\b':
                if (cur > 0) {
                    memmove(buf + cur - 1, buf + cur, (size_t)(len - cur + 1));
                    cur--; len--;
                }
                break;
            case KEY_DC:
                if (cur < len) {
                    memmove(buf + cur, buf + cur + 1, (size_t)(len - cur));
                    len--;
                }
                break;
            case KEY_LEFT:
                if (cur > 0) cur--;
                break;
            case KEY_RIGHT:
                if (cur < len) cur++;
                break;
            case KEY_HOME: case KEY_PPAGE:
                cur = 0;
                break;
            case KEY_END: case KEY_NPAGE:
                cur = len;
                break;
            default:
                if (ch >= 32 && ch <= 126 && len < bufsz - 1) {
                    memmove(buf + cur + 1, buf + cur, (size_t)(len - cur + 1));
                    buf[cur++] = (char)ch;
                    len++;
                }
                break;
        }
    }
}

/* ── tui_menu ─────────────────────────────────────────────────────────────────
 *
 * Layout (bw=58, bh=25):
 *
 *  row  0  ┌──────────────────────────────────────────────────────┐
 *  row  1  │                                                      │
 *  row  2  │                    term-chan                         │
 *  row  3  │             terminal chat over LAN                  │
 *  row  4  │                                                      │
 *  row  5  ├──────────────────────────────────────────────────────┤
 *  row  6  │                                                      │
 *  row  7  │    nickname  <field, col 14, width 38>               │
 *  row  8  │    port      <field, col 14, width 10>               │
 *  row  9  │                                                      │
 *  row 10  ├──────────────────────────────────────────────────────┤
 *  row 11  │                                                      │
 *  row 12  │        [ create room ]          [ connect ]          │
 *  row 13  │                                                      │
 *  row 14  ├──────────────────────────────────────────────────────┤
 *
 *  Dynamic section below row 14 (MODE_LISTEN):
 *  row 15  │                                                      │
 *  row 16  │    password protect this session?                    │
 *  row 17  │                                                      │
 *  row 18  │    [ none ]   [ auto-generate ]   [ set manually ]   │
 *  row 19  │                                                      │
 *  row 20  │    (result: "password: XXXXXX  (share this)")        │
 *  row 21  │    (result: "press enter to continue...")            │
 *  row 22  │                                                      │
 *  row 23  │                                                      │
 *  row 24  └──────────────────────────────────────────────────────┘
 *
 *  Dynamic section below row 14 (MODE_CONNECT):
 *  row 15  │                                                      │
 *  row 16  │    peer0              192.168.x.x                    │
 *  row 17  │    peer1  ← selected (reversed)                     │
 *  ...       up to 6 peers (rows 16-21)                          │
 *  row 22  │                                                      │
 *  row 23  │    tab  type IP   r  rescan   q  quit                │
 *  row 24  └──────────────────────────────────────────────────────┘
 */

#define MENU_BW 58
#define MENU_BH 25

/* Field positions */
#define NICK_ROW  7
#define NICK_COL  14
#define NICK_W    38

#define PORT_ROW  8
#define PORT_COL  14
#define PORT_W    10

/* Mode buttons (row 12) */
#define MODE_ROW      12
#define MODE_CREATE_C 12   /* col of "  create room  " */
#define MODE_CONN_C   35   /* col of "  connect  "     */

/* Peer list (MODE_CONNECT) */
#define PEER_ROWS  6
#define PEER_ROW0  16

/* Password section (MODE_LISTEN) */
#define PW_NONE_C   6
#define PW_AUTO_C   17
#define PW_MAN_C    37
#define PW_BTN_ROW  18

static void menu_draw_static(WINDOW *w, int bw) {
    werase(w);
    box(w, 0, 0);
    mvwprintw(w, 2, (bw - 9)  / 2, "term-chan");
    mvwprintw(w, 3, (bw - 22) / 2, "terminal chat over LAN");
    draw_section(w, 5,  bw, NULL);
    mvwprintw(w, 7, 4, "nickname");
    mvwprintw(w, 8, 4, "port    ");
    draw_section(w, 10, bw, NULL);
}

static void peer_list_draw(WINDOW *w, Peer *peers, int count, int sel) {
    for (int r = 15; r <= 23; r++) { wmove(w, r, 1); wclrtoeol(w); }

    if (count == 0) {
        mvwprintw(w, PEER_ROW0, 4, "(no peers yet -- waiting for beacons...)");
    } else {
        for (int i = 0; i < count && i < PEER_ROWS; i++) {
            if (i == sel) wattron(w, A_REVERSE);
            mvwprintw(w, PEER_ROW0 + i, 4,
                      "%-16.16s  %-15.15s",
                      peers[i].nickname, peers[i].ip);
            if (i == sel) wattroff(w, A_REVERSE);
        }
    }
    mvwprintw(w, 23, 4,
              count ? "tab  type IP   r  rescan   up  back   q  quit"
                    : "r  rescan   tab  type IP   up  back   q  quit");
}

int tui_menu(MenuResult *out) {
    ncurses_start();

    const int bw = MENU_BW, bh = MENU_BH;
    int bx = (COLS - bw) / 2, by = (LINES - bh) / 2;
    if (bx < 0) bx = 0;
    if (by < 0) by = 0;

    WINDOW *w = newwin(bh, bw, by, bx);
    /* keypad MUST stay TRUE throughout — enables KEY_UP/DOWN/LEFT/RIGHT etc.
       noecho MUST stay active — read_field handles its own display. */
    keypad(w, TRUE);
    noecho();
    curs_set(0);

    menu_draw_static(w, bw);
    /* Pre-fill nickname from saved config */
    if (out->nickname[0])
        mvwprintw(w, NICK_ROW, NICK_COL, "%.*s", NICK_W, out->nickname);
    /* Pre-fill port */
    char port_str[12];
    snprintf(port_str, sizeof(port_str), "%d", out->port > 0 ? out->port : 5000);
    mvwprintw(w, PORT_ROW, PORT_COL, "%s", port_str);
    wrefresh(w);

    /* ── State machine ────────────────────────────────────────────────────── */
    /* States (in top-to-bottom order):
     *   0 = NICK field
     *   1 = PORT field
     *   2 = MODE selector
     *   3 = PASSWORD selector  (create room path)
     *   4 = PEER LIST          (connect path)
     */
    int state = 0;

    while (1) {

        /* ── State 0: nickname ── */
        if (state == 0) {
            wattron(w, A_REVERSE);
            mvwprintw(w, NICK_ROW, NICK_COL - 1, ">");
            wattroff(w, A_REVERSE);
            wrefresh(w);

            int r = read_field(w, NICK_ROW, NICK_COL, NICK_W,
                               out->nickname, MAX_NAME);
            /* Remove the focus marker */
            mvwprintw(w, NICK_ROW, NICK_COL - 1, " ");

            if (r == FIELD_ABORT) goto abort;
            if (r == FIELD_PREV)  { /* already at top, stay */ continue; }
            /* FIELD_NEXT → validate then go to port */
            if (!out->nickname[0]) { state = 0; continue; }
            for (int i = 0; out->nickname[i]; i++)
                if (out->nickname[i] == ' ') out->nickname[i] = '_';
            state = 1;
            continue;
        }

        /* ── State 1: port ── */
        if (state == 1) {
            wattron(w, A_REVERSE);
            mvwprintw(w, PORT_ROW, PORT_COL - 1, ">");
            wattroff(w, A_REVERSE);
            wrefresh(w);

            int r = read_field(w, PORT_ROW, PORT_COL, PORT_W,
                               port_str, sizeof(port_str));
            mvwprintw(w, PORT_ROW, PORT_COL - 1, " ");

            if (r == FIELD_ABORT) goto abort;
            if (r == FIELD_PREV)  { state = 0; continue; }
            out->port = atoi(port_str);
            if (out->port <= 0 || out->port > 65535) {
                out->port = 5000;
                snprintf(port_str, sizeof(port_str), "5000");
                mvwprintw(w, PORT_ROW, PORT_COL, "%-*s", PORT_W, port_str);
            }
            /* Start discovery now that we have a nick */
            discovery_start(out->nickname, out->discovery_port);
            state = 2;
            continue;
        }

        /* ── State 2: mode selector ── */
        if (state == 2) {
            int mode_sel = (out->mode == MODE_CONNECT) ? 1 : 0;
            while (1) {
                if (mode_sel == 0) wattron(w, A_REVERSE);
                mvwprintw(w, MODE_ROW, MODE_CREATE_C, "  create room  ");
                if (mode_sel == 0) wattroff(w, A_REVERSE);
                if (mode_sel == 1) wattron(w, A_REVERSE);
                mvwprintw(w, MODE_ROW, MODE_CONN_C, "  connect  ");
                if (mode_sel == 1) wattroff(w, A_REVERSE);
                wrefresh(w);

                int ch = wgetch(w);
                switch (ch) {
                    case KEY_LEFT:  case 'h': mode_sel = 0; break;
                    case KEY_RIGHT: case 'l': mode_sel = 1; break;
                    case '\t':                mode_sel ^= 1; break;
                    case KEY_UP:
                        /* De-highlight both buttons, go back to port */
                        mvwprintw(w, MODE_ROW, MODE_CREATE_C, "  create room  ");
                        mvwprintw(w, MODE_ROW, MODE_CONN_C,   "  connect  ");
                        state = 1;
                        goto mode_break;
                    case '\n': case '\r': case KEY_DOWN:
                        out->mode = (mode_sel == 0) ? MODE_LISTEN : MODE_CONNECT;
                        /* Draw lower divider and clear dynamic section */
                        draw_section(w, 14, bw, NULL);
                        for (int r = 15; r <= 23; r++) {
                            wmove(w, r, 1); wclrtoeol(w);
                        }
                        wrefresh(w);
                        state = 3;
                        goto mode_break;
                    case KEY_RESIZE: case 27: case 'q':
                        goto abort;
                }
            }
mode_break:
            continue;
        }

        /* ── State 3: password (create room) or peer list (connect) ── */
        if (state == 3) {

            if (out->mode == MODE_LISTEN) {
                /* ── Password selector ── */
                mvwprintw(w, 16, 4, "password protect this session?");
                mvwprintw(w, 23, 4, "left/right  select   enter  confirm   up  back");
                int pw_sel = 0;
                while (1) {
                    if (pw_sel == 0) wattron(w, A_REVERSE);
                    mvwprintw(w, PW_BTN_ROW, PW_NONE_C, "[ none ]");
                    if (pw_sel == 0) wattroff(w, A_REVERSE);
                    if (pw_sel == 1) wattron(w, A_REVERSE);
                    mvwprintw(w, PW_BTN_ROW, PW_AUTO_C, "[ auto-generate ]");
                    if (pw_sel == 1) wattroff(w, A_REVERSE);
                    if (pw_sel == 2) wattron(w, A_REVERSE);
                    mvwprintw(w, PW_BTN_ROW, PW_MAN_C, "[ set manually ]");
                    if (pw_sel == 2) wattroff(w, A_REVERSE);
                    wrefresh(w);

                    int ch = wgetch(w);
                    switch (ch) {
                        case KEY_LEFT:  case 'h': if (pw_sel > 0) pw_sel--; break;
                        case KEY_RIGHT: case 'l': if (pw_sel < 2) pw_sel++; break;
                        case '\t':      pw_sel = (pw_sel + 1) % 3; break;
                        case KEY_UP:
                            /* Back to mode selector */
                            for (int r = 15; r <= 23; r++) {
                                wmove(w, r, 1); wclrtoeol(w);
                            }
                            /* Remove lower divider */
                            mvwhline(w, 14, 1, ' ', bw - 2);
                            mvwaddch(w, 14, 0,      ACS_VLINE);
                            mvwaddch(w, 14, bw - 1, ACS_VLINE);
                            state = 2;
                            goto pw_break;
                        case '\n': case '\r':
                            goto pw_chosen;
                        case KEY_RESIZE: case 27: case 'q':
                            goto abort;
                    }
                }
pw_chosen:
                if (pw_sel == 0) {
                    out->password[0] = '\0';
                } else if (pw_sel == 1) {
                    unsigned char rnd[PASS_LEN];
                    if (getrandom(rnd, sizeof(rnd), 0) != (ssize_t)sizeof(rnd)) {
                        FILE *uf = fopen("/dev/urandom", "rb");
                        if (uf) { (void)fread(rnd, 1, sizeof(rnd), uf); fclose(uf); }
                    }
                    for (int i = 0; i < PASS_LEN; i++)
                        out->password[i] = PASS_CHARS[rnd[i] % (sizeof(PASS_CHARS) - 1)];
                    out->password[PASS_LEN] = '\0';
                    mvwprintw(w, 20, 4, "password: %s  (share this)", out->password);
                    mvwprintw(w, 21, 4, "press enter to continue...");
                    wrefresh(w);
                    /* Wait for any key */
                    wgetch(w);
                } else {
                    /* Manual password input */
                    mvwprintw(w, 20, 4, "enter password (A-Z 0-9, up to 6 chars):");
                    for (int r = 21; r <= 22; r++) { wmove(w, r, 1); wclrtoeol(w); }
                    wrefresh(w);
                    char tmp[MAX_PASS + 1];
                    memset(tmp, 0, sizeof(tmp));
                    /* Use read_field — arrows in the pw field go nowhere special */
                    read_field(w, 21, 4, PASS_LEN + 2, tmp, MAX_PASS + 1);
                    for (int i = 0; tmp[i]; i++)
                        out->password[i] = (tmp[i] >= 'a' && tmp[i] <= 'z')
                                            ? tmp[i] - 32 : tmp[i];
                    out->password[MAX_PASS - 1] = '\0';
                }
pw_break:
                /* fall through to done on next iteration if state == 3 still */
                if (state == 3) goto connect_done;
                continue;

            } else {
                /* ── Peer list (MODE_CONNECT) ── */
                Peer peers[MAX_PEERS];
                int  count    = 0;
                int  peer_sel = 0;

                wtimeout(w, 200);
                while (1) {
                    count = discovery_peers(peers, MAX_PEERS);
                    if (peer_sel >= count) peer_sel = count > 0 ? count - 1 : 0;
                    peer_list_draw(w, peers, count, peer_sel);
                    wrefresh(w);

                    int ch = wgetch(w);
                    switch (ch) {
                        case KEY_UP:
                            if (peer_sel > 0) {
                                peer_sel--;
                            } else {
                                /* Already at top of list — go back to mode */
                                wtimeout(w, -1);
                                for (int r = 15; r <= 23; r++) {
                                    wmove(w, r, 1); wclrtoeol(w);
                                }
                                mvwhline(w, 14, 1, ' ', bw - 2);
                                mvwaddch(w, 14, 0,      ACS_VLINE);
                                mvwaddch(w, 14, bw - 1, ACS_VLINE);
                                state = 2;
                                goto peer_break;
                            }
                            break;
                        case KEY_DOWN:
                            if (peer_sel < count - 1) peer_sel++;
                            break;
                        case '\n': case '\r':
                            if (count > 0) {
                                strncpy(out->peer_ip, peers[peer_sel].ip,
                                        sizeof(out->peer_ip) - 1);
                                wtimeout(w, -1);
                                goto connect_done;
                            }
                            break;
                        case 'r': case 'R':
                            discovery_reset();
                            peer_sel = 0;
                            break;
                        case '\t': case 'i':
                            wtimeout(w, -1);
                            goto manual_ip;
                        case KEY_RESIZE:
                        case 'q': case 27:
                            wtimeout(w, -1);
                            goto abort;
                    }
                }
peer_break:
                continue;

manual_ip:
                /* Manual IP entry */
                for (int r = 15; r <= 23; r++) { wmove(w, r, 1); wclrtoeol(w); }
                draw_section(w, 14, bw, " type IP manually ");
                mvwprintw(w, 17, 4, "peer ip");
                wrefresh(w);
                memset(out->peer_ip, 0, sizeof(out->peer_ip));
                int r = read_field(w, 17, 14, 40, out->peer_ip,
                                   (int)sizeof(out->peer_ip));
                if (r == FIELD_ABORT || !out->peer_ip[0]) goto abort;
                goto connect_done;
            }
        }
    } /* end while(1) */

connect_done:
    delwin(w);
    return 0;

abort:
    discovery_stop();
    delwin(w);
    endwin();
    return -1;
}

/* ── tui_accept_request ───────────────────────────────────────────────────── */

int tui_accept_request(const char *peer_nick, const char *peer_ip) {
    clear(); refresh();
    const int bw = 50, bh = 10;
    int bx = (COLS - bw) / 2, by = (LINES - bh) / 2;
    if (bx < 0) bx = 0;
    if (by < 0) by = 0;
    WINDOW *w = newwin(bh, bw, by, bx);
    keypad(w, TRUE);
    noecho();
    curs_set(0);
    int sel = 1;
    while (1) {
        werase(w); box(w, 0, 0);
        mvwprintw(w, 1, (bw - 19) / 2, "incoming connection");
        draw_section(w, 2, bw, NULL);
        mvwprintw(w, 3, 4, "user   %s", peer_nick);
        mvwprintw(w, 4, 4, "ip     %s", peer_ip);
        draw_section(w, 5, bw, NULL);
        mvwprintw(w, 6, 4, "accept this connection?");
        if (sel == 0) wattron(w, A_REVERSE);
        mvwprintw(w, 7, 10, "  reject  ");
        if (sel == 0) wattroff(w, A_REVERSE);
        if (sel == 1) wattron(w, A_REVERSE);
        mvwprintw(w, 7, 30, "  accept  ");
        if (sel == 1) wattroff(w, A_REVERSE);
        wrefresh(w);
        int ch = wgetch(w);
        switch (ch) {
            case KEY_LEFT: case KEY_UP:    sel = 0; break;
            case KEY_RIGHT: case KEY_DOWN: sel = 1; break;
            case '\t': case 'h': case 'l': sel ^= 1; break;
            case '\n': case '\r':          goto done;
            case 'y': case 'Y':            sel = 1; goto done;
            case 'n': case 'N':            sel = 0; goto done;
        }
    }
done:
    delwin(w); clear(); refresh();
    return sel;
}

/* ── tui_enter_password ───────────────────────────────────────────────────── */

const char *tui_enter_password(const char *peer_nick, const char *peer_ip) {
    static char entered[MAX_PASS + 1];
    memset(entered, 0, sizeof(entered));
    clear(); refresh();
    const int bw = 50, bh = 9;
    int bx = (COLS - bw) / 2, by = (LINES - bh) / 2;
    if (bx < 0) bx = 0;
    if (by < 0) by = 0;
    WINDOW *w = newwin(bh, bw, by, bx);
    keypad(w, TRUE);
    noecho();
    curs_set(0);
    werase(w); box(w, 0, 0);
    mvwprintw(w, 1, (bw - 17) / 2, "password required");
    draw_section(w, 2, bw, NULL);
    mvwprintw(w, 3, 4, "host   %s", peer_nick);
    mvwprintw(w, 4, 4, "ip     %s", peer_ip);
    draw_section(w, 5, bw, NULL);
    mvwprintw(w, 6, 4, "password");
    wrefresh(w);
    read_field(w, 6, 13, PASS_LEN + 2, entered, MAX_PASS + 1);
    for (int i = 0; entered[i]; i++)
        if (entered[i] >= 'a' && entered[i] <= 'z') entered[i] -= 32;
    delwin(w); clear(); refresh();
    return entered;
}

/* ── tui_waiting_run ──────────────────────────────────────────────────────── */

#define WAIT_BW 58
#define WAIT_BH 26
#define WP_MAX  (MAX_CLIENTS + 1)
#define WP_ROW0 13

static void draw_waiting_peers(WINDOW *w,
                                char nicks[][MAX_NAME],
                                int  is_host[],
                                int  is_me[],
                                int  count) {
    for (int r = WP_ROW0; r <= WP_ROW0 + WP_MAX - 1; r++) {
        wmove(w, r, 1); wclrtoeol(w);
    }
    for (int i = 0; i < count && i < WP_MAX; i++) {
        const char *ann = is_host[i] ? "host" : (is_me[i] ? "you" : "");
        mvwprintw(w, WP_ROW0 + i, 4, "%-16.16s  %s", nicks[i], ann);
    }
    wrefresh(w);
}

int tui_waiting_run(int sock, const char *host_nick,
                    const char *host_ip, int port,
                    const char *my_nick) {
    ncurses_start();

    const int bw = WAIT_BW, bh = WAIT_BH;
    int bx = (COLS - bw) / 2, by = (LINES - bh) / 2;
    if (bx < 0) bx = 0;
    if (by < 0) by = 0;

    WINDOW *w = newwin(bh, bw, by, bx);
    keypad(w, TRUE);
    noecho();
    curs_set(0);
    nodelay(w, TRUE);

    werase(w); box(w, 0, 0);
    mvwprintw(w, 2, (bw - 9)  / 2, "term-chan");
    mvwprintw(w, 3, (bw - 16) / 2, "waiting for host");
    draw_section(w, 5, bw, NULL);
    mvwprintw(w, 7,  4, "connected to    %s", host_nick);
    mvwprintw(w, 8,  4, "address         %s:%d", host_ip, port);
    mvwprintw(w, 9,  4, "your nickname   %s", my_nick);
    draw_section(w, 11, bw, " in the room ");
    mvwprintw(w, 22, 4, "waiting for host to start the session...");
    draw_section(w, 23, bw, NULL);
    mvwprintw(w, 24, 4, "q  quit");
    wrefresh(w);

    char wp_nicks[WP_MAX][MAX_NAME];
    int  wp_is_host[WP_MAX];
    int  wp_is_me[WP_MAX];
    int  wp_count = 0;
    memset(wp_nicks,   0, sizeof(wp_nicks));
    memset(wp_is_host, 0, sizeof(wp_is_host));
    memset(wp_is_me,   0, sizeof(wp_is_me));

    strncpy(wp_nicks[0], host_nick, MAX_NAME - 1);
    wp_is_host[0] = 1;
    strncpy(wp_nicks[1], my_nick,   MAX_NAME - 1);
    wp_is_me[1]   = 1;
    wp_count = 2;
    draw_waiting_peers(w, wp_nicks, wp_is_host, wp_is_me, wp_count);

    while (1) {
        int ch = wgetch(w);
        if (ch == 'q' || ch == 27) {
            delwin(w); endwin(); return -1;
        }

        fd_set rfds;
        struct timeval tv = {0, 100000};
        FD_ZERO(&rfds);
        FD_SET(sock, &rfds);
        if (select(sock + 1, &rfds, NULL, NULL, &tv) <= 0)
            continue;

        Packet p;
        if (recv(sock, &p, sizeof(Packet), 0) <= 0) {
            delwin(w); endwin(); return -1;
        }

        if (p.type == CHAT_START) {
            delwin(w);
            return 0;
        }

        if (p.type == ROSTER_SYNC) {
            wp_count = 0;
            char buf[MAX_MSG];
            strncpy(buf, p.content, MAX_MSG - 1);
            buf[MAX_MSG - 1] = '\0';
            char *tok = strtok(buf, "\n");
            while (tok && wp_count < WP_MAX) {
                strncpy(wp_nicks[wp_count], tok, MAX_NAME - 1);
                wp_nicks[wp_count][MAX_NAME - 1] = '\0';
                wp_is_host[wp_count] = (strcmp(tok, p.sender) == 0);
                wp_is_me[wp_count]   = (strcmp(tok, my_nick)  == 0);
                wp_count++;
                tok = strtok(NULL, "\n");
            }
            draw_waiting_peers(w, wp_nicks, wp_is_host, wp_is_me, wp_count);
        }

        if (p.type == PEER_JOIN && wp_count < WP_MAX) {
            strncpy(wp_nicks[wp_count], p.sender, MAX_NAME - 1);
            wp_nicks[wp_count][MAX_NAME - 1] = '\0';
            wp_is_host[wp_count] = 0;
            wp_is_me[wp_count]   = 0;
            wp_count++;
            draw_waiting_peers(w, wp_nicks, wp_is_host, wp_is_me, wp_count);
        }
    }
}
