/*
** heartbeat_monitor.c -- thread de surveillance des heartbeats clients
**
** Ne fait AUCUN printf() direct : ce thread tourne concurremment à l'UI
** ncurses (qui contrôle tout l'écran du terminal une fois démarrée), et un
** printf() ici corromprait l'affichage. Le passage en CLIENT_ALERT_DISCONNECTED
** suffit : l'UI ncurses lit ce statut et affiche le client en rouge/[ALERT]
** d'elle-même (voir ncurses_ui.c).
*/

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
            }
        }
        pthread_mutex_unlock(&ctx->registry.lock);
        sleep(1);
    }
    return NULL;
}
