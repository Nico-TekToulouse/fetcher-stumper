/*
** args.c -- parsing des options CLI du fetcher
*/

#include "fetcher.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void usage(const char *prog)
{
    fprintf(stderr,
        "usage: %s [-n|--name NOM] [-h|--host HOST] [-p|--port PORT]\n",
        prog);
}

static int set_identifier(fetcher_config_t *cfg, const char *value)
{
    strncpy(cfg->identifier, value, sizeof(cfg->identifier) - 1);
    cfg->identifier[sizeof(cfg->identifier) - 1] = '\0';
    return 0;
}

static int set_host(fetcher_config_t *cfg, const char *value)
{
    strncpy(cfg->server_host, value, sizeof(cfg->server_host) - 1);
    cfg->server_host[sizeof(cfg->server_host) - 1] = '\0';
    return 0;
}

static int set_port(fetcher_config_t *cfg, const char *value)
{
    int port;

    port = atoi(value);
    if (port <= 0 || port > 65535)
        return -1;
    cfg->server_port = port;
    return 0;
}

static int apply_default_identifier(fetcher_config_t *cfg)
{
    char hostname[256];

    if (cfg->identifier[0] != '\0')
        return 0;

    if (gethostname(hostname, sizeof(hostname)) != 0) {
        strncpy(hostname, "unknown-host", sizeof(hostname) - 1);
        hostname[sizeof(hostname) - 1] = '\0';
    }
    hostname[sizeof(hostname) - 1] = '\0';
    strncpy(cfg->identifier, hostname, sizeof(cfg->identifier) - 1);
    cfg->identifier[sizeof(cfg->identifier) - 1] = '\0';
    return 0;
}

int args_parse(int argc, char **argv, fetcher_config_t *cfg)
{
    int i;

    if (cfg == NULL)
        return -1;

    memset(cfg, 0, sizeof(*cfg));
    strncpy(cfg->server_host, "127.0.0.1", sizeof(cfg->server_host) - 1);
    cfg->server_port = PROTO_DEFAULT_PORT;
    cfg->sockfd = -1;

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-n") == 0 || strcmp(argv[i], "--name") == 0) {
            if (i + 1 >= argc) {
                usage(argv[0]);
                return -1;
            }
            set_identifier(cfg, argv[++i]);
        } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--host") == 0) {
            if (i + 1 >= argc) {
                usage(argv[0]);
                return -1;
            }
            set_host(cfg, argv[++i]);
        } else if (strcmp(argv[i], "-p") == 0 || strcmp(argv[i], "--port") == 0) {
            if (i + 1 >= argc) {
                usage(argv[0]);
                return -1;
            }
            if (set_port(cfg, argv[++i]) != 0) {
                fprintf(stderr, "args_parse: port invalide: %s\n", argv[i]);
                return -1;
            }
        } else {
            fprintf(stderr, "args_parse: option inconnue: %s\n", argv[i]);
            usage(argv[0]);
            return -1;
        }
    }

    apply_default_identifier(cfg);
    protocol_sanitize_identifier(cfg->identifier);

    return 0;
}
