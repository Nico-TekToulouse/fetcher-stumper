/*
** whitelist.c -- liste blanche optionnelle d'identifiants autorisés.
**
** Format du fichier : un identifiant par ligne. Lignes vides et lignes
** commençant par '#' ignorées (commentaires). Espaces de début/fin
** retirés. Chargé une seule fois au démarrage du serveur, en mémoire
** uniquement (relecture du fichier de config, pas de persistance des
** données de surveillance -- aucun lien avec la contrainte "pas de
** persistance" qui concerne les commandes reçues, pas la configuration).
*/

#include "server.h"
#include <stdio.h>
#include <string.h>

static void trim(char *s)
{
    size_t len;
    char *start;

    start = s;
    while (*start == ' ' || *start == '\t')
        start++;
    if (start != s)
        memmove(s, start, strlen(start) + 1);
    len = strlen(s);
    while (len > 0 &&
        (s[len - 1] == '\n' || s[len - 1] == '\r' ||
         s[len - 1] == ' ' || s[len - 1] == '\t')) {
        s[len - 1] = '\0';
        len--;
    }
}

int whitelist_load(whitelist_t *wl, const char *path)
{
    FILE *f;
    char line[PROTO_MAX_IDENTIFIER + 16];

    wl->count = 0;
    wl->active = 0;
    if (path == NULL)
        return 0;

    f = fopen(path, "r");
    if (f == NULL) {
        fprintf(stderr, "whitelist_load: impossible d'ouvrir %s\n", path);
        return -1;
    }
    while (fgets(line, sizeof(line), f) != NULL) {
        trim(line);
        if (line[0] == '\0' || line[0] == '#')
            continue;
        if (wl->count >= WHITELIST_MAX_ENTRIES) {
            fprintf(stderr,
                "whitelist_load: limite de %d entrees atteinte, le reste "
                "de %s est ignore\n", WHITELIST_MAX_ENTRIES, path);
            break;
        }
        strncpy(wl->entries[wl->count], line, PROTO_MAX_IDENTIFIER - 1);
        wl->entries[wl->count][PROTO_MAX_IDENTIFIER - 1] = '\0';
        wl->count++;
    }
    fclose(f);
    wl->active = 1;
    return 0;
}

int whitelist_is_active(const whitelist_t *wl)
{
    return wl->active;
}

int whitelist_allows(const whitelist_t *wl, const char *identifier)
{
    size_t i;

    if (!wl->active)
        return 1;
    for (i = 0; i < wl->count; i++) {
        if (strncmp(wl->entries[i], identifier, PROTO_MAX_IDENTIFIER) == 0)
            return 1;
    }
    return 0;
}
