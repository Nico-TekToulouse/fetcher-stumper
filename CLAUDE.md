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
- Heartbeat toutes les `PROTO_HEARTBEAT_INTERVAL_SEC` (2s) dans un thread dédié.
- `SIGINT`/`SIGTERM` → demande le mot de passe enseignant (actuellement en dur dans
  `signal_handler.c`, `TODO` documenté pour l'externaliser — voir « Points encore ouverts »).
  Bon mot de passe → arrêt propre signalé au watchdog. Mauvais mot de passe → poursuite.
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
- Interface **ncurses** (`ncurses_ui.c`/`ncurses_input.c`) : fenêtre gauche = liste des
  identifiants (vert connecté / rouge `[ALERT]`), fenêtre droite = flux des commandes du
  client sélectionné (horodaté), navigation `↑`/`↓`, `q` pour quitter l'UI. Limites
  d'affichage : 64 clients, 200 dernières commandes par client, lignes tronquées à 255
  caractères (purement pour l'affichage, pas pour les données en mémoire).

## Build

3 Makefiles (`Makefile` racine, `fetcher/Makefile`, `server/Makefile`), cibles `all`/`clean`/
`fclean`/`re`. `-Wall -Wextra` sans warning. `server/Makefile` utilise
`pkg-config --cflags/--libs ncurses` (disponible nativement via le SDK système sur macOS,
pas d'installation requise).

```sh
make        # compile fetcher/fetcher et server/server
./server/server [port]              # défaut: 4242
FETCHER_TEACHER_PASSWORD=<mdp> ./fetcher/fetcher -n <nom> --host <ip> --port <port>
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

- Mot de passe enseignant externalisé via `FETCHER_TEACHER_PASSWORD` (décision actée, voir
  « Build »). Reste en clair en variable d'environnement (pas de hash) — acceptable pour
  l'usage actuel, à revoir si besoin d'un niveau de sécurité supérieur.
- Pas de liste blanche de hostnames/identifiants autorisés à se connecter (décision actée :
  non implémenté pour l'instant, le serveur accepte toute connexion entrante).
- Pas de persistance disque côté serveur (décision actée : en mémoire uniquement, rien
  n'est conservé après l'arrêt du serveur).
- Pas de Norme Epitech stricte imposée (décision actée : style C propre, `-Wall -Wextra`
  sans warning).
- `handle_stop_signal` (fetcher) appelle `fgets`/`fprintf` directement depuis un handler de
  signal : simple et fonctionnel en pratique pour cet usage, mais pas strictement
  async-signal-safe au sens POSIX — à garder en tête si le code évolue.
- Pas de backoff sur les tentatives de reconnexion du fetcher si le serveur est injoignable
  au démarrage (le watchdog le relance alors en boucle rapide).
