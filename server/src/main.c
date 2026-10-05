/*
** main.c -- point d'entrée du server : orchestration des threads
** (réseau, heartbeat monitor, UI ncurses).
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include "server.h"

static server_context_t g_ctx;

static void handle_sigint(int sig)
{
    (void)sig;
    g_ctx.running = 0;
    /*
    ** accept() reste bloqué sur une connexion : on ferme le listen_fd pour
    ** le débloquer proprement. Les threads clients détachés se terminent
    ** d'eux-mêmes à la prochaine erreur de recv() (ou restent jusqu'à la
    ** fin du process, ce qui est acceptable ici car tout est en mémoire
    ** et le process va de toute façon se terminer).
    */
    close(g_ctx.listen_fd);
}

int main(int argc, char **argv)
{
    int port;
    const char *whitelist_path;
    pthread_t net_tid;
    pthread_t hb_tid;
    pthread_t ui_tid;
    struct sigaction sa;

    port = PROTO_DEFAULT_PORT;
    if (argc > 1)
        port = atoi(argv[1]);
    if (port <= 0 || port > 65535) {
        fprintf(stderr, "server: port invalide: %s\n", argv[1]);
        return 1;
    }
    whitelist_path = (argc > 2) ? argv[2] : NULL;
    if (whitelist_load(&g_ctx.whitelist, whitelist_path) != 0)
        return 1;
    last_event_init(&g_ctx.last_rejection);
    client_registry_init(&g_ctx.registry);
    g_ctx.listen_fd = network_listen(port);
    if (g_ctx.listen_fd < 0) {
        client_registry_destroy(&g_ctx.registry);
        return 1;
    }
    g_ctx.running = 1;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = handle_sigint;
    sigaction(SIGINT, &sa, NULL);
    printf("server: en écoute sur le port %d (mémoire uniquement, aucune "
        "persistance disque)\n", port);
    if (whitelist_is_active(&g_ctx.whitelist))
        printf("server: whitelist active (%s, %zu identifiant(s) autorise(s))\n",
            whitelist_path, g_ctx.whitelist.count);
    else
        printf("server: aucune whitelist -- toute connexion est acceptee\n");
    fflush(stdout);
    pthread_create(&net_tid, NULL, network_accept_loop, &g_ctx);
    pthread_create(&hb_tid, NULL, heartbeat_monitor_loop, &g_ctx);
    pthread_create(&ui_tid, NULL, ncurses_ui_loop, &g_ctx);
    pthread_join(net_tid, NULL);
    g_ctx.running = 0;
    pthread_join(hb_tid, NULL);
    pthread_join(ui_tid, NULL);
    client_registry_destroy(&g_ctx.registry);
    return 0;
}
