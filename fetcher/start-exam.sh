#!/bin/sh
# start-exam.sh -- A SOURCER (pas executer) en debut d'epreuve :
#
#     source fetcher/start-exam.sh -n <nom> --host <ip> --port <port>
#
# Pourquoi ce script existe : bash et zsh n'ecrivent l'historique sur disque
# (~/.bash_history ou ~/.zsh_history) qu'a la fermeture du shell par defaut,
# pas apres chaque commande. Comme le fetcher lit ce fichier sur disque, il
# ne verrait donc aucune commande tant que le terminal de l'etudiant reste
# ouvert -- ce qui est precisement le cas pendant tout un examen. Ce script,
# une fois SOURCE (pas execute en sous-process, sinon le changement
# d'option ne s'appliquerait qu'a ce sous-process et pas au shell de
# l'etudiant), active l'ecriture immediate de l'historique dans le shell
# courant, puis lance le fetcher en arriere-plan avec les arguments passes.
#
# Transparence : le fetcher reste un processus normal, visible via `ps`/
# `jobs` -- ce script ne fait qu'automatiser la configuration prealable.

if [ -n "$ZSH_VERSION" ]; then
    setopt INC_APPEND_HISTORY
    _fetcher_script_dir="${0:A:h}"
elif [ -n "$BASH_VERSION" ]; then
    case ":$PROMPT_COMMAND:" in
        *":history -a:"*) ;;
        *) PROMPT_COMMAND="history -a;${PROMPT_COMMAND}" ;;
    esac
    _fetcher_script_dir="$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
else
    echo "start-exam.sh: shell non reconnu (ni zsh ni bash) -- configurez" \
        "manuellement l'ecriture immediate de l'historique avant de lancer" \
        "le fetcher" >&2
    _fetcher_script_dir=""
fi

if [ -z "$_fetcher_script_dir" ]; then
    _fetcher_script_dir="$(pwd)"
fi

"${_fetcher_script_dir}/fetcher" "$@" &
echo "fetcher: lance en arriere-plan (pid $!) -- processus visible," \
    "verifiable avec 'jobs' ou 'ps'"

unset _fetcher_script_dir
