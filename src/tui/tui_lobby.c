#include "tui.h"
#include "tui_internal.h"
#include "session.h"
#include "network.h"
#include "room.h"
#include "protocol.h"
#include <ncurses.h>
#include <sys/select.h>
#include <string.h>
#include <stdio.h>

#define LOBBY_W 54
#define LOBBY_H 20

static void draw_lobby(WINDOW *w, Session *s) {
    werase(w);
    box(w, 0, 0);
    mvwprintw(w, 1, (LOBBY_W - 12) / 2, "Room Lobby");
    mvwhline(w, 2, 1, ACS_HLINE, LOBBY_W - 2);
    mvwprintw(w, 3, 2, "Host: %s", s->my_nick);
    mvwprintw(w, 4, 2, "Peers (%d/%d):", s->count, MAX_CLIENTS);
    for (int i = 0; i < s->count && i < MAX_CLIENTS; i++)
        mvwprintw(w, 5 + i, 4, "  %s", s->nicks[i]);
    mvwhline(w, 16, 1, ACS_HLINE, LOBBY_W - 2);
    mvwprintw(w, 17, 2, "Enter = start chat    q = quit");
    wrefresh(w);
}

int tui_lobby(Session *s, int listener_fd, const char *password) {
    ncurses_start();
    int bx = (COLS - LOBBY_W) / 2;
    int by = (LINES - LOBBY_H) / 2;
    if (bx < 0) bx = 0;
    if (by < 0) by = 0;

    WINDOW *w = newwin(LOBBY_H, LOBBY_W, by, bx);
    keypad(w, TRUE);
    nodelay(w, TRUE);

    while (1) {
        int ch = wgetch(w);
        if (ch == '\n' || ch == '\r') break;
        if (ch == 'q'  || ch == 27)  { delwin(w); endwin(); return -1; }

        /* Check for incoming connections without blocking */
        fd_set rfds;
        struct timeval tv = { .tv_sec = 0, .tv_usec = 300000 };
        FD_ZERO(&rfds);
        FD_SET(listener_fd, &rfds);
        if (select(listener_fd + 1, &rfds, NULL, NULL, &tv) > 0) {
            char peer_ip[64]         = {0};
            char peer_nick[MAX_NAME] = {0};
            char peer_pass[MAX_PASS] = {0};
            int conn = accept_connection(listener_fd, peer_ip, peer_nick, peer_pass);
            if (conn >= 0) {
                if (password && password[0] &&
                    strcmp(peer_pass, password) != 0) {
                    send_conn_wrong_pass(conn);
                } else if (s->count >= MAX_CLIENTS) {
                    send_conn_reject(conn);
                } else if (tui_accept_request(peer_nick, peer_ip)) {
                    send_conn_accept(conn, s->my_nick);
                    room_add(s, conn, peer_nick);
                    Packet join = { .type = PEER_JOIN };
                    strncpy(join.sender, peer_nick, MAX_NAME - 1);
                    snprintf(join.content, MAX_MSG - 1, "%s joined the room.", peer_nick);
                    room_broadcast(s, &join, conn);
                } else {
                    send_conn_reject(conn);
                }
            }
        }

        draw_lobby(w, s);
    }

    delwin(w);
    return 0;
}
