#!/bin/sh
# start-exam.sh -- A SOURCER (pas executer) en debut d'epreuve :
#
#     source fetcher/start-exam.sh -n <nom> --host <ip> --port <port>
#
# Pourquoi ce script existe : s'appuyer sur ~/.bash_history ou
# ~/.zsh_history directement est peu fiable. Ces fichiers ne sont pas
# garantis "append-only" : bash/zsh peuvent les REECRIRE (pas seulement les
# etendre), par exemple a cause d'options de deduplication (zsh
# HIST_IGNORE_ALL_DUPS retire l'ancienne occurrence d'une commande repetee
# et la rajoute a la fin) ou de leur sauvegarde periodique. Une lecture par
# offset brut sur un tel fichier peut alors renvoyer des commandes en
# double, ou au contraire en sauter (typiquement une commande repetee).
#
# Ce script met en place un mecanisme fiable a la place : un hook shell qui
# ecrit CHAQUE commande, au moment exact de son execution, dans un fichier
# dedie que NOUS controlons entierement (jamais touche par les mecanismes
# d'historique de bash/zsh) -- donc reellement append-only. Le fetcher lit
# ce fichier (variable FETCHER_CMDLOG) au lieu de l'historique brut du
# shell.
#
# IMPORTANT : ce script doit etre SOURCE (pas execute en sous-process),
# sinon le hook installe (zshaddhistory / PROMPT_COMMAND) ne s'appliquerait
# qu'a ce sous-process et pas au shell reellement utilise par l'etudiant.
#
# Transparence : le fetcher reste un processus normal, visible via `ps`/
# `jobs` -- ce script ne fait qu'automatiser la configuration prealable.
# Le fichier de log temporaire est supprime a la fermeture du shell (trap
# EXIT) -- ce n'est qu'un tampon de transmission vers le serveur, pas une
# donnee conservee (le serveur lui-meme ne persiste rien sur disque).

if [ -n "$ZSH_VERSION" ]; then
    _fetcher_script_dir="${0:A:h}"
elif [ -n "$BASH_VERSION" ]; then
    _fetcher_script_dir="$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
else
    echo "start-exam.sh: shell non reconnu (ni zsh ni bash) -- suivi fiable" \
        "non disponible, le fetcher utilisera un fallback degrade" >&2
    _fetcher_script_dir="$(pwd)"
fi

FETCHER_CMDLOG="$(mktemp "${TMPDIR:-/tmp}/fetcher_cmdlog.XXXXXX")"
export FETCHER_CMDLOG

if [ -n "$ZSH_VERSION" ]; then
    # zshaddhistory recoit en $1 la ligne de commande complete (avec son
    # retour a la ligne final), AVANT toute deduplication appliquee par zsh
    # a son propre HISTFILE : on capture donc chaque execution exactement
    # une fois, independamment de la config d'historique de l'etudiant.
    # "return 0" laisse zsh traiter l'historique normalement par ailleurs.
    # Definition directe (pas de add-zsh-hook) : plus simple et plus
    # robuste -- un seul hook nous interesse ici, pas besoin de la gestion
    # multi-hooks d'add-zsh-hook, qui depend d'un fpath correctement
    # configure (pas garanti selon la config de l'etudiant).
    zshaddhistory() {
        printf '%s' "$1" >> "$FETCHER_CMDLOG"
        return 0
    }
elif [ -n "$BASH_VERSION" ]; then
    # Pas de hook "avant execution" simple en bash : on ajoute la derniere
    # entree d'historique (`history 1`) au log a chaque affichage de prompt,
    # donc une fois par commande executee. Le numero d'entree en tete est
    # retire. Fonctionne meme si HISTCONTROL=ignoredups empeche bash
    # d'ajouter une NOUVELLE entree pour une repetition : `history 1`
    # renvoie alors toujours le texte de la derniere commande executee, qui
    # est bien ce qu'on veut enregistrer.
    _fetcher_log_last_cmd() {
        history 1 | sed -e 's/^[[:space:]]*[0-9]*[[:space:]]*//' >> "$FETCHER_CMDLOG"
    }
    case ":$PROMPT_COMMAND:" in
        *":_fetcher_log_last_cmd:"*) ;;
        *) PROMPT_COMMAND="_fetcher_log_last_cmd;${PROMPT_COMMAND}" ;;
    esac
fi

trap 'rm -f "$FETCHER_CMDLOG"' EXIT

"${_fetcher_script_dir}/fetcher" "$@" &
echo "fetcher: lance en arriere-plan (pid $!) -- processus visible," \
    "verifiable avec 'jobs' ou 'ps'"

unset _fetcher_script_dir
