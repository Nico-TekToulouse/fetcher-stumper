/*
** stop_token.c -- signalement d'un arrêt légitime vers le watchdog
**
** Le fetcher (processus enfant du watchdog) dispose d'une extrémité
** d'écriture d'un pipe anonyme (voir watchdog.c). Lorsqu'un arrêt légitime
** est validé (mot de passe enseignant correct), un octet convenu ('Q') est
** écrit dans ce pipe juste avant de quitter, pour indiquer au watchdog qu'il
** ne doit PAS relancer un nouveau fetcher.
**
** Si aucun watchdog n'est actif (ex: tests manuels en lançant directement le
** binaire fetcher, stop_token_set_pipe jamais appelé), ces fonctions sont
** des no-op sûrs.
*/

#include "fetcher.h"
#include <unistd.h>

#define STOP_TOKEN_BYTE 'Q'

static int g_stop_write_fd = -1;

void stop_token_set_pipe(int write_fd)
{
    g_stop_write_fd = write_fd;
}

void stop_token_signal_legitimate_exit(void)
{
    ssize_t written;
    char byte;

    if (g_stop_write_fd < 0)
        return;

    byte = STOP_TOKEN_BYTE;
    written = write(g_stop_write_fd, &byte, 1);
    (void)written;
}
