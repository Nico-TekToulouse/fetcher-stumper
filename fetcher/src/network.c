/*
** network.c -- connexion TCP au serveur et envoi de messages du protocole
*/

#include "fetcher.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>

int network_connect(const fetcher_config_t *cfg)
{
    struct addrinfo hints;
    struct addrinfo *res;
    struct addrinfo *rp;
    char port_str[16];
    int sockfd;
    int ret;

    if (cfg == NULL)
        return -1;

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    snprintf(port_str, sizeof(port_str), "%d", cfg->server_port);

    ret = getaddrinfo(cfg->server_host, port_str, &hints, &res);
    if (ret != 0) {
        fprintf(stderr, "network_connect: getaddrinfo: %s\n",
            gai_strerror(ret));
        return -1;
    }

    sockfd = -1;
    for (rp = res; rp != NULL; rp = rp->ai_next) {
        sockfd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (sockfd == -1)
            continue;
        if (connect(sockfd, rp->ai_addr, rp->ai_addrlen) == 0)
            break;
        close(sockfd);
        sockfd = -1;
    }
    freeaddrinfo(res);

    if (sockfd == -1)
        perror("network_connect: connect");

    return sockfd;
}

int network_send_message(int sockfd, const char *identifier, const char *type,
    const char *payload, size_t payload_len)
{
    char msg[PROTO_MAX_MESSAGE];
    size_t len;
    size_t sent;
    ssize_t n;

    if (protocol_build_message(msg, sizeof(msg), identifier, type, payload,
            payload_len) != 0)
        return -1;

    len = strlen(msg);
    sent = 0;
    while (sent < len) {
        n = send(sockfd, msg + sent, len - sent, 0);
        if (n < 0) {
            if (errno == EINTR)
                continue;
            return -1;
        }
        if (n == 0)
            return -1;
        sent += (size_t)n;
    }

    return 0;
}
