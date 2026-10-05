/*
** protocol.c -- implémentation du format de message partagé fetcher/server
*/

#include <string.h>
#include <stdio.h>
#include "protocol.h"

static const char g_b64_alphabet[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

void protocol_sanitize_identifier(char *str)
{
    for (size_t i = 0; str[i] != '\0'; i++) {
        if (str[i] == '|' || str[i] == '\n')
            str[i] = '_';
    }
}

size_t base64_encode(const unsigned char *data, size_t len, char *out,
    size_t out_size)
{
    size_t needed = ((len + 2) / 3) * 4 + 1;
    size_t oi = 0;

    if (out_size < needed)
        return (size_t)-1;
    for (size_t i = 0; i < len; i += 3) {
        unsigned int chunk = (unsigned int)data[i] << 16;
        int have2 = (i + 1 < len);
        int have3 = (i + 2 < len);

        if (have2)
            chunk |= (unsigned int)data[i + 1] << 8;
        if (have3)
            chunk |= (unsigned int)data[i + 2];
        out[oi++] = g_b64_alphabet[(chunk >> 18) & 0x3F];
        out[oi++] = g_b64_alphabet[(chunk >> 12) & 0x3F];
        out[oi++] = have2 ? g_b64_alphabet[(chunk >> 6) & 0x3F] : '=';
        out[oi++] = have3 ? g_b64_alphabet[chunk & 0x3F] : '=';
    }
    out[oi] = '\0';
    return oi;
}

static int b64_value(char c)
{
    if (c >= 'A' && c <= 'Z')
        return c - 'A';
    if (c >= 'a' && c <= 'z')
        return c - 'a' + 26;
    if (c >= '0' && c <= '9')
        return c - '0' + 52;
    if (c == '+')
        return 62;
    if (c == '/')
        return 63;
    return -1;
}

size_t base64_decode(const char *in, unsigned char *out, size_t out_size)
{
    size_t len = strlen(in);
    size_t oi = 0;

    if (len % 4 != 0)
        return (size_t)-1;
    for (size_t i = 0; i < len; i += 4) {
        int v0 = b64_value(in[i]);
        int v1 = b64_value(in[i + 1]);
        int pad2 = (in[i + 2] == '=');
        int pad3 = (in[i + 3] == '=');
        int v2 = pad2 ? 0 : b64_value(in[i + 2]);
        int v3 = pad3 ? 0 : b64_value(in[i + 3]);

        if (v0 < 0 || v1 < 0 || (!pad2 && v2 < 0) || (!pad3 && v3 < 0))
            return (size_t)-1;
        if (oi + 1 > out_size)
            return (size_t)-1;
        out[oi++] = (unsigned char)((v0 << 2) | (v1 >> 4));
        if (!pad2) {
            if (oi + 1 > out_size)
                return (size_t)-1;
            out[oi++] = (unsigned char)(((v1 & 0xF) << 4) | (v2 >> 2));
        }
        if (!pad2 && !pad3) {
            if (oi + 1 > out_size)
                return (size_t)-1;
            out[oi++] = (unsigned char)(((v2 & 0x3) << 6) | v3);
        }
    }
    return oi;
}

int protocol_build_message(char *out, size_t out_size, const char *identifier,
    const char *type, const char *payload, size_t payload_len)
{
    char b64[PROTO_MAX_MESSAGE];
    size_t b64_len;
    int written;

    b64_len = base64_encode((const unsigned char *)payload, payload_len, b64,
        sizeof(b64));
    if (b64_len == (size_t)-1)
        return -1;
    written = snprintf(out, out_size, "%s|%s|%s\n", identifier, type, b64);
    if (written < 0 || (size_t)written >= out_size)
        return -1;
    return 0;
}

int protocol_parse_message(const char *line, char *identifier,
    size_t identifier_size, char *type, size_t type_size, char *payload,
    size_t payload_size, size_t *payload_len)
{
    const char *sep1 = strchr(line, '|');
    const char *sep2;
    size_t id_len;
    size_t type_len;
    size_t b64_len;
    size_t decoded;
    char b64[PROTO_MAX_MESSAGE];

    if (sep1 == NULL)
        return -1;
    sep2 = strchr(sep1 + 1, '|');
    if (sep2 == NULL)
        return -1;
    id_len = (size_t)(sep1 - line);
    type_len = (size_t)(sep2 - sep1 - 1);
    b64_len = strlen(sep2 + 1);
    if (id_len >= identifier_size || type_len >= type_size ||
        b64_len >= sizeof(b64))
        return -1;
    memcpy(identifier, line, id_len);
    identifier[id_len] = '\0';
    memcpy(type, sep1 + 1, type_len);
    type[type_len] = '\0';
    memcpy(b64, sep2 + 1, b64_len);
    b64[b64_len] = '\0';
    if (b64_len == 0) {
        payload[0] = '\0';
        *payload_len = 0;
        return 0;
    }
    decoded = base64_decode(b64, (unsigned char *)payload, payload_size - 1);
    if (decoded == (size_t)-1)
        return -1;
    payload[decoded] = '\0';
    *payload_len = decoded;
    return 0;
}
