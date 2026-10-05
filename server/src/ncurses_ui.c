/*
** ncurses_ui.c -- interface ncurses temps reel de surveillance d'examen.
**
** Deux fenetres :
**   - gauche (~30% de la largeur) : liste des identifiants connectes.
**   - droite (~70% de la largeur) : flux des commandes du client selectionne.
**
** Limites documentees :
**   - MAX_DISPLAYED_CLIENTS clients affiches au maximum (au-dela, les
**     clients supplementaires ne sont simplement pas montres).
**   - MAX_DISPLAYED_COMMANDS dernieres commandes du client selectionne
**     copiees et affichees au maximum (scroll "N dernieres lignes").
**
** Verrouillage : reg->lock est pris uniquement le temps de copier les
** donnees necessaires (identifiants/statuts des clients, commandes du
** client selectionne) dans des buffers locaux, puis relache avant tout
** appel ncurses. getch() est configure en mode "timeout court" (non
** bloquant au-dela de REFRESH_MS) afin de permettre un rafraichissement
** periodique de l'affichage sans jamais garder le lock pendant un appel
** ncurses potentiellement bloquant.
**
** Sortie de l'UI : quand l'utilisateur appuie sur 'q'/'Q',
** ncurses_input_handle() retourne 1 et la boucle ci-dessous sort
** proprement (endwin() puis return). Par choix, ctx->running n'est PAS
** modifie ici : l'arret global du process est gere ailleurs (ce fichier
** ne doit toucher a aucune autre logique du serveur).
*/

#include <ncurses.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "server.h"

#define MAX_DISPLAYED_CLIENTS 64
#define MAX_DISPLAYED_COMMANDS 200
#define MAX_CMD_LINE_LEN 256
#define REFRESH_MS 200
#define LEFT_WIDTH_PERCENT 30

typedef struct client_snapshot {
    char identifier[PROTO_MAX_IDENTIFIER];
    client_status_t status;
} client_snapshot_t;

typedef struct command_snapshot {
    char text[MAX_CMD_LINE_LEN];
    time_t received_at;
} command_snapshot_t;

static void init_colors(void)
{
    if (!has_colors())
        return;
    start_color();
    init_pair(1, COLOR_GREEN, COLOR_BLACK);
    init_pair(2, COLOR_RED, COLOR_BLACK);
}

/* Copie sous lock, dans `out` (capacite `max`), les identifiants et
** statuts des clients de la registry, dans l'ordre de la liste chainee.
** Retourne le nombre de clients copies. */
static size_t snapshot_clients(client_registry_t *reg, client_snapshot_t *out,
    size_t max)
{
    size_t n = 0;
    client_t *cur;

    pthread_mutex_lock(&reg->lock);
    for (cur = reg->head; cur != NULL && n < max; cur = cur->next) {
        strncpy(out[n].identifier, cur->identifier, PROTO_MAX_IDENTIFIER - 1);
        out[n].identifier[PROTO_MAX_IDENTIFIER - 1] = '\0';
        out[n].status = cur->status;
        n++;
    }
    pthread_mutex_unlock(&reg->lock);
    return n;
}

/* Sous lock, retrouve le client a la position `index` (0-based, ordre de
** la liste chainee) et copie dans `out` (capacite `max`) ses dernieres
** commandes (les plus recentes), dans l'ordre chronologique. Retourne le
** nombre de commandes copiees (0 si index invalide ou client absent). */
static size_t snapshot_commands(client_registry_t *reg, int index,
    command_snapshot_t *out, size_t max)
{
    client_t *cur;
    command_entry_t *entry;
    int i = 0;
    size_t total;
    size_t skip;
    size_t n = 0;

    if (index < 0)
        return 0;
    pthread_mutex_lock(&reg->lock);
    for (cur = reg->head; cur != NULL; cur = cur->next, i++) {
        if (i == index)
            break;
    }
    if (cur == NULL) {
        pthread_mutex_unlock(&reg->lock);
        return 0;
    }
    total = cur->command_count;
    skip = total > max ? total - max : 0;
    i = 0;
    for (entry = cur->commands_head; entry != NULL; entry = entry->next, i++) {
        if ((size_t)i < skip)
            continue;
        strncpy(out[n].text, entry->text, MAX_CMD_LINE_LEN - 1);
        out[n].text[MAX_CMD_LINE_LEN - 1] = '\0';
        out[n].received_at = entry->received_at;
        n++;
    }
    pthread_mutex_unlock(&reg->lock);
    return n;
}

static void draw_clients(WINDOW *win, client_snapshot_t *clients,
    size_t count, int selected)
{
    int h;
    int w;
    size_t i;
    int attr;
    int color;

    if (win == NULL)
        return;
    getmaxyx(win, h, w);
    (void)w;
    werase(win);
    box(win, 0, 0);
    mvwprintw(win, 0, 2, " Clients ");
    for (i = 0; i < count && (int)i < h - 2; i++) {
        color = clients[i].status == CLIENT_CONNECTED ? 1 : 2;
        attr = COLOR_PAIR(color);
        if ((int)i == selected)
            attr |= A_REVERSE;
        wattron(win, attr);
        if (clients[i].status == CLIENT_ALERT_DISCONNECTED)
            mvwprintw(win, (int)i + 1, 1, "[ALERT] %s", clients[i].identifier);
        else
            mvwprintw(win, (int)i + 1, 1, "%s", clients[i].identifier);
        wattroff(win, attr);
    }
}

static void draw_commands(WINDOW *win, command_snapshot_t *cmds,
    size_t count, const char *identifier)
{
    int h;
    int w;
    size_t start;
    size_t avail;
    size_t i;
    int line;
    char timebuf[16];
    struct tm tmres;

    if (win == NULL)
        return;
    getmaxyx(win, h, w);
    (void)w;
    werase(win);
    box(win, 0, 0);
    if (identifier != NULL)
        mvwprintw(win, 0, 2, " Commandes: %s ", identifier);
    else
        mvwprintw(win, 0, 2, " Commandes ");
    if (h <= 2)
        return;
    avail = (size_t)(h - 2);
    start = count > avail ? count - avail : 0;
    line = 1;
    for (i = start; i < count; i++) {
        localtime_r(&cmds[i].received_at, &tmres);
        strftime(timebuf, sizeof(timebuf), "%H:%M:%S", &tmres);
        mvwprintw(win, line, 1, "[%s] %s", timebuf, cmds[i].text);
        line++;
    }
}

static void draw_help_line(server_context_t *ctx)
{
    static const char help[] =
        "Up/Down: selection du client   q: quitter l'interface ncurses";
    char rejection[160];
    char line[256];

    if (LINES < 1)
        return;
    mvhline(LINES - 1, 0, ' ', COLS);
    if (last_event_get(&ctx->last_rejection, rejection, sizeof(rejection))) {
        snprintf(line, sizeof(line), "%s   |   %s", help, rejection);
        mvprintw(LINES - 1, 0, "%.*s", COLS, line);
    } else {
        mvprintw(LINES - 1, 0, "%.*s", COLS, help);
    }
}

static void compute_layout(int *left_w, int *right_w, int *body_h)
{
    *body_h = LINES - 1;
    if (*body_h < 1)
        *body_h = 1;
    *left_w = COLS * LEFT_WIDTH_PERCENT / 100;
    if (*left_w < 1)
        *left_w = 1;
    if (*left_w >= COLS)
        *left_w = COLS > 1 ? COLS - 1 : 1;
    *right_w = COLS - *left_w;
    if (*right_w < 1)
        *right_w = 1;
}

void *ncurses_ui_loop(void *arg)
{
    server_context_t *ctx = (server_context_t *)arg;
    client_snapshot_t clients[MAX_DISPLAYED_CLIENTS];
    command_snapshot_t commands[MAX_DISPLAYED_COMMANDS];
    int selected_index = -1;
    WINDOW *left_win;
    WINDOW *right_win;
    int left_w;
    int right_w;
    int body_h;
    int ch;
    size_t client_count;
    size_t cmd_count;

    initscr();
    noecho();
    cbreak();
    curs_set(0);
    keypad(stdscr, TRUE);
    init_colors();
    timeout(REFRESH_MS);

    while (ctx->running) {
        compute_layout(&left_w, &right_w, &body_h);
        left_win = newwin(body_h, left_w, 0, 0);
        right_win = newwin(body_h, right_w, 0, left_w);

        ch = getch();
        if (ch != ERR &&
            ncurses_input_handle(ch, &ctx->registry, &selected_index) == 1) {
            delwin(left_win);
            delwin(right_win);
            break;
        }

        client_count = snapshot_clients(&ctx->registry, clients,
            MAX_DISPLAYED_CLIENTS);
        if (client_count == 0)
            selected_index = -1;
        else if (selected_index < 0)
            selected_index = 0;
        else if (selected_index >= (int)client_count)
            selected_index = (int)client_count - 1;

        draw_clients(left_win, clients, client_count, selected_index);

        cmd_count = 0;
        if (selected_index >= 0)
            cmd_count = snapshot_commands(&ctx->registry, selected_index,
                commands, MAX_DISPLAYED_COMMANDS);
        draw_commands(right_win, commands, cmd_count,
            selected_index >= 0 ? clients[selected_index].identifier : NULL);

        draw_help_line(ctx);

        wnoutrefresh(stdscr);
        if (left_win != NULL)
            wnoutrefresh(left_win);
        if (right_win != NULL)
            wnoutrefresh(right_win);
        doupdate();

        delwin(left_win);
        delwin(right_win);
    }

    endwin();
    return NULL;
}
