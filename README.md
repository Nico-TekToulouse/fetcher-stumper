# fetcher-stumper

Outil de surveillance d'examen en deux parties :

- **fetcher** : lancé sur le poste d'un étudiant (ouvertement, avec sa connaissance), lit
  les commandes tapées dans son terminal et les envoie à un serveur.
- **server** : reçoit les commandes de tous les fetchers connectés, les affiche en temps
  réel dans une interface terminal (ncurses), classées par identifiant.

```
[poste étudiant A] fetcher ─┐
[poste étudiant B] fetcher ─┼──► server (TCP) ──► stockage en mémoire ──► interface ncurses
[poste étudiant C] fetcher ─┘
```

Tout est gardé **en mémoire côté serveur** : rien n'est écrit sur disque, rien ne survit à
l'arrêt du serveur.

## Cadre d'usage

Cet outil est prévu pour une surveillance d'examen **annoncée** : les étudiants sont
informés à l'avance qu'un fetcher tourne sur leur poste pendant l'épreuve. Le fetcher
affiche clairement au démarrage qu'il est actif et sous quel identifiant. Il peut être
arrêté normalement avec `kill -9` (ce n'est volontairement pas empêché) ; un redémarrage
automatique et une notification au serveur sont en place en cas d'arrêt non prévu, mais
rien ne contourne le contrôle final du propriétaire de la machine sur ses propres
processus.

## Build

Nécessite un compilateur C et ncurses (déjà présent sur macOS et la plupart des
distributions Linux).

```sh
make        # compile fetcher/fetcher et server/server
make clean  # supprime les .o
make fclean # supprime aussi les binaires
make re     # fclean + all
```

## Lancer le serveur

```sh
./server/server [port] [whitelist_file]
```

- `port` : optionnel, défaut `4242`.
- `whitelist_file` : optionnel. Si fourni, seuls les identifiants listés dans ce fichier
  peuvent se connecter (un par ligne, lignes vides et commentaires `#` ignorés) :

  ```
  # étudiants autorisés pour l'examen du 2026-10-05
  poste-salle-a-01
  poste-salle-a-02
  alice-dupont
  ```

  Sans ce fichier, toute connexion est acceptée.

Dans l'interface :
- `↑` / `↓` : sélectionner un étudiant dans la liste.
- `q` : quitter l'interface (n'arrête pas le serveur).
- Vert = connecté, rouge `[ALERT]` = déconnexion suspecte détectée (plus de signal de vie
  depuis quelques secondes, ou arrêt forcé signalé explicitement par le poste étudiant).

## Lancer le fetcher

```sh
FETCHER_TEACHER_PASSWORD=<mot_de_passe> ./fetcher/fetcher -n <nom> --host <ip_serveur> --port <port>
```

- `-n`/`--name` : identifiant affiché côté serveur (sinon le nom de la machine est utilisé).
- `-h`/`--host` : adresse du serveur (défaut `127.0.0.1`).
- `-p`/`--port` : port du serveur (défaut `4242`).
- `FETCHER_TEACHER_PASSWORD` : mot de passe à saisir pour arrêter proprement le fetcher via
  `Ctrl+C`. Si cette variable n'est pas définie, un mot de passe par défaut est utilisé avec
  un avertissement — à ne jamais faire pour un examen réel.

### ⚠️ Lancement recommandé : `start-exam.sh`

Lire directement `~/.bash_history` ou `~/.zsh_history` est peu fiable : ces fichiers ne sont
pas garantis "append-only" (bash/zsh peuvent les réécrire, notamment à cause de la
déduplication des commandes répétées), et par défaut ils ne sont de toute façon écrits sur
disque qu'à la fermeture du terminal, pas après chaque commande. Résultat sans correctif :
des commandes peuvent apparaître en double, une commande répétée peut ne jamais remonter, et
rien ne remonte tant que le terminal reste ouvert.

`start-exam.sh` corrige ça en installant un hook shell qui capture chaque commande dans un
fichier dédié, au moment exact de son exécution :

```sh
cd fetcher
source start-exam.sh -n <nom> --host <ip_serveur> --port <port>
```

**Important : ce script doit être `source`-é, pas exécuté**, sinon le hook installé ne
s'appliquerait qu'à un sous-processus et pas au terminal réellement utilisé par l'étudiant.

## Protocole réseau

Communication TCP en texte, un message par ligne :

```
IDENTIFIANT|TYPE|BASE64(PAYLOAD)
```

- `TYPE` : `CMD` (commande), `HEARTBEAT` (signal de vie, toutes les 2s), ou `KILLED`
  (notification d'arrêt forcé envoyée par le fetcher).

## Structure du projet

```
common/    Protocole partagé fetcher ↔ server (format de message, encodage base64)
fetcher/   Client : lecture de l'historique shell, envoi au serveur, mot de passe d'arrêt
server/    Serveur : réception TCP, stockage en mémoire, interface ncurses
```

Voir `CLAUDE.md` pour le détail d'implémentation, les choix d'architecture, et les points
encore ouverts.
