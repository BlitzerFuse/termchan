#include "tui.h"
#include "tui_internal.h"
#include <ncurses.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <pthread.h>

static WINDOW *msg_win;
static WINDOW *input_win;
static char    prompt[MAX_NAME + 8];
static char    g_last_sender[MAX_NAME] = {0};

/* Protects all ncurses calls shared between the main thread (input)
   and peer recv threads (display_message / tui_status).
   ncurses is not thread-safe. */
static pthread_mutex_t tui_mu = PTHREAD_MUTEX_INITIALIZER;

static void draw_input_bar(void) {
    mvwhline(input_win, 0, 0, ACS_HLINE, COLS);
    mvwprintw(input_win, 1, 0, "%s", prompt);
    wclrtoeol(input_win);
    wrefresh(input_win);
}

void tui_clear_chat(void) {
    pthread_mutex_lock(&tui_mu);
    werase(msg_win);
    wrefresh(msg_win);
    draw_input_bar();
    pthread_mutex_unlock(&tui_mu);
}

const char *tui_get_last_sender(void) {
    return g_last_sender[0] ? g_last_sender : NULL;
}

void tui_init(const char *nickname) {
    ncurses_start();
    keypad(stdscr, TRUE);
    int msg_h = LINES - 2;
    msg_win   = newwin(msg_h, COLS, 0,     0);
    input_win = newwin(2,     COLS, msg_h, 0);
    scrollok(msg_win, TRUE);
    idlok(msg_win, TRUE);
    snprintf(prompt, sizeof(prompt), "[%s] > ", nickname);
    wprintw(msg_win, "termchat connected as %s\nType /help for commands.\n\n", nickname);
    wrefresh(msg_win);
    draw_input_bar();
}

void tui_shutdown(void) {
    delwin(msg_win);
    delwin(input_win);
    endwin();
}

void tui_display_message(Packet *p) {
    char tbuf[6];
    time_t now = time(NULL);
    strftime(tbuf, sizeof(tbuf), "%H:%M", localtime(&now));

    pthread_mutex_lock(&tui_mu);
    switch (p->type) {
        case MSG:
            strncpy(g_last_sender, p->sender, MAX_NAME - 1);
            g_last_sender[MAX_NAME - 1] = '\0';
            if (p->target[0])
                wprintw(msg_win, "[%s] [%s -> %s]: %s\n",
                        tbuf, p->sender, p->target, p->content);
            else
                wprintw(msg_win, "[%s] %s: %s\n", tbuf, p->sender, p->content);
            break;
        case PEER_JOIN:
        case PEER_LEAVE:
            wprintw(msg_win, "--- %s ---\n", p->content);
            break;
        case NICK_CHANGE:
            wprintw(msg_win, "--- %s is now known as %s ---\n",
                    p->sender, p->content);
            break;
        default:
            break;
    }
    wrefresh(msg_win);
    wrefresh(input_win);
    pthread_mutex_unlock(&tui_mu);
}

void tui_status(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    char buf[256];
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    pthread_mutex_lock(&tui_mu);
    wprintw(msg_win, "*** %s ***\n", buf);
    wrefresh(msg_win);
    pthread_mutex_unlock(&tui_mu);
}

char *tui_get_input(void) {
    if (tui_was_resized()) return NULL;

    pthread_mutex_lock(&tui_mu);
    draw_input_bar();
    wmove(input_win, 1, (int)strlen(prompt));
    echo();
    pthread_mutex_unlock(&tui_mu);

    /* wgetnstr blocks -- release tui_mu while waiting so that
       peer recv threads can still call tui_display_message
       and tui_status while the user is typing. */
    char buf[MAX_MSG];
    int r = wgetnstr(input_win, buf, sizeof(buf) - 1);

    pthread_mutex_lock(&tui_mu);
    noecho();
    pthread_mutex_unlock(&tui_mu);

    if (r == ERR || tui_was_resized()) return NULL;
    return strdup(buf);
}
