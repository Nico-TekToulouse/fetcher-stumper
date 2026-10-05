/*
** ncurses_input.c -- gestion des entrees clavier de l'UI ncurses.
**
** Convention de retour de ncurses_input_handle() :
**   0 -> continuer normalement (touche geree ou ignoree)
**   1 -> demande de sortie de l'interface ncurses (touche 'q'/'Q')
**
** La fonction ne fait jamais d'appel bloquant : elle prend reg->lock
** uniquement le temps de compter les clients pour borner
** *selected_index, puis le relache immediatement.
*/

#include <ncurses.h>
#include "server.h"

static size_t count_clients(client_registry_t *reg)
{
    size_t count = 0;
    client_t *cur;

    pthread_mutex_lock(&reg->lock);
    for (cur = reg->head; cur != NULL; cur = cur->next)
        count++;
    pthread_mutex_unlock(&reg->lock);
    return count;
}

static void clamp_selected_index(int *selected_index, size_t count)
{
    if (count == 0) {
        *selected_index = -1;
        return;
    }
    if (*selected_index < 0)
        *selected_index = 0;
    if (*selected_index >= (int)count)
        *selected_index = (int)count - 1;
}

int ncurses_input_handle(int ch, client_registry_t *reg, int *selected_index)
{
    size_t count = count_clients(reg);

    if (ch == 'q' || ch == 'Q')
        return 1;
    if (ch == KEY_UP) {
        clamp_selected_index(selected_index, count);
        if (*selected_index > 0)
            (*selected_index)--;
        return 0;
    }
    if (ch == KEY_DOWN) {
        clamp_selected_index(selected_index, count);
        if (count > 0 && *selected_index < (int)count - 1)
            (*selected_index)++;
        return 0;
    }
    clamp_selected_index(selected_index, count);
    return 0;
}
