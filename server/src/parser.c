/*
** parser.c -- wrapper serveur autour du protocole réseau
**
** message_parse() n'est qu'un fin wrapper sur protocol_parse_message() :
** on garde une séparation logique entre le format du protocole réseau
** (common/protocol.h, partagé avec fetcher/) et la logique métier du
** serveur (server.h). Si un jour le serveur doit faire du post-traitement
** spécifique sur un message reçu (validation supplémentaire, stats, ...),
** c'est ici qu'il faudra l'ajouter, sans toucher au protocole commun.
*/

#include "server.h"

int message_parse(const char *line, char *identifier, size_t identifier_size,
    char *type, size_t type_size, char *payload, size_t payload_size,
    size_t *payload_len)
{
    return protocol_parse_message(line, identifier, identifier_size, type,
        type_size, payload, payload_size, payload_len);
}
