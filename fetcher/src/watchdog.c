/*
** watchdog.c -- supervision du fetcher (anti-kill simple, sans resistance a
** SIGKILL : le watchdog relance juste un nouveau fetcher, il n'empeche en
** rien le `kill -9` de fonctionner normalement sur le processus visé).
**
** Architecture choisie : watchdog_run() est appelé tout au début de main(),
** AVANT tout autre traitement (args_parse, etc). Il fait un fork() :
**
**   - Le processus PARENT devient le "watchdog" : il ne fait plus rien
**     d'autre que surveiller l'enfant via waitpid(), et NE RETOURNE JAMAIS
**     à l'appelant (il appelle exit() lui-même en fin de vie).
**   - Le processus ENFANT retourne 0 à main(), qui continue alors son
**     exécution normale (args_parse, connexion, boucle principale, etc).
**     C'est donc l'enfant qui est le "vrai" fetcher visible côté étudiant.
**
** Si l'enfant meurt sans avoir signalé un arrêt légitime (mot de passe
** enseignant validé via Ctrl+C/SIGTERM, cf signal_handler.c), le watchdog
** considère que c'est un arrêt illégitime (kill -9, crash...) et relance un
** nouveau fetcher à l'identique (même argc/argv), simplement en refaisant un
** tour de la boucle fork() ci-dessous -- PAS via execve : le process courant
** est déjà le bon binaire avec les bons argv, un simple fork() suffit et
** reste plus simple/robuste qu'une réexécution complète.
**
** Communication enfant -> watchdog : un pipe anonyme créé AVANT chaque
** fork(). L'enfant reçoit l'extrémité d'écriture (stop_token_set_pipe()).
** S'il se termine légitimement, il écrit un octet convenu dans ce pipe
** juste avant de quitter. Le watchdog lit le pipe juste après waitpid() :
** à ce stade l'extrémité d'écriture est forcément fermée (côté watchdog
** comme côté enfant terminé), donc ce read() ne bloque jamais, qu'il y ait
** eu écriture ou non.
**
** Appel attendu depuis main.c : args_parse() DOIT avoir été appelé avant
** (le watchdog a besoin de cfg->server_host/port/identifier pour pouvoir
** notifier le serveur en cas d'arrêt illégitime, voir notify_server_killed
** ci-dessous) :
**
**     int main(int argc, char **argv)
**     {
**         fetcher_config_t cfg;
**         if (args_parse(argc, argv, &cfg) != 0)
**             return 1;
**         if (watchdog_run(&cfg) != 0)
**             return 1;
**         // ... suite normale du fetcher : ce code ne s'exécute que dans
**         // le processus "enfant" (le fetcher réel). Le processus
**         // "watchdog" parent ne revient jamais ici, il exit() lui-même.
**         ...
**     }
**
** Notification serveur (PROTO_TYPE_KILLED) : un redémarrage par le watchdog
** peut être plus rapide que PROTO_HEARTBEAT_TIMEOUT_SEC (le nouveau fetcher
** se reconnecte et renvoie un heartbeat avant que le serveur ait eu le temps
** de détecter l'absence de heartbeat). Pour garantir malgré tout que le
** serveur soit notifié d'un `kill -9`/crash, le watchdog ouvre lui-même une
** brève connexion TCP juste avant de relancer, envoie un message
** PROTO_TYPE_KILLED, puis la referme. Cette connexion est indépendante de
** celle du fetcher (qui est morte avec lui) : le watchdog ne maintient
** aucune connexion permanente, seulement cet aller-retour ponctuel.
*/

#include "fetcher.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/wait.h>

static int child_sent_legitimate_exit(int read_fd)
{
    char c;
    ssize_t n;

    n = read(read_fd, &c, 1);
    return (n == 1 && c == 'Q');
}

/*
** Notifie le serveur d'un arrêt illégitime avant de relancer. Best-effort :
** si la connexion échoue (serveur injoignable), on log sur stderr et on
** relance quand même -- ne jamais bloquer le redémarrage là-dessus.
*/
static void notify_server_killed(const fetcher_config_t *cfg)
{
    int sockfd;
    static const char msg[] =
        "arret illegitime detecte (kill -9/crash), redemarrage automatique";

    sockfd = network_connect(cfg);
    if (sockfd < 0) {
        fprintf(stderr,
            "watchdog: impossible de notifier le serveur de l'arret "
            "(connexion echouee)\n");
        return;
    }
    if (network_send_message(sockfd, cfg->identifier, PROTO_TYPE_KILLED, msg,
            strlen(msg)) != 0) {
        fprintf(stderr, "watchdog: echec d'envoi de la notification KILLED\n");
    }
    close(sockfd);
}

int watchdog_run(const fetcher_config_t *cfg)
{
    int pipefd[2];
    pid_t pid;
    int status;
    int legitimate;

    for (;;) {
        if (pipe(pipefd) == -1) {
            perror("watchdog_run: pipe");
            exit(1);
        }

        pid = fork();
        if (pid == -1) {
            perror("watchdog_run: fork");
            exit(1);
        }

        if (pid == 0) {
            /* enfant : devient le fetcher réel */
            close(pipefd[0]);
            stop_token_set_pipe(pipefd[1]);
            return 0;
        }

        /* parent : watchdog */
        close(pipefd[1]);
        if (waitpid(pid, &status, 0) == -1)
            perror("watchdog_run: waitpid");

        legitimate = child_sent_legitimate_exit(pipefd[0]);
        close(pipefd[0]);

        if (legitimate)
            exit(0);

        fprintf(stderr,
            "watchdog: le fetcher s'est arrete de maniere inattendue, "
            "redemarrage...\n");
        notify_server_killed(cfg);
    }
}
