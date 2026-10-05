/*
** heartbeat_monitor.c -- thread de surveillance des heartbeats clients
*/

#include <stdio.h>
#include <unistd.h>
#include "server.h"

void *heartbeat_monitor_loop(void *arg)
{
    server_context_t *ctx;
    client_t *cur;
    time_t now;
    time_t since;

    ctx = (server_context_t *)arg;
    while (ctx->running) {
        pthread_mutex_lock(&ctx->registry.lock);
        now = time(NULL);
        for (cur = ctx->registry.head; cur != NULL; cur = cur->next) {
            since = now - cur->last_heartbeat;
            if (since > PROTO_HEARTBEAT_TIMEOUT_SEC &&
                cur->status != CLIENT_ALERT_DISCONNECTED) {
                cur->status = CLIENT_ALERT_DISCONNECTED;
                printf("[ALERT] %s ne répond plus (dernier heartbeat il y a "
                    "%lds) - arrêt suspect (kill -9, crash, ou déconnexion "
                    "réseau)\n", cur->identifier, (long)since);
                fflush(stdout);
            }
        }
        pthread_mutex_unlock(&ctx->registry.lock);
        sleep(1);
    }
    return NULL;
}
