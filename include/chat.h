#ifndef CHAT_H
#define CHAT_H

#include "protocol.h"
#include "session.h"

void start_chat(Session *s, void (*display_cb)(Packet *));

#endif
