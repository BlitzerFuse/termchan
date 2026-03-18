#include "chat.h"
#include "room.h"
#include "commands.h"
#include "tui.h"
#include <pthread.h>
#include <stdatomic.h>
#include <string.h>
#include <stdio.h>
#include <sys/socket.h>
#include <stdlib.h>

typedef struct {
    Session    *s;
    int         fd;
    void      (*display_cb)(Packet *);
    atomic_int *running;
} PeerArgs;

static void *peer_recv_thread(void *arg) {
    PeerArgs *a = arg;
    Packet p;

    while (atomic_load(a->running)) {
        if (recv(a->fd, &p, sizeof(Packet), 0) <= 0) {
            char nick[MAX_NAME];
            room_nick_for_fd(a->s, a->fd, nick, sizeof(nick));
            room_remove(a->s, a->fd);

            Packet leave = { .type = PEER_LEAVE };
            strncpy(leave.sender, nick, MAX_NAME - 1);
            snprintf(leave.content, MAX_MSG - 1, "%s left the chat.", nick);
            if (a->s->is_host)
                room_broadcast(a->s, &leave, -1);
            if (a->display_cb) a->display_cb(&leave);
            break;
        }

        if (a->s->is_host) {
            strncpy(p.target, "everyone", MAX_NAME - 1);
            room_broadcast(a->s, &p, a->fd);
        }
        if (a->display_cb) a->display_cb(&p);
    }

    free(a);
    return NULL;
}

void start_chat(Session *s, void (*display_cb)(Packet *)) {
    atomic_int running;
    atomic_init(&running, 1);

    pthread_t tids[MAX_CLIENTS] = {0};
    int n_tids = 0;

    for (int i = 0; i < s->count; i++) {
        PeerArgs *a = malloc(sizeof(PeerArgs));
        a->s = s; a->fd = s->fds[i];
        a->display_cb = display_cb; a->running = &running;
        pthread_create(&tids[n_tids++], NULL, peer_recv_thread, a);
    }

    while (atomic_load(&running)) {
        char *input = tui_get_input();
        if (!input) break;
        if (input[0] == '\0') { free(input); continue; }

        if (input[0] == '/') {
            CmdResult r = cmd_dispatch(input, s->my_nick, s);
            free(input);
            if (r == CMD_QUIT) break;
            continue;
        }

        Packet out = { .type = MSG };
        strncpy(out.sender, s->my_nick, MAX_NAME - 1);
        strncpy(out.target, "everyone", MAX_NAME - 1);
        strncpy(out.content, input, MAX_MSG - 1);
        free(input);

        room_broadcast(s, &out, -1);
        if (display_cb) display_cb(&out);
    }

    atomic_store(&running, 0);
    room_shutdown_all(s);
    for (int i = 0; i < n_tids; i++)
        pthread_join(tids[i], NULL);
}
