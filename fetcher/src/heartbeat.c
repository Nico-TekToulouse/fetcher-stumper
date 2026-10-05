/*
** heartbeat.c -- thread envoyant un HEARTBEAT périodique au serveur
*/

#include "fetcher.h"
#include <unistd.h>

void *heartbeat_loop(void *arg)
{
    heartbeat_arg_t *hb;

    hb = (heartbeat_arg_t *)arg;
    if (hb == NULL)
        return NULL;

    for (;;) {
        network_send_message(hb->sockfd, hb->identifier, PROTO_TYPE_HEARTBEAT,
            "", 0);
        sleep(PROTO_HEARTBEAT_INTERVAL_SEC);
    }

    return NULL;
}
