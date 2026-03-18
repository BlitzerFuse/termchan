#include "chat.h"
#include "network.h"
#include "session.h"
#include "room.h"
#include "tui.h"
#include "discovery.h"
#include "protocol.h"
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
#include <ncurses.h>
#include <string.h>

#define DEFAULT_PORT 5000

int main(void) {
    MenuResult menu = {0};
    if (tui_menu(&menu) < 0)
        return 0;

    /* Discovery beacon no longer needed once we have an IP to connect to */
    discovery_stop();

    Session s = {0};
    strncpy(s.my_nick, menu.nickname, MAX_NAME - 1);

    if (menu.mode == MODE_LISTEN) {
        s.is_host = 1;

        int listener = init_listener(DEFAULT_PORT);
        if (listener < 0) { endwin(); return 1; }

        while (s.count == 0) {
            tui_waiting(DEFAULT_PORT, menu.password[0] ? menu.password : NULL);

            fd_set fds;
            struct timeval tv = { .tv_sec = 0, .tv_usec = 300000 };
            FD_ZERO(&fds); FD_SET(listener, &fds);
            if (select(listener + 1, &fds, NULL, NULL, &tv) <= 0) continue;

            char peer_ip[64]         = {0};
            char peer_nick[MAX_NAME] = {0};
            char peer_pass[MAX_PASS] = {0};
            int conn = accept_connection(listener, peer_ip, peer_nick, peer_pass);
            if (conn < 0) continue;

            if (menu.password[0] &&
                strcmp(peer_pass, menu.password) != 0) {
                send_conn_wrong_pass(conn);
                continue;
            }
            if (!tui_accept_request(peer_nick, peer_ip)) {
                send_conn_reject(conn);
                continue;
            }
            send_conn_accept(conn, menu.nickname);
            room_add(&s, conn, peer_nick);

            Packet join = { .type = PEER_JOIN };
            strncpy(join.sender, peer_nick, MAX_NAME - 1);
            snprintf(join.content, MAX_MSG - 1, "%s joined the room.", peer_nick);
            room_broadcast(&s, &join, conn);
        }

        if (tui_lobby(&s, listener, menu.password[0] ? menu.password : NULL) < 0) {
            close(listener);
            return 0;
        }
        close(listener);

        Packet start = { .type = CHAT_START };
        room_broadcast(&s, &start, -1);

    } else {
        s.is_host = 0;
        char host_nick[MAX_NAME] = {0};

        while (1) {
            const char *pass = tui_enter_password(
                menu.peer_ip[0] ? menu.peer_ip : "peer", menu.peer_ip);
            int fd = connect_to_peer(menu.peer_ip, DEFAULT_PORT,
                                     menu.nickname, pass, host_nick);
            if (fd == NET_ERR_WRONGPASS) {
                clear();
                mvprintw(LINES/2, (COLS-40)/2, "Wrong password. Press any key to retry.");
                refresh(); getch(); continue;
            }
            if (fd == NET_ERR_REJECTED) {
                clear();
                mvprintw(LINES/2, (COLS-40)/2, "Rejected. Press any key to retry.");
                refresh(); getch(); continue;
            }
            if (fd < 0) { endwin(); return 1; }

            room_add(&s, fd, host_nick[0] ? host_nick : menu.peer_ip);
            break;
        }

        tui_waiting_for_start();
        Packet p;
        while (recv(s.fds[0], &p, sizeof(Packet), 0) > 0)
            if (p.type == CHAT_START) break;
    }

    tui_init(menu.nickname);
    start_chat(&s, tui_display_message);
    tui_shutdown();
    return 0;
}
