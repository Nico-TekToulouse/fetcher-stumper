/*
** signal_handler.c -- interception de SIGINT/SIGTERM avec mot de passe
** enseignant avant tout arrêt réel.
**
** IMPORTANT : SIGKILL n'est ni intercepté ni bloqué (et ne peut de toute
** façon pas l'être). Le process doit pouvoir être tué par `kill -9`
** normalement -- c'est voulu, aucun contournement n'est tenté ici.
**
** Mot de passe : lu depuis la variable d'environnement
** FETCHER_TEACHER_PASSWORD au moment de signal_handler_install() (donc une
** seule fois, hors du handler de signal lui-même -- getenv() n'est pas
** async-signal-safe, on ne l'appelle jamais depuis handle_stop_signal()).
** Copié dans un buffer statique g_teacher_password pour que le handler
** n'ait plus qu'à comparer une chaîne déjà en mémoire. Si la variable
** n'est pas définie, un mot de passe par défaut est utilisé et un
** avertissement est affiché sur stderr -- NE PAS utiliser ce défaut pour
** un examen réel.
*/

#include "fetcher.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <termios.h>
#include <unistd.h>

#define TEACHER_PASSWORD_ENV "FETCHER_TEACHER_PASSWORD"
#define TEACHER_PASSWORD_DEFAULT "exam2026"
#define TEACHER_PASSWORD_MAX 128

static char g_teacher_password[TEACHER_PASSWORD_MAX];

static void read_password_no_echo(char *buf, size_t size)
{
    struct termios oldt;
    struct termios newt;
    int have_termios;
    size_t len;

    memset(buf, 0, size);

    have_termios = (tcgetattr(STDIN_FILENO, &oldt) == 0);
    if (have_termios) {
        newt = oldt;
        newt.c_lflag &= ~((tcflag_t)ECHO);
        tcsetattr(STDIN_FILENO, TCSANOW, &newt);
    }

    if (fgets(buf, (int)size, stdin) == NULL)
        buf[0] = '\0';

    if (have_termios)
        tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
    fprintf(stdout, "\n");

    len = strlen(buf);
    if (len > 0 && buf[len - 1] == '\n')
        buf[len - 1] = '\0';
}

static void handle_stop_signal(int signum)
{
    char password[128];

    (void)signum;

    fprintf(stdout,
        "\n[fetcher] Signal d'arret recu. Mot de passe enseignant requis: ");
    fflush(stdout);

    read_password_no_echo(password, sizeof(password));

    if (strcmp(password, g_teacher_password) == 0) {
        fprintf(stdout, "[fetcher] Mot de passe correct, arret legitime.\n");
        fflush(stdout);
        stop_token_signal_legitimate_exit();
        exit(0);
    }

    fprintf(stdout,
        "[fetcher] Mot de passe incorrect, poursuite de la surveillance.\n");
    fflush(stdout);
}

static void load_teacher_password(void)
{
    const char *env_val;

    env_val = getenv(TEACHER_PASSWORD_ENV);
    if (env_val == NULL || env_val[0] == '\0') {
        fprintf(stderr,
            "fetcher: attention, %s n'est pas definie -- utilisation d'un "
            "mot de passe par defaut (A NE PAS utiliser pour un examen "
            "reel)\n", TEACHER_PASSWORD_ENV);
        strncpy(g_teacher_password, TEACHER_PASSWORD_DEFAULT,
            sizeof(g_teacher_password) - 1);
        g_teacher_password[sizeof(g_teacher_password) - 1] = '\0';
        return;
    }
    strncpy(g_teacher_password, env_val, sizeof(g_teacher_password) - 1);
    g_teacher_password[sizeof(g_teacher_password) - 1] = '\0';
}

void signal_handler_install(void)
{
    struct sigaction sa;

    load_teacher_password();

    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = handle_stop_signal;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;

    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
}
