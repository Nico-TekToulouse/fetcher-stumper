/*
** main.c -- point d'entrée du fetcher
**
** Transparence : ce programme est volontairement visible (processus
** classique, pas de démon caché). Il affiche clairement qu'il est actif et
** pour quel identifiant il surveille/transmet l'historique shell.
*/

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include "fetcher.h"

static int run_fetcher(fetcher_config_t *cfgp)
{
    fetcher_config_t cfg = *cfgp;
    history_tracker_t tracker;
    heartbeat_arg_t hb_arg;
    pthread_t hb_thread;
    char buf[PROTO_MAX_MESSAGE];
    ssize_t n;

    signal_handler_install();

    if (history_tracker_init(&tracker) != 0) {
        fprintf(stderr,
            "fetcher: impossible d'initialiser le suivi de l'historique\n");
        return 1;
    }

    cfg.sockfd = network_connect(&cfg);
    if (cfg.sockfd < 0) {
        fprintf(stderr, "fetcher: connexion au serveur %s:%d impossible\n",
            cfg.server_host, cfg.server_port);
        return 1;
    }

    printf("fetcher: actif - identifiant=\"%s\" serveur=%s:%d "
        "(processus visible, surveillance transparente de l'historique shell)\n",
        cfg.identifier, cfg.server_host, cfg.server_port);
    fflush(stdout);

    hb_arg.sockfd = cfg.sockfd;
    strncpy(hb_arg.identifier, cfg.identifier, sizeof(hb_arg.identifier) - 1);
    hb_arg.identifier[sizeof(hb_arg.identifier) - 1] = '\0';

    if (pthread_create(&hb_thread, NULL, heartbeat_loop, &hb_arg) != 0) {
        fprintf(stderr, "fetcher: impossible de demarrer le thread heartbeat\n");
        close(cfg.sockfd);
        return 1;
    }
    pthread_detach(hb_thread);

    for (;;) {
        n = history_tracker_poll(&tracker, buf, sizeof(buf));
        if (n > 0) {
            if (network_send_message(cfg.sockfd, cfg.identifier,
                    PROTO_TYPE_CMD, buf, (size_t)n) != 0) {
                fprintf(stderr, "fetcher: echec d'envoi au serveur\n");
            }
        } else if (n < 0) {
            fprintf(stderr, "fetcher: erreur de lecture de l'historique\n");
        }
        sleep(1);
    }

    return 0;
}

int main(int argc, char **argv)
{
    fetcher_config_t cfg;

    /*
    ** args_parse() est appelé AVANT watchdog_run() : le watchdog (processus
    ** parent après le fork() interne) a besoin de cfg pour pouvoir notifier
    ** le serveur (PROTO_TYPE_KILLED) en cas d'arrêt illégitime de l'enfant.
    ** Comme fork() copie la mémoire du processus, parent et enfant ont tous
    ** les deux une copie cohérente de cfg sans avoir à reparser argv.
    */
    if (args_parse(argc, argv, &cfg) != 0)
        return 1;

    if (watchdog_run(&cfg) != 0)
        return 1;

    /*
    ** À partir d'ici, seul le processus "enfant" (le fetcher réel) exécute
    ** ce code. Le processus "watchdog" parent ne revient jamais de
    ** watchdog_run() : il surveille l'enfant et se termine lui-même.
    */
    return run_fetcher(&cfg);
}
