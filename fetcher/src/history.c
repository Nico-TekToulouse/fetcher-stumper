/*
** history.c -- lecture incrémentale des commandes de l'étudiant
**
** Deux sources possibles, dans cet ordre de préférence :
**
** 1. FETCHER_CMDLOG (recommandé, mis en place par fetcher/start-exam.sh) :
**    un fichier texte créé et rempli EXCLUSIVEMENT par un hook shell (une
**    commande = une ligne, ajoutée au moment exact de son exécution), donc
**    réellement append-only. C'est la source fiable.
**
** 2. Fallback : ~/.bash_history ou ~/.zsh_history (selon $SHELL) si
**    FETCHER_CMDLOG n'est pas définie. ATTENTION : bash et zsh peuvent
**    RÉÉCRIRE ces fichiers (pas seulement les étendre) selon leurs propres
**    options de déduplication (ex: zsh HIST_IGNORE_ALL_DUPS) ou leur
**    mécanisme de sauvegarde périodique -- une lecture par offset brut sur
**    un fichier qui n'est pas garanti append-only peut alors sauter des
**    commandes répétées ou en renvoyer en double. Ce fallback est donc
**    best-effort, pas garanti fiable : préférer FETCHER_CMDLOG.
**
** zsh stocke son historique avec un préfixe de métadonnées de la forme
** ": <timestamp>:<duree>;commande" -- pas de parsing fin ici, le texte brut
** est renvoyé tel quel (une commande multi-lignes avec '\' de continuation
** casserait un parsing naïf par ';').
*/

#include "fetcher.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>

static int build_history_path(char *out, size_t out_size)
{
    const char *home;
    const char *shell;
    const char *filename;
    const char *cmdlog;

    cmdlog = getenv("FETCHER_CMDLOG");
    if (cmdlog != NULL && cmdlog[0] != '\0') {
        if (snprintf(out, out_size, "%s", cmdlog) >= (int)out_size)
            return -1;
        return 0;
    }

    home = getenv("HOME");
    if (home == NULL || home[0] == '\0')
        home = "/tmp";

    shell = getenv("SHELL");
    if (shell != NULL && strstr(shell, "zsh") != NULL)
        filename = ".zsh_history";
    else if (shell != NULL && strstr(shell, "bash") != NULL)
        filename = ".bash_history";
    else
        filename = ".bash_history"; /* fallback raisonnable */

    if (snprintf(out, out_size, "%s/%s", home, filename) >= (int)out_size)
        return -1;
    return 0;
}

int history_tracker_init(history_tracker_t *tracker)
{
    struct stat st;

    if (tracker == NULL)
        return -1;

    if (build_history_path(tracker->path, sizeof(tracker->path)) != 0)
        return -1;

    if (stat(tracker->path, &st) == 0) {
        tracker->offset = (long)st.st_size;
        tracker->inode = st.st_ino;
    } else {
        /* le fichier n'existe pas encore : on part de zéro */
        tracker->offset = 0;
        tracker->inode = 0;
    }

    return 0;
}

ssize_t history_tracker_poll(history_tracker_t *tracker, char *out,
    size_t out_size)
{
    struct stat st;
    int fd;
    ssize_t n;
    size_t to_read;

    if (tracker == NULL || out == NULL || out_size == 0)
        return -1;

    if (stat(tracker->path, &st) != 0) {
        /* fichier absent pour l'instant : rien de nouveau */
        return 0;
    }

    if (st.st_ino != tracker->inode) {
        /* rotation ou recréation du fichier : on repart du début */
        tracker->inode = st.st_ino;
        tracker->offset = 0;
    }

    if ((long)st.st_size <= tracker->offset)
        return 0;

    fd = open(tracker->path, O_RDONLY);
    if (fd < 0)
        return -1;

    if (lseek(fd, tracker->offset, SEEK_SET) == (off_t)-1) {
        close(fd);
        return -1;
    }

    to_read = out_size - 1;
    n = read(fd, out, to_read);
    if (n < 0) {
        close(fd);
        return -1;
    }

    close(fd);
    out[n] = '\0';
    tracker->offset += n;

    return n;
}
