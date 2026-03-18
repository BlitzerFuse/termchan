#ifndef COMMANDS_H
#define COMMANDS_H

#include "protocol.h"
#include "session.h"
#include <stddef.h>

typedef enum {
    CMD_OK,
    CMD_QUIT,
    CMD_UNKNOWN
} CmdResult;

typedef struct {
    const char *name;
    const char *usage;
    CmdResult (*handler)(const char *args, char *nickname, Session *s);
} Command;

CmdResult cmd_dispatch(const char *input, char *nickname, Session *s);

int cmd_register(const char *name, const char *usage,
                 CmdResult (*handler)(const char *, char *, Session *));

#endif
