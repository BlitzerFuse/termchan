#ifndef CONFIG_H
#define CONFIG_H

#include "protocol.h"

#define CONFIG_PATH "/.termchan/termchan.conf"

typedef struct {
    char nickname[MAX_NAME];
    int  port;
    int  discovery_port;
} Config;

int  config_load(Config *cfg);

int  config_save(const Config *cfg);

void config_defaults(Config *cfg);

#endif
