/*
** protocol.h -- format de message partagé entre fetcher et server
**
** Format d'un message sur le fil (un message par ligne) :
**
**     IDENTIFIANT|TYPE|BASE64(PAYLOAD)\n
**
** - IDENTIFIANT : hostname ou nom choisi via -n/--name côté fetcher, sans '|' ni '\n'
**   (sanitizé à l'émission : tout '|' ou '\n' est remplacé par '_').
** - TYPE         : PROTO_TYPE_CMD ou PROTO_TYPE_HEARTBEAT.
** - PAYLOAD      : encodé en base64 standard ([A-Za-z0-9+/=], jamais de '\n'), pour
**   supporter sans ambiguïté des commandes contenant '|' ou des retours à la ligne.
**   Peut être vide pour un heartbeat.
**
** Ce header est inclus par fetcher/ et server/ via -I../common. Il ne doit être
** modifié que de façon coordonnée entre les deux côtés.
*/

#ifndef PROTOCOL_H_
#define PROTOCOL_H_

#include <stddef.h>

#define PROTO_TYPE_CMD "CMD"
#define PROTO_TYPE_HEARTBEAT "HEARTBEAT"
/*
** PROTO_TYPE_KILLED : envoyé une seule fois par le watchdog (pas par le
** fetcher lui-même, qui est déjà mort) juste avant de relancer un nouveau
** fetcher suite à un arrêt non légitime (kill -9, crash...). Permet au
** serveur d'être notifié immédiatement, sans attendre un timeout de
** heartbeat qui n'arriverait jamais si le redémarrage est plus rapide que
** PROTO_HEARTBEAT_TIMEOUT_SEC.
*/
#define PROTO_TYPE_KILLED "KILLED"

#define PROTO_MAX_IDENTIFIER 256
#define PROTO_MAX_MESSAGE 4096
#define PROTO_DEFAULT_PORT 4242
#define PROTO_HEARTBEAT_INTERVAL_SEC 2
#define PROTO_HEARTBEAT_TIMEOUT_SEC 6

/*
** Remplace tout '|' et '\n' de `str` par '_', en place. Utilisé pour sanitizer
** l'identifiant avant de construire un message.
*/
void protocol_sanitize_identifier(char *str);

/*
** Construit un message complet "IDENTIFIANT|TYPE|BASE64(PAYLOAD)\n" dans `out`
** (buffer fourni par l'appelant, de taille `out_size`).
** Retourne 0 en cas de succès, -1 si le buffer est trop petit.
*/
int protocol_build_message(char *out, size_t out_size, const char *identifier,
    const char *type, const char *payload, size_t payload_len);

/*
** Parse un message "IDENTIFIANT|TYPE|BASE64(PAYLOAD)\n" (sans le '\n' final,
** déjà retiré par l'appelant). Remplit identifier/type/payload (buffers fournis
** par l'appelant) et payload_len. Retourne 0 en cas de succès, -1 sinon.
*/
int protocol_parse_message(const char *line, char *identifier,
    size_t identifier_size, char *type, size_t type_size, char *payload,
    size_t payload_size, size_t *payload_len);

/*
** Encode `len` octets de `data` en base64 standard dans `out` (buffer fourni par
** l'appelant, de taille `out_size`, doit contenir la place pour le '\0' final).
** Retourne la longueur de la chaîne encodée (hors '\0'), ou (size_t)-1 si
** `out_size` est insuffisant.
*/
size_t base64_encode(const unsigned char *data, size_t len, char *out,
    size_t out_size);

/*
** Décode une chaîne base64 standard `in` (terminée par '\0') dans `out` (buffer
** fourni par l'appelant, de taille `out_size`). Retourne le nombre d'octets
** décodés, ou (size_t)-1 si `out_size` est insuffisant ou si `in` est invalide.
*/
size_t base64_decode(const char *in, unsigned char *out, size_t out_size);

#endif /* PROTOCOL_H_ */
