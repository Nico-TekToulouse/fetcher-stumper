/*
** fetcher.h -- structures et prototypes partagés du fetcher
*/

#ifndef FETCHER_H_
#define FETCHER_H_

#include <stddef.h>
#include <sys/types.h>
#include "protocol.h"

#ifndef PATH_MAX_LOCAL
#define PATH_MAX_LOCAL 1024
#endif

typedef struct history_tracker {
    char path[PATH_MAX_LOCAL];
    long offset;
    ino_t inode;
} history_tracker_t;

typedef struct fetcher_config {
    char identifier[PROTO_MAX_IDENTIFIER];
    char server_host[256];
    int server_port;
    int sockfd;
} fetcher_config_t;

/* argument passé au thread heartbeat (heartbeat.c / main.c) */
typedef struct heartbeat_arg {
    int sockfd;
    char identifier[PROTO_MAX_IDENTIFIER];
} heartbeat_arg_t;

/* args.c */
int args_parse(int argc, char **argv, fetcher_config_t *cfg);

/* history.c */
int history_tracker_init(history_tracker_t *tracker);
ssize_t history_tracker_poll(history_tracker_t *tracker, char *out,
    size_t out_size);

/* network.c */
int network_connect(const fetcher_config_t *cfg);
int network_send_message(int sockfd, const char *identifier, const char *type,
    const char *payload, size_t payload_len);

/* heartbeat.c */
void *heartbeat_loop(void *arg);

/* signal_handler.c */
void signal_handler_install(void);

/*
** watchdog.c
**
** cfg doit déjà être rempli (args_parse appelé) AVANT cet appel : le
** watchdog en a besoin pour pouvoir notifier le serveur (PROTO_TYPE_KILLED)
** en cas d'arrêt illégitime de l'enfant, sans avoir à reparser argv.
*/
int watchdog_run(const fetcher_config_t *cfg);

/* stop_token.c */
void stop_token_set_pipe(int write_fd);
void stop_token_signal_legitimate_exit(void);

#endif /* FETCHER_H_ */
