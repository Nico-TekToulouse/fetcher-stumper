/*
** client_registry.c -- liste chaînée de clients protégée par mutex
**
** Choix d'architecture notables :
**
** - client_t.sockfd : le socket d'un client n'est PAS géré ici. La socket
**   est ouverte/fermée par network.c (un thread par connexion), qui met
**   simplement à jour client->sockfd une fois le client identifié. Ici,
**   à la création, sockfd vaut -1 par défaut : "pas encore de socket
**   connue pour ce client" (le registry peut exister indépendamment du
**   réseau, par exemple si on voulait un jour le peupler autrement).
**
** - client_registry_touch_heartbeat(client_t *client) : la signature
**   déclarée dans server.h ne prend qu'un client_t*, pas le registry.
**   Pour rester verrouillé correctement malgré tout (et ne pas casser
**   cette signature dont dépendent d'autres agents), on mémorise un
**   pointeur statique vers le registry actif, posé par
**   client_registry_init(). Le projet n'a qu'un seul registry global
**   (un seul server_context_t par process), donc cette hypothèse est
**   sûre ici. Si un jour plusieurs registries coexistent, il faudra
**   changer la signature pour accepter explicitement le registry.
*/

#include <stdlib.h>
#include <string.h>
#include "server.h"

static client_registry_t *g_active_registry = NULL;

void client_registry_init(client_registry_t *reg)
{
    reg->head = NULL;
    pthread_mutex_init(&reg->lock, NULL);
    g_active_registry = reg;
}

client_t *client_registry_find_or_create(client_registry_t *reg,
    const char *identifier)
{
    client_t *cur;
    client_t *created;

    pthread_mutex_lock(&reg->lock);
    for (cur = reg->head; cur != NULL; cur = cur->next) {
        if (strncmp(cur->identifier, identifier, PROTO_MAX_IDENTIFIER) == 0) {
            cur->status = CLIENT_CONNECTED;
            pthread_mutex_unlock(&reg->lock);
            return cur;
        }
    }
    created = malloc(sizeof(*created));
    if (created == NULL) {
        pthread_mutex_unlock(&reg->lock);
        return NULL;
    }
    strncpy(created->identifier, identifier, PROTO_MAX_IDENTIFIER - 1);
    created->identifier[PROTO_MAX_IDENTIFIER - 1] = '\0';
    created->sockfd = -1;
    created->commands_head = NULL;
    created->commands_tail = NULL;
    created->command_count = 0;
    created->status = CLIENT_CONNECTED;
    created->last_heartbeat = time(NULL);
    created->next = reg->head;
    reg->head = created;
    pthread_mutex_unlock(&reg->lock);
    return created;
}

void client_registry_add_command(client_registry_t *reg, client_t *client,
    const char *text)
{
    command_entry_t *entry;

    entry = command_entry_create(text);
    if (entry == NULL)
        return;
    pthread_mutex_lock(&reg->lock);
    /*
    ** Pas de limite de taille imposée ici (stockage mémoire uniquement,
    ** comme demandé). Une limite (ex: FIFO bornée par client) pourrait
    ** être ajoutée plus tard si la consommation mémoire devient un
    ** problème lors d'examens très longs.
    */
    if (client->commands_tail == NULL) {
        client->commands_head = entry;
        client->commands_tail = entry;
    } else {
        client->commands_tail->next = entry;
        client->commands_tail = entry;
    }
    client->command_count++;
    pthread_mutex_unlock(&reg->lock);
}

void client_registry_touch_heartbeat(client_t *client)
{
    if (g_active_registry != NULL)
        pthread_mutex_lock(&g_active_registry->lock);
    client->last_heartbeat = time(NULL);
    client->status = CLIENT_CONNECTED;
    if (g_active_registry != NULL)
        pthread_mutex_unlock(&g_active_registry->lock);
}

/*
** Marque explicitement un client en alerte (ex: notification PROTO_TYPE_KILLED
** reçue du watchdog d'un fetcher juste avant un redémarrage suite à un
** kill -9/crash). Contrairement à client_registry_touch_heartbeat(), ne met
** PAS à jour last_heartbeat : le but est justement de signaler l'incident
** immédiatement dans l'UI, sans attendre le timeout habituel. Le statut
** repassera à CLIENT_CONNECTED dès le prochain heartbeat/commande reçu du
** fetcher relancé (comportement voulu : l'alerte est un signal ponctuel
** de l'incident, pas un état bloquant).
*/
void client_registry_mark_alert(client_t *client)
{
    if (g_active_registry != NULL)
        pthread_mutex_lock(&g_active_registry->lock);
    client->status = CLIENT_ALERT_DISCONNECTED;
    if (g_active_registry != NULL)
        pthread_mutex_unlock(&g_active_registry->lock);
}

void client_registry_destroy(client_registry_t *reg)
{
    client_t *cur;
    client_t *next;
    command_entry_t *centry;
    command_entry_t *cnext;

    pthread_mutex_lock(&reg->lock);
    cur = reg->head;
    while (cur != NULL) {
        next = cur->next;
        centry = cur->commands_head;
        while (centry != NULL) {
            cnext = centry->next;
            command_entry_destroy(centry);
            centry = cnext;
        }
        free(cur);
        cur = next;
    }
    reg->head = NULL;
    pthread_mutex_unlock(&reg->lock);
    pthread_mutex_destroy(&reg->lock);
    if (g_active_registry == reg)
        g_active_registry = NULL;
}
