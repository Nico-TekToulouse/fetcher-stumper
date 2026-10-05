/*
** command_buffer.c -- allocation/libération des commandes reçues
**
** Tout est gardé en mémoire uniquement (aucune écriture sur disque).
*/

#include <stdlib.h>
#include <string.h>
#include "server.h"

command_entry_t *command_entry_create(const char *text)
{
    command_entry_t *entry;

    entry = malloc(sizeof(*entry));
    if (entry == NULL)
        return NULL;
    entry->text = strdup(text);
    if (entry->text == NULL) {
        free(entry);
        return NULL;
    }
    entry->received_at = time(NULL);
    entry->next = NULL;
    return entry;
}

void command_entry_destroy(command_entry_t *entry)
{
    if (entry == NULL)
        return;
    free(entry->text);
    free(entry);
}
