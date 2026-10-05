/*
** network.c -- écoute TCP et gestion des connexions clients
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include "server.h"

#define RECV_CHUNK 4096

typedef struct client_thread_arg {
    int fd;
    server_context_t *ctx;
} client_thread_arg_t;

int network_listen(int port)
{
    int fd;
    int opt;
    struct sockaddr_in addr;

    fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        perror("network_listen: socket");
        return -1;
    }
    opt = 1;
    if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        perror("network_listen: setsockopt");
        close(fd);
        return -1;
    }
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons((uint16_t)port);
    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("network_listen: bind");
        close(fd);
        return -1;
    }
    if (listen(fd, 16) < 0) {
        perror("network_listen: listen");
        close(fd);
        return -1;
    }
    return fd;
}

/*
** Traite une ligne complète reçue d'un client (sans le '\n' final) :
** parse le message, identifie/crée le client dans le registry, et agit
** selon le type (HEARTBEAT ou CMD).
*/
static void handle_line(const char *line, int fd, server_context_t *ctx)
{
    char identifier[PROTO_MAX_IDENTIFIER];
    char type[32];
    char payload[PROTO_MAX_MESSAGE];
    size_t payload_len;
    client_t *client;

    if (message_parse(line, identifier, sizeof(identifier), type,
        sizeof(type), payload, sizeof(payload), &payload_len) != 0)
        return;
    client = client_registry_find_or_create(&ctx->registry, identifier);
    if (client == NULL)
        return;
    client->sockfd = fd;
    if (strcmp(type, PROTO_TYPE_HEARTBEAT) == 0) {
        client_registry_touch_heartbeat(client);
    } else if (strcmp(type, PROTO_TYPE_CMD) == 0) {
        client_registry_touch_heartbeat(client);
        client_registry_add_command(&ctx->registry, client, payload);
        printf("[%s] %s\n", identifier, payload);
        fflush(stdout);
    } else if (strcmp(type, PROTO_TYPE_KILLED) == 0) {
        /*
        ** Notification ponctuelle envoyée par le watchdog d'un fetcher
        ** (pas le fetcher lui-même, déjà mort) juste avant un redémarrage
        ** suite à un arrêt illégitime (kill -9, crash...). On ne touche
        ** pas le heartbeat ici : le but est de marquer l'alerte
        ** immédiatement, sans attendre/masquer via un heartbeat récent.
        */
        client_registry_mark_alert(client);
        printf("[ALERT] %s : %s\n", identifier, payload);
        fflush(stdout);
    }
}

/*
** Thread dédié à une connexion cliente : lit le socket en boucle avec un
** buffer accumulateur (recv() ne garantit pas de recevoir un message
** complet par appel), découpe sur '\n' et traite chaque ligne complète.
*/
static void *client_thread(void *arg)
{
    client_thread_arg_t *cta;
    int fd;
    server_context_t *ctx;
    char buf[RECV_CHUNK];
    char acc[PROTO_MAX_MESSAGE * 2];
    size_t acc_len;
    ssize_t received;
    char *nl;
    size_t line_len;

    cta = (client_thread_arg_t *)arg;
    fd = cta->fd;
    ctx = cta->ctx;
    free(cta);
    acc_len = 0;
    while (1) {
        received = recv(fd, buf, sizeof(buf), 0);
        if (received <= 0)
            break;
        if (acc_len + (size_t)received >= sizeof(acc)) {
            /* ligne/accumulation trop grande : on réinitialise le buffer */
            acc_len = 0;
            continue;
        }
        memcpy(acc + acc_len, buf, (size_t)received);
        acc_len += (size_t)received;
        acc[acc_len] = '\0';
        while ((nl = memchr(acc, '\n', acc_len)) != NULL) {
            line_len = (size_t)(nl - acc);
            acc[line_len] = '\0';
            handle_line(acc, fd, ctx);
            memmove(acc, nl + 1, acc_len - line_len - 1);
            acc_len -= line_len + 1;
        }
    }
    close(fd);
    return NULL;
}

void *network_accept_loop(void *arg)
{
    server_context_t *ctx;
    int client_fd;
    struct sockaddr_in client_addr;
    socklen_t client_addr_len;
    pthread_t tid;
    client_thread_arg_t *cta;

    ctx = (server_context_t *)arg;
    while (ctx->running) {
        client_addr_len = sizeof(client_addr);
        client_fd = accept(ctx->listen_fd, (struct sockaddr *)&client_addr,
            &client_addr_len);
        if (client_fd < 0) {
            if (!ctx->running)
                break;
            continue;
        }
        cta = malloc(sizeof(*cta));
        if (cta == NULL) {
            close(client_fd);
            continue;
        }
        cta->fd = client_fd;
        cta->ctx = ctx;
        if (pthread_create(&tid, NULL, client_thread, cta) != 0) {
            free(cta);
            close(client_fd);
            continue;
        }
        pthread_detach(tid);
    }
    return NULL;
}
