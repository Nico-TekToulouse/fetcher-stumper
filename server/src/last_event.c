/*
** last_event.c -- dernier événement notable à afficher dans l'UI ncurses
** (ex: dernier rejet whitelist), à la place d'un printf() direct qui
** corromprait l'écran contrôlé par ncurses.
*/

#include "server.h"
#include <string.h>

void last_event_init(last_event_t *ev)
{
    ev->text[0] = '\0';
    ev->has_value = 0;
    pthread_mutex_init(&ev->lock, NULL);
}

void last_event_set(last_event_t *ev, const char *text)
{
    pthread_mutex_lock(&ev->lock);
    strncpy(ev->text, text, sizeof(ev->text) - 1);
    ev->text[sizeof(ev->text) - 1] = '\0';
    ev->has_value = 1;
    pthread_mutex_unlock(&ev->lock);
}

int last_event_get(last_event_t *ev, char *out, size_t out_size)
{
    int had_value;

    pthread_mutex_lock(&ev->lock);
    had_value = ev->has_value;
    if (had_value) {
        strncpy(out, ev->text, out_size - 1);
        out[out_size - 1] = '\0';
    }
    pthread_mutex_unlock(&ev->lock);
    return had_value;
}
