#include "tui.h"
#include "tui_internal.h"
#include "discovery.h"
#include <ncurses.h>
#include <pthread.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static const char PASS_CHARS[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
#define PASS_LEN 6

void tui_get_local_ip(char *buf, size_t len) {
    strncpy(buf, "unavailable", len - 1);
    buf[len - 1] = '\0';
    FILE *f = popen("hostname -i 2>/dev/null", "r");
    if (!f) return;
    char tmp[128] = {0};
    if (fgets(tmp, sizeof(tmp), f)) {
        char *tok = strtok(tmp, " \t\n");
        if (tok) { strncpy(buf, tok, len - 1); buf[len - 1] = '\0'; }
    }
    pclose(f);
}

static void draw_peer_list(WINDOW *w, int bw, Peer *peers, int count, int sel) {
    for (int r = 10; r < 18; r++) { wmove(w, r, 1); wclrtoeol(w); }
    if (count == 0)
        mvwprintw(w, 10, 2, "(no peers yet — waiting for beacons...)");
    else {
        mvwprintw(w, 10, 2, "Peers on network:");
        for (int i = 0; i < count && i < 6; i++) {
            if (i == sel) wattron(w, A_REVERSE);
            mvwprintw(w, 11 + i, 3, " %-20s %s ", peers[i].nickname, peers[i].ip);
            if (i == sel) wattroff(w, A_REVERSE);
        }
    }
    mvwprintw(w, 17, 2, "%-*s", bw - 4,
              count ? "Enter=select  r=rescan  Tab/i=type IP  q=quit"
                    : "r=rescan  Tab/i=type IP  q=quit");
}

int tui_menu(MenuResult *out) {
    ncurses_start();

    char local_ip[64];
    tui_get_local_ip(local_ip, sizeof(local_ip));

    const int bw = 56, bh = 22;
    int bx = (COLS - bw) / 2, by = (LINES - bh) / 2;
    if (by < 0) by = 0;
    if (bx < 0) bx = 0;

    WINDOW *w = newwin(bh, bw, by, bx);
    keypad(w, TRUE);

    werase(w);
    box(w, 0, 0);
    mvwprintw(w, 1, (bw - 8) / 2, "Term-chan");
    mvwhline(w, 2, 1, ACS_HLINE, bw - 2);
    mvwprintw(w, 3, 2, "Your IP : %s", local_ip);
    mvwprintw(w, 4, 2, "Port    : 5000");
    mvwhline(w, 5, 1, ACS_HLINE, bw - 2);
    mvwprintw(w, 6, 2, "Nickname: ");
    wrefresh(w);

    echo(); curs_set(1);
    wmove(w, 6, 12);
    wgetnstr(w, out->nickname, MAX_NAME - 1);
    noecho(); curs_set(0);
    if (!out->nickname[0]) goto abort;

    for (int i = 0; out->nickname[i]; i++)
        if (out->nickname[i] == ' ') out->nickname[i] = '_';

    /* Start beacon immediately after nickname is known — both modes */
    discovery_start(out->nickname);

    mvwhline(w, 7, 1, ACS_HLINE, bw - 2);
    mvwprintw(w, 8, 2, "Mode (arrows + Enter):");

    int mode_sel = 0;
    while (1) {
        if (mode_sel == 0) wattron(w, A_REVERSE);
        mvwprintw(w, 9, 4, " Listen (wait) ");
        if (mode_sel == 0) wattroff(w, A_REVERSE);
        if (mode_sel == 1) wattron(w, A_REVERSE);
        mvwprintw(w, 9, 21, " Connect       ");
        if (mode_sel == 1) wattroff(w, A_REVERSE);
        wrefresh(w);
        int ch = wgetch(w);
        switch (ch) {
            case KEY_LEFT:  case KEY_UP:    mode_sel = 0; break;
            case KEY_RIGHT: case KEY_DOWN:  mode_sel = 1; break;
            case '\t':                      mode_sel ^= 1; break;
            case '\n': case '\r':           goto mode_done;
            case KEY_RESIZE:                goto abort;
            case 'q':  case 27:             goto abort;
        }
    }
mode_done:
    out->mode = (mode_sel == 0) ? MODE_LISTEN : MODE_CONNECT;

    if (out->mode == MODE_LISTEN) {
        mvwhline(w, 10, 1, ACS_HLINE, bw - 2);
        mvwprintw(w, 11, 2, "Password protect this session?");

        int pw_sel = 0;
        while (1) {
            if (pw_sel == 0) wattron(w, A_REVERSE);
            mvwprintw(w, 12, 4, " None  ");
            if (pw_sel == 0) wattroff(w, A_REVERSE);
            if (pw_sel == 1) wattron(w, A_REVERSE);
            mvwprintw(w, 12, 13, " Auto-generate ");
            if (pw_sel == 1) wattroff(w, A_REVERSE);
            if (pw_sel == 2) wattron(w, A_REVERSE);
            mvwprintw(w, 12, 30, " Set manually ");
            if (pw_sel == 2) wattroff(w, A_REVERSE);
            wrefresh(w);
            int ch = wgetch(w);
            switch (ch) {
                case KEY_LEFT:  if (pw_sel > 0) pw_sel--; break;
                case KEY_RIGHT: if (pw_sel < 2) pw_sel++; break;
                case '\t':      pw_sel = (pw_sel + 1) % 3; break;
                case '\n': case '\r': goto pw_done;
                case KEY_RESIZE: goto abort;
                case 'q': case 27: goto abort;
            }
        }
pw_done:
        if (pw_sel == 0) {
            out->password[0] = '\0';
        } else if (pw_sel == 1) {
            srand((unsigned)time(NULL));
            for (int i = 0; i < PASS_LEN; i++)
                out->password[i] = PASS_CHARS[rand() % (sizeof(PASS_CHARS) - 1)];
            out->password[PASS_LEN] = '\0';
            mvwprintw(w, 13, 2, "Password: %s  (share this)", out->password);
            mvwprintw(w, 14, 2, "Press Enter to continue...");
            wrefresh(w);
            wgetch(w);
        } else {
            mvwprintw(w, 13, 2, "Enter password (6 chars, A-Z 0-9): ");
            echo(); curs_set(1);
            wmove(w, 13, 38);
            char tmp[MAX_PASS] = {0};
            wgetnstr(w, tmp, PASS_LEN);
            noecho(); curs_set(0);
            for (int i = 0; tmp[i]; i++)
                out->password[i] = (tmp[i] >= 'a' && tmp[i] <= 'z') ? tmp[i] - 32 : tmp[i];
            out->password[PASS_LEN] = '\0';
        }
    }

    if (out->mode == MODE_CONNECT) {
        mvwhline(w, 10, 1, ACS_HLINE, bw - 2);
        mvwhline(w, 18, 1, ACS_HLINE, bw - 2);
        const char *label = " or type IP manually ";
        mvwprintw(w, 18, (bw - (int)strlen(label)) / 2, "%s", label);
        mvwprintw(w, 19, 2, "Peer IP : ");

        Peer  peers[MAX_PEERS];
        int   count   = 0;
        int   peer_sel = 0;

        wtimeout(w, 300);   /* refresh peer list every 300ms */
        while (1) {
            count = discovery_peers(peers, MAX_PEERS);
            if (peer_sel >= count) peer_sel = count > 0 ? count - 1 : 0;
            draw_peer_list(w, bw, peers, count, peer_sel);
            wrefresh(w);

            int ch = wgetch(w);
            switch (ch) {
                case KEY_UP:
                    if (peer_sel > 0) peer_sel--;
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

manual_ip:
        wmove(w, 19, 12);
        wclrtoeol(w);
        wrefresh(w);
        echo(); curs_set(1);
        wgetnstr(w, out->peer_ip, (int)sizeof(out->peer_ip) - 1);
        noecho(); curs_set(0);
        if (!out->peer_ip[0]) goto abort;
    }

connect_done:
    delwin(w);
    return 0;

abort:
    discovery_stop();
    delwin(w);
    endwin();
    return -1;
}

void tui_waiting(int port, const char *password) {
    clear();
    int cy = LINES / 2;
    int cx = (COLS - 40) / 2;
    if (cx < 0) cx = 0;
    const int w = 38;
    mvaddch(cy-1, cx, ACS_ULCORNER); mvhline(cy-1, cx+1, ACS_HLINE, w); mvaddch(cy-1, cx+w+1, ACS_URCORNER);
    mvaddch(cy, cx, ACS_VLINE);
    char line1[40];
    snprintf(line1, sizeof(line1), "  Waiting for connection on port %-4d", port);
    mvprintw(cy, cx+1, "%-*s", w, line1);
    mvaddch(cy, cx+w+1, ACS_VLINE);
    mvaddch(cy+1, cx, ACS_VLINE);
    if (password && password[0]) {
        char pw_line[40];
        snprintf(pw_line, sizeof(pw_line), "  Password: %s", password);
        mvprintw(cy+1, cx+1, "%-*s", w, pw_line);
    } else {
        mvprintw(cy+1, cx+1, "%-*s", w, "  No password set");
    }
    mvaddch(cy+1, cx+w+1, ACS_VLINE);
    mvaddch(cy+2, cx, ACS_VLINE);
    mvprintw(cy+2, cx+1, "%-*s", w, "  Press Ctrl-C to cancel");
    mvaddch(cy+2, cx+w+1, ACS_VLINE);
    mvaddch(cy+3, cx, ACS_LLCORNER); mvhline(cy+3, cx+1, ACS_HLINE, w); mvaddch(cy+3, cx+w+1, ACS_LRCORNER);
    refresh();
}

void tui_waiting_for_start(void) {
    clear();
    int cy = LINES / 2;
    int cx = (COLS - 40) / 2;
    if (cx < 0) cx = 0;
    const int w = 38;
    mvaddch(cy-1, cx, ACS_ULCORNER); mvhline(cy-1, cx+1, ACS_HLINE, w); mvaddch(cy-1, cx+w+1, ACS_URCORNER);
    mvaddch(cy, cx, ACS_VLINE);
    mvprintw(cy, cx+1, "%-*s", w, "  Connected! Waiting for host...");
    mvaddch(cy, cx+w+1, ACS_VLINE);
    mvaddch(cy+1, cx, ACS_VLINE);
    mvprintw(cy+1, cx+1, "%-*s", w, "  Chat will begin shortly.");
    mvaddch(cy+1, cx+w+1, ACS_VLINE);
    mvaddch(cy+2, cx, ACS_LLCORNER); mvhline(cy+2, cx+1, ACS_HLINE, w); mvaddch(cy+2, cx+w+1, ACS_LRCORNER);
    refresh();
}

int tui_accept_request(const char *peer_nick, const char *peer_ip) {
    clear(); refresh();
    const int bw = 50, bh = 10;
    int bx = (COLS - bw) / 2, by = (LINES - bh) / 2;
    if (bx < 0) bx = 0;
    if (by < 0) by = 0;
    WINDOW *w = newwin(bh, bw, by, bx);
    keypad(w, TRUE);
    int sel = 1;
    while (1) {
        werase(w); box(w, 0, 0);
        mvwprintw(w, 1, (bw-20)/2, "Incoming Connection");
        mvwhline(w, 2, 1, ACS_HLINE, bw-2);
        mvwprintw(w, 3, 2, "User : %s", peer_nick);
        mvwprintw(w, 4, 2, "IP   : %s", peer_ip);
        mvwhline(w, 5, 1, ACS_HLINE, bw-2);
        mvwprintw(w, 6, 2, "Accept this connection?");
        if (sel == 0) wattron(w, A_REVERSE);
        mvwprintw(w, 7, 8,  " Reject ");
        if (sel == 0) wattroff(w, A_REVERSE);
        if (sel == 1) wattron(w, A_REVERSE);
        mvwprintw(w, 7, 32, " Accept ");
        if (sel == 1) wattroff(w, A_REVERSE);
        wrefresh(w);
        int ch = wgetch(w);
        switch (ch) {
            case KEY_LEFT: case KEY_UP:    sel = 0; break;
            case KEY_RIGHT: case KEY_DOWN: sel = 1; break;
            case '\t':                     sel ^= 1; break;
            case '\n': case '\r':          goto done;
            case 'y': case 'Y':            sel = 1; goto done;
            case 'n': case 'N':            sel = 0; goto done;
        }
    }
done:
    delwin(w); clear(); refresh();
    return sel;
}

const char *tui_enter_password(const char *peer_nick, const char *peer_ip) {
    static char entered[MAX_PASS];
    memset(entered, 0, sizeof(entered));
    clear(); refresh();
    const int bw = 50, bh = 9;
    int bx = (COLS - bw) / 2, by = (LINES - bh) / 2;
    if (bx < 0) bx = 0;
    if (by < 0) by = 0;
    WINDOW *w = newwin(bh, bw, by, bx);
    keypad(w, TRUE);
    werase(w); box(w, 0, 0);
    mvwprintw(w, 1, (bw-22)/2, "Password Required");
    mvwhline(w, 2, 1, ACS_HLINE, bw-2);
    mvwprintw(w, 3, 2, "User : %s", peer_nick);
    mvwprintw(w, 4, 2, "IP   : %s", peer_ip);
    mvwhline(w, 5, 1, ACS_HLINE, bw-2);
    mvwprintw(w, 6, 2, "Password: ");
    wrefresh(w);
    echo(); curs_set(1);
    wmove(w, 6, 12);
    wgetnstr(w, entered, PASS_LEN);
    noecho(); curs_set(0);
    for (int i = 0; entered[i]; i++)
        if (entered[i] >= 'a' && entered[i] <= 'z') entered[i] -= 32;
    delwin(w); clear(); refresh();
    return entered;
}
