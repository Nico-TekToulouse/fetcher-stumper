/*
** main.c -- point d'entrée du fetcher
**
** Transparence : ce programme est volontairement visible (processus
** classique, pas de démon caché). Il affiche clairement qu'il est actif et
** pour quel identifiant il surveille/transmet l'historique shell.
*/

#include <stdio.h>
#include <stdlib.h>
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
    if (getenv("FETCHER_CMDLOG") == NULL || getenv("FETCHER_CMDLOG")[0] == '\0') {
        fprintf(stderr,
            "fetcher: attention, FETCHER_CMDLOG n'est pas definie -- lecture "
            "degradee de l'historique shell brut (peut sauter des commandes "
            "repetees ou en renvoyer en double selon la config shell). "
            "Lancez via 'source start-exam.sh' pour un suivi fiable.\n");
    }
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
            /*
            ** Plusieurs commandes peuvent avoir été ajoutées depuis le
            ** dernier sondage : on envoie un message CMD par ligne complète
            ** plutôt qu'un seul message contenant tout le bloc, pour que
            ** chaque commande apparaisse comme une entrée distincte côté
            ** serveur. Une ligne incomplète en fin de lecture (rare : lu en
            ** pleine écriture du hook shell) n'est PAS envoyée tronquée --
            ** l'offset est rembobiné pour la relire complète au prochain
            ** sondage, combinée à ce qui aura été ajouté entre-temps.
            */
            char *line_start = buf;
            char *nl;
            ssize_t consumed;

            while ((nl = memchr(line_start, '\n',
                    (size_t)(buf + n - line_start))) != NULL) {
                size_t line_len = (size_t)(nl - line_start);

                if (line_len > 0) {
                    if (network_send_message(cfg.sockfd, cfg.identifier,
                            PROTO_TYPE_CMD, line_start, line_len) != 0) {
                        fprintf(stderr, "fetcher: echec d'envoi au serveur\n");
                    }
                }
                line_start = nl + 1;
            }
            consumed = line_start - buf;
            if (consumed < n)
                tracker.offset -= (n - consumed);
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
