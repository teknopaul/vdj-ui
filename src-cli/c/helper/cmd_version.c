#include <stdio.h>

#include "../lib/status.h"
#include "dispatch.h"

/* set from ./version by the Makefile */
#ifndef TC_VERSION
#define TC_VERSION "dev"
#endif

int cmd_version(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    printf("trakcrate-helper %s (api %d)\n", TC_VERSION, TC_API_VERSION);
    return TC_OK;
}
