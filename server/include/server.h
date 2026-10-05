/*
** server.h -- structures et prototypes partagés du serveur
*/

#ifndef SERVER_H_
#define SERVER_H_

#include <pthread.h>
#include <time.h>
#include "protocol.h"

typedef enum client_status {
    CLIENT_CONNECTED,
    CLIENT_ALERT_DISCONNECTED,
} client_status_t;

typedef struct command_entry {
    char *text;
    time_t received_at;
    struct command_entry *next;
} command_entry_t;

typedef struct client {
    char identifier[PROTO_MAX_IDENTIFIER];
    int sockfd;
    time_t last_heartbeat;
    client_status_t status;
    command_entry_t *commands_head;
    command_entry_t *commands_tail;
    size_t command_count;
    struct client *next;
} client_t;

typedef struct client_registry {
    client_t *head;
    pthread_mutex_t lock;
} client_registry_t;

/* client_registry.c */
void client_registry_init(client_registry_t *reg);
client_t *client_registry_find_or_create(client_registry_t *reg,
    const char *identifier);
void client_registry_add_command(client_registry_t *reg, client_t *client,
    const char *text);
void client_registry_touch_heartbeat(client_t *client);
void client_registry_mark_alert(client_t *client);
void client_registry_destroy(client_registry_t *reg);

/* parser.c */
int message_parse(const char *line, char *identifier, size_t identifier_size,
    char *type, size_t type_size, char *payload, size_t payload_size,
    size_t *payload_len);

/* heartbeat_monitor.c */
void *heartbeat_monitor_loop(void *arg);

/* command_buffer.c */
command_entry_t *command_entry_create(const char *text);
void command_entry_destroy(command_entry_t *entry);

/* network.c */
int network_listen(int port);
void *network_accept_loop(void *arg);

/*
** whitelist.c -- liste blanche optionnelle d'identifiants autorisés à se
** connecter. Chargée une seule fois au démarrage depuis un fichier texte
** (un identifiant par ligne, lignes vides et commentaires '#' ignorés).
** Si aucun fichier n'est fourni au serveur, la whitelist reste "inactive"
** (whitelist_is_active() retourne 0) et toute connexion est acceptée,
** comme avant cette fonctionnalité.
*/
#define WHITELIST_MAX_ENTRIES 256

typedef struct whitelist {
    char entries[WHITELIST_MAX_ENTRIES][PROTO_MAX_IDENTIFIER];
    size_t count;
    int active;
} whitelist_t;

int whitelist_load(whitelist_t *wl, const char *path);
int whitelist_is_active(const whitelist_t *wl);
int whitelist_allows(const whitelist_t *wl, const char *identifier);

/*
** last_event.c -- petit "dernier événement notable" affiché dans l'UI
** ncurses (ex: dernier rejet whitelist). Ne PAS utiliser printf() depuis
** un thread une fois ncurses démarré : ncurses contrôle alors tout
** l'écran du terminal, et un printf concurrent corrompt l'affichage
** (c'est exactement le bug que ce mécanisme remplace).
*/
typedef struct last_event {
    char text[160];
    int has_value;
    pthread_mutex_t lock;
} last_event_t;

void last_event_init(last_event_t *ev);
void last_event_set(last_event_t *ev, const char *text);
int last_event_get(last_event_t *ev, char *out, size_t out_size);

typedef struct server_context {
    client_registry_t registry;
    int listen_fd;
    volatile int running;
    whitelist_t whitelist;
    last_event_t last_rejection;
} server_context_t;

/* ncurses_ui.c */
void *ncurses_ui_loop(void *arg);

/* ncurses_input.c */
int ncurses_input_handle(int ch, client_registry_t *reg, int *selected_index);

#endif /* SERVER_H_ */
