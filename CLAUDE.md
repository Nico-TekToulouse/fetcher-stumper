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
- Lit périodiquement, depuis un offset suivi (pas de relecture du passé au démarrage,
  détection de rotation via l'inode), **en priorité** le fichier désigné par la variable
  d'environnement `FETCHER_CMDLOG` (mis en place par `fetcher/start-exam.sh`, voir
  ci-dessous), et seulement si elle est absente, `~/.bash_history`/`~/.zsh_history` (selon
  `$SHELL`) en fallback dégradé.
- **Pourquoi `FETCHER_CMDLOG` et pas directement `~/.bash_history`/`~/.zsh_history`** (bug
  réel rencontré et corrigé) : ces fichiers ne sont pas garantis append-only — bash/zsh
  peuvent les **réécrire** (pas seulement les étendre), notamment à cause d'options de
  déduplication (ex: zsh `HIST_IGNORE_ALL_DUPS` retire l'ancienne occurrence d'une commande
  répétée et la rajoute en fin de fichier). Une lecture par offset brut sur un tel fichier
  pouvait renvoyer des commandes en double, ou au contraire en sauter (typiquement une
  commande répétée). `FETCHER_CMDLOG` est un fichier dédié, rempli uniquement par un hook
  shell (`zshaddhistory` en zsh, `PROMPT_COMMAND`+`history 1` en bash, voir
  `fetcher/start-exam.sh`) qui écrit chaque commande exactement une fois au moment de son
  exécution — réellement append-only, donc fiable avec une lecture par offset.
- **Autre limitation connue, corrigée en même temps** : bash et zsh n'écrivent de toute
  façon l'historique sur disque qu'à la fermeture du shell par défaut, pas après chaque
  commande — `start-exam.sh` corrige aussi ce point (option `INC_APPEND_HISTORY`/hook
  immédiat selon le shell).
- Le buffer lu à chaque sondage est découpé en lignes, et **chaque commande est envoyée
  comme un message `CMD` séparé** (une ligne incomplète en fin de lecture est remise en
  attente plutôt qu'envoyée tronquée) — évite que plusieurs commandes tapées dans la même
  seconde ne soient fusionnées en un seul message.
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

`fetcher/start-exam.sh` : met en place le mécanisme fiable `FETCHER_CMDLOG` (voir
« fetcher/ ») avant de lancer le fetcher en arrière-plan. Doit être **sourcé**, pas exécuté
comme sous-process (sinon le hook/la variable d'environnement installés ne s'appliqueraient
qu'à ce sous-process, pas au shell réellement utilisé par l'étudiant). Concrètement :

- Crée un fichier temporaire unique (`mktemp`) et l'exporte dans `FETCHER_CMDLOG`.
- **zsh** : définit la fonction `zshaddhistory()`, hook natif appelé par zsh avec la
  commande complète en `$1` à chaque exécution, avant toute déduplication de son propre
  historique — écrit directement dans `FETCHER_CMDLOG`.
- **bash** : pas de hook natif aussi direct ; ajoute `history 1` (dernière entrée, texte
  nettoyé du numéro de ligne) à `FETCHER_CMDLOG` via `PROMPT_COMMAND`, donc une fois par
  commande exécutée — fonctionne même si `HISTCONTROL=ignoredups` empêche bash d'ajouter une
  nouvelle entrée à son propre historique pour une répétition.
- Supprime le fichier temporaire à la fermeture du shell (`trap ... EXIT`) : ce n'est qu'un
  tampon de transmission vers le serveur, pas une donnée conservée.
- Vérifié : le hook s'enregistre correctement (`whence -w zshaddhistory` → `function`) et le
  fichier grandit de façon strictement incrémentale à chaque commande (testé en session zsh
  simulée). La capture en vrai terminal interactif reste à confirmer par l'utilisateur (même
  limite de simulation que documentée précédemment).

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

- `fetcher/start-exam.sh`/`FETCHER_CMDLOG` (correctif des bugs de commandes dupliquées/
  manquantes et de l'écriture différée, voir « fetcher/ ») : le hook zsh s'enregistre
  correctement et le fichier grandit de façon strictement incrémentale à chaque commande en
  session zsh simulée, mais **pas encore confirmé par un vrai test interactif en terminal
  par l'utilisateur** — les outils de test automatisés disponibles ont des limites pour
  simuler fidèlement un vrai shell interactif (TTY réel, sandboxing imbriqué). À valider en
  conditions réelles avant un examen. Le chemin bash (`PROMPT_COMMAND`+`history 1`) n'a pas
  pu être testé du tout dans cet environnement, à vérifier en priorité si des étudiants
  utilisent bash.
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
