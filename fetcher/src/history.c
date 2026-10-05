/*
** history.c -- lecture incrémentale de l'historique shell (bash/zsh)
**
** Choix d'implémentation : zsh stocke son historique avec un préfixe de
** métadonnées de la forme ": <timestamp>:<duree>;commande". On ne fait PAS
** de parsing fin ligne par ligne ici : le texte brut nouvellement ajouté au
** fichier est renvoyé tel quel au serveur. C'est un choix volontaire de
** simplicité et de fidélité (zsh peut produire des entrées multi-lignes
** avec des '\' de continuation ; un parsing naïf par ';' casserait ce genre
** de cas). Un nettoyage plus fin (extraction après le dernier ';') peut être
** ajouté côté serveur si besoin.
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
