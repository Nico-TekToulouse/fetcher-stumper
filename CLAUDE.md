# fetcher-stumper

Outil de **surveillance d'examen** en deux parties : un **fetcher** (client, lancé sur les
machines des étudiants au début d'une épreuve, avec leur connaissance) qui collecte les
commandes tapées dans le terminal et les envoie à un **server** qui les stocke par identifiant
(hostname, ou nom choisi via une option du fetcher) et les affiche en temps réel dans une
interface ncurses à l'enseignant.

```
[poste étudiant A] fetcher ─┐
[poste étudiant B] fetcher ─┼──► server (TCP) ──► stockage par identifiant (mémoire) ──► TUI ncurses
[poste étudiant C] fetcher ─┘
```

Implémenté et testé de bout en bout (compilation propre `-Wall -Wextra`, scénarios
heartbeat/déconnexion/mot de passe/watchdog validés manuellement — voir « État »).

## Cadre d'usage (important)

- Usage prévu : surveillance d'examen sur machines gérées par l'école, avec les étudiants
  informés au préalable (politique d'examen communiquée avant l'épreuve).
- Le fetcher est **annoncé**, pas caché : il affiche clairement au démarrage qu'il est actif
  et pour quel identifiant, lancé via un script de début d'épreuve connu des étudiants.
- Protection anti-arrêt acceptée : interception de `SIGINT`/`SIGTERM` avec mot de passe
  enseignant requis pour un arrêt propre, + watchdog qui relance le fetcher si le process
  meurt sans ce mot de passe.
- Limite ferme, à ne jamais dépasser : **pas de résistance à `SIGKILL`**, pas de masquage du
  processus dans `ps`/gestionnaire de tâches, pas de technique de persistance façon rootkit.
  `SIGKILL` doit toujours fonctionner — c'est le dernier recours du propriétaire de la
  machine, et le contourner est une caractéristique de malware, hors de propos ici.
- Détection d'un arrêt forcé côté serveur par deux mécanismes complémentaires :
  1. **Notification active** : le watchdog, quand il constate que le fetcher est mort sans
     mot de passe valide (kill -9, crash), ouvre une brève connexion au serveur et envoie un
     message `KILLED` avant de relancer un nouveau fetcher — notification quasi instantanée.
  2. **Heartbeat/timeout** (filet de sécurité) : si le fetcher ne peut même plus se
     reconnecter (serveur injoignable, machine éteinte, coupure réseau), le serveur marque
     l'identifiant en alerte après `PROTO_HEARTBEAT_TIMEOUT_SEC` (6s) sans heartbeat reçu.
  - Pourquoi les deux : le watchdog relance le fetcher en général en moins d'une seconde, ce
    qui est plus rapide que le timeout de heartbeat — sans la notification active, l'alerte
    de timeout ne se déclencherait quasiment jamais pour un simple `kill -9`.

## Structure

```
common/    protocol.h + protocol.c : format de message partagé fetcher<->server (base64,
           framing par '\n', constantes TYPE/PROTO_*)
fetcher/   Client en C : lit l'historique shell, envoie au serveur, heartbeat,
           mot de passe + watchdog anti-kill
server/    Serveur en C : écoute TCP, registry des clients en mémoire, interface ncurses
```

## Protocole réseau

Un message par ligne, terminé par `\n` :

```
IDENTIFIANT|TYPE|BASE64(PAYLOAD)\n
```

- `IDENTIFIANT` : hostname ou nom `-n`/`--name`, sanitizé (`|`/`\n` → `_`).
- `TYPE` : `CMD`, `HEARTBEAT`, ou `KILLED` (notification watchdog, voir ci-dessus).
- `PAYLOAD` encodé en base64 standard (jamais de `\n`), vide pour un heartbeat.

Voir `common/protocol.h` pour les constantes et fonctions (`protocol_build_message`,
`protocol_parse_message`, `base64_encode`/`base64_decode`).

## fetcher/

- `-n`/`--name NOM` pour choisir l'identifiant (sinon `gethostname()`), `-h`/`--host` et
  `-p`/`--port` pour la cible serveur (défauts `127.0.0.1:4242`).
- Lit périodiquement `~/.bash_history` ou `~/.zsh_history` (selon `$SHELL`) depuis un offset
  suivi (pas de relecture de l'historique déjà présent au démarrage), détecte une rotation
  de fichier via l'inode.
- **Limitation connue et importante** : bash et zsh n'écrivent l'historique sur disque qu'à
  la fermeture du shell par défaut, pas après chaque commande — donc rien ne remonte tant
  que le terminal de l'étudiant reste ouvert (ce qui est le cas pendant tout un examen).
  Voir `fetcher/start-exam.sh` ci-dessous pour le correctif.
- Heartbeat toutes les `PROTO_HEARTBEAT_INTERVAL_SEC` (2s) dans un thread dédié.
- `SIGINT`/`SIGTERM` → demande le mot de passe enseignant (`FETCHER_TEACHER_PASSWORD`, voir
  « Build »). Bon mot de passe → arrêt propre signalé au watchdog. Mauvais mot de passe →
  poursuite.
- Watchdog (`fork()` dans `main()`, avant tout le reste) : le parent surveille l'enfant via
  `waitpid()` et un pipe anonyme signalant un arrêt légitime ; si l'enfant meurt sans ce
  signal, le watchdog notifie le serveur (`KILLED`) puis relance un nouveau fetcher.

## server/

- TCP (`network_listen`/`network_accept_loop`), un thread détaché par connexion cliente,
  bufferisation par client pour gérer le framing sur `\n`.
- `client_registry` : liste chaînée de clients en mémoire protégée par mutex, aucune écriture
  disque nulle part (vérifié par relecture + test manuel).
- `heartbeat_monitor` : thread qui marque un client en alerte après 6s sans heartbeat (filet
  de sécurité, voir « Cadre d'usage »).
- `whitelist` (optionnelle) : fichier texte d'identifiants autorisés (un par ligne,
  commentaires `#`, chargé une seule fois au démarrage). Si fourni, tout identifiant absent
  de la liste est rejeté (connexion TCP fermée immédiatement, aucune entrée créée dans le
  registry). Si omis, comportement inchangé : toute connexion acceptée.
- Interface **ncurses** (`ncurses_ui.c`/`ncurses_input.c`) : fenêtre gauche = liste des
  identifiants (vert connecté / rouge `[ALERT]`), fenêtre droite = flux des commandes du
  client sélectionné (horodaté), navigation `↑`/`↓`, `q` pour quitter l'UI. Limites
  d'affichage : 64 clients, 200 dernières commandes par client, lignes tronquées à 255
  caractères (purement pour l'affichage, pas pour les données en mémoire). Ligne d'aide en
  bas : affiche aussi le dernier rejet whitelist s'il y en a un.
- **Règle stricte depuis que ncurses est démarré (thread `ncurses_ui_loop`)** : aucun autre
  thread (réseau, heartbeat monitor) ne doit faire de `printf`/`fflush(stdout)` — ncurses
  contrôle alors tout l'écran du terminal, et un printf concurrent corrompt l'affichage
  (bug réel rencontré et corrigé : voir `server/src/last_event.c`). Tout événement à montrer
  à l'enseignant doit passer soit par le statut d'un client dans le registry (déjà lu par
  l'UI), soit par le mécanisme `last_event_*` pour un message ponctuel sans home naturel
  dans le registry (ex: rejet whitelist).

## Build

3 Makefiles (`Makefile` racine, `fetcher/Makefile`, `server/Makefile`), cibles `all`/`clean`/
`fclean`/`re`. `-Wall -Wextra` sans warning. `server/Makefile` utilise
`pkg-config --cflags/--libs ncurses` (disponible nativement via le SDK système sur macOS,
pas d'installation requise).

```sh
make        # compile fetcher/fetcher et server/server
./server/server [port] [whitelist_file]     # défaut port: 4242, whitelist: aucune (tout accepté)

# lancement direct du binaire (voir limitation historique ci-dessus/ci-dessous) :
FETCHER_TEACHER_PASSWORD=<mdp> ./fetcher/fetcher -n <nom> --host <ip> --port <port>

# lancement recommandé côté étudiant, à SOURCER (pas exécuter) depuis le shell
# qui sera surveillé, pour que l'option d'écriture immédiate de l'historique
# s'applique à CE shell (voir fetcher/start-exam.sh) :
source fetcher/start-exam.sh -n <nom> --host <ip> --port <port>
```

`fetcher/start-exam.sh` : corrige la limitation ci-dessus en activant `INC_APPEND_HISTORY`
(zsh) ou `PROMPT_COMMAND="history -a;..."` (bash) dans le shell courant avant de lancer le
fetcher en arrière-plan. Doit être **sourcé**, pas exécuté comme sous-process (sinon le
changement d'option shell ne s'appliquerait qu'à ce sous-process, pas au shell de
l'étudiant).

`whitelist_file` (optionnel) : un identifiant autorisé par ligne, lignes vides et
commentaires `#` ignorés. Exemple :

```
# étudiants autorisés pour l'examen du 2026-10-05
poste-salle-a-01
poste-salle-a-02
alice-dupont
```

Le mot de passe enseignant (demandé sur `SIGINT`/`SIGTERM` avant un arrêt propre du
fetcher) est lu depuis la variable d'environnement `FETCHER_TEACHER_PASSWORD` au démarrage.
Si elle n'est pas définie, un mot de passe par défaut (`exam2026`) est utilisé avec un
avertissement sur stderr — à ne jamais utiliser tel quel pour un examen réel.

## État

Implémenté et compilé sans warning. Testé manuellement de bout en bout (historique simulé
via `$HOME` isolé) : réception des commandes par le serveur, UI ncurses (liste + flux +
couleurs), arrêt légitime par mot de passe (watchdog ne relance pas, pas d'alerte), mauvais
mot de passe (poursuite), `kill -9` (watchdog relance + notification `KILLED` immédiate
visible dans l'UI). Pas de test automatisé (unit/CI) à ce stade.

## Points encore ouverts

- `fetcher/start-exam.sh` (correctif de la limitation d'écriture différée de l'historique,
  voir « fetcher/ ») : la logique (`setopt INC_APPEND_HISTORY`, `PROMPT_COMMAND`) a été
  vérifiée isolément, mais **pas confirmée par un vrai test interactif en terminal** — les
  outils de test automatisés disponibles ne peuvent pas simuler fidèlement l'écriture
  incrémentale de l'historique d'un vrai shell interactif (zsh/bash ne l'écrivent qu'en
  session TTY réelle, pas via `-c`/un script non-interactif, même avec l'option activée).
  À valider en conditions réelles avant un examen.
- Mot de passe enseignant externalisé via `FETCHER_TEACHER_PASSWORD` (décision actée, voir
  « Build »). Reste en clair en variable d'environnement (pas de hash) — acceptable pour
  l'usage actuel, à revoir si besoin d'un niveau de sécurité supérieur.
- Liste blanche de hostnames/identifiants implémentée (`server/src/whitelist.c`), optionnelle
  via un 2e argument CLI du serveur (voir « Build »). Limite de `WHITELIST_MAX_ENTRIES` (256)
  entrées chargées.
- Pas de persistance disque côté serveur (décision actée : en mémoire uniquement, rien
  n'est conservé après l'arrêt du serveur).
- Pas de Norme Epitech stricte imposée (décision actée : style C propre, `-Wall -Wextra`
  sans warning).
- `handle_stop_signal` (fetcher) appelle `fgets`/`fprintf` directement depuis un handler de
  signal : simple et fonctionnel en pratique pour cet usage, mais pas strictement
  async-signal-safe au sens POSIX — à garder en tête si le code évolue.
- Pas de backoff sur les tentatives de reconnexion du fetcher si le serveur est injoignable
  au démarrage (le watchdog le relance alors en boucle rapide).

## Git

- Commits et push sur ce repo sans ligne d'attribution Claude/Anthropic (pas de
  `Co-Authored-By`, pas de mention d'outil IA) — consigne explicite de l'utilisateur.
