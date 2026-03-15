#include "chat.h"
#include "network.h"
#include "tui.h"
#include <pthread.h>
#include <unistd.h>
#include <ncurses.h>

#define DEFAULT_PORT 5000

int main(void) {
    MenuResult menu;
    if (tui_menu(&menu) < 0)
        return 0;

    int sock_fd;

    if (menu.mode == MODE_LISTEN) {
        int listener = init_listener(DEFAULT_PORT);
        if (listener < 0) {
            endwin();
            return 1;
        }

        tui_waiting(DEFAULT_PORT);

        char peer_ip[64] = {0};
        sock_fd = accept_connection(listener, peer_ip);
        close(listener);
        if (sock_fd < 0) { endwin(); return 1; }

    } else {
        sock_fd = connect_to_peer(menu.peer_ip, DEFAULT_PORT);
        if (sock_fd < 0) { endwin(); return 1; }
    }

    tui_init(menu.nickname);
    start_chat(sock_fd, menu.nickname, tui_display_message);
    tui_shutdown();
    return 0;
}
