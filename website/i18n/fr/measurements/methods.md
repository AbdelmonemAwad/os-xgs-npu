---
title: Comment on mesure
source: docs/measurements/methods.md
source_reviewed: 2026-10-21
---

# Comment les mesures sont prises

*Traduction de la page [How the measurements are taken](/docs/measurements/methods/). En cas de
différence, le texte anglais fait foi.*

Chaque page de mesure dit comment ses nombres ont été pris et ce qu'ils ne signifient pas. Cette page
rassemble ce qu'elles ont toutes en commun : le banc, l'endroit où un chiffre est lu, les outils, le calcul
derrière chaque sorte de chiffre et les règles que tient chaque page - chacune avec la raison pour laquelle
on procède ainsi, car un chiffre sans sa méthode ne prouve rien, et ceux qui ont la meilleure allure
mesurent souvent autre chose que ce qu'ils prétendent.

## Le banc

Deux XGS 3300 et un XGS 136, routés entre eux, et un PC sur celui du milieu à 10 Gbit/s - comme sur
[la page du banc](/docs/measurements/bench-2026-10-17/) (en anglais), où la fenêtre de chaque exécution est
photographiée :

| lien | depuis | vers | support |
|---|---|---|---|
| le PC | la carte 10GBASE-T du PC | F1 du second XGS 3300 | cuivre, 10 Gbit/s |
| la fibre | le port de module A1 du second XGS 3300 | F1 du premier XGS 3300 | fibre, 10 Gbit/s |
| le lien gigabit | Port8 du second XGS 3300 | Port8 du XGS 136 | cuivre, 1 Gbit/s |

| quoi | pourquoi |
|---|---|
| **Un pair matériel aux deux bouts de chaque tunnel.** | Jusqu'au 17/10/2026, le bout distant d'un tunnel mesuré était un pare-feu faisant l'ESP en logiciel ; un chiffre pouvait donc être celui de ce pare-feu plutôt que celui du boîtier. |
| **Chaque lien dans son propre /30, hors du bridge LAN, avec une règle de passage et une route statique.** | Le trafic est routé à travers le boîtier au lieu d'être commuté à côté, et rien d'autre sur le réseau ne partage le chemin. |
| **Jamais un serveur de test public comme bout distant.** | Ce sont les serveurs d'autres personnes, et ils refusent ce qui ressemble à une inondation ; une ligne refusée a un jour laissé trois « téléchargements » dans une table qui n'avaient jamais tourné. Tout ce qui est parallèle ou répété va vers un bout distant du banc. |

## Où un chiffre est lu

**Au récepteur, au bout distant - jamais sur les compteurs du boîtier lui-même.** Une connexion en
matériel va de port à port à l'intérieur du coprocesseur et n'atteint jamais l'hôte : l'état de pf, les
compteurs des interfaces et tous les graphiques bâtis dessus cessent de croître. Une jauge qui affiche
zéro quand tout fonctionne n'est pas une jauge - une exécution a un jour été arrêtée par une règle qui a lu
un tel zéro comme un effondrement qui n'a jamais eu lieu.

**Le chiffre du récepteur, pas celui de l'émetteur.** iperf3 affiche les deux : l'émetteur compte ce qu'il
a écrit dans sa socket, le récepteur ce qui est arrivé. Seul le second est un débit.

**Une ligne n'est lue qu'une fois qu'on sait qu'elle a tourné** - des octets reçus, pas seulement des
compteurs qui ont bougé.

## Les outils

| outil | ce qu'il mesure ici | pourquoi celui-là |
|---|---|---|
| **iperf3** - 3.20 sous WSL sur le PC pour émettre, iperf3 pour Windows pour recevoir, 3.22 (le paquet d'OPNsense) comme serveur sur l'hôte d'un boîtier | le débit, un flux TCP ou quatre, dix secondes par exécution | le même outil aux deux bouts de chaque exécution, sa version nommée. Deux clients sur le PC parce que le réseau en miroir de WSL laisse sortir une trame de plus de 1500 octets mais pas entrer : chaque exécution où le PC recevait au-delà de 1500 a mesuré 0,00 bit/s jusqu'à ce qu'elle soit refaite avec la version Windows |
| **curl en HTTPS, et SHA-256** | les tests d'intégrité de longue durée : un fichier de 1 Gio d'octets aléatoires récupéré en boucle pendant des heures | TLS refuse tout enregistrement qui ne s'authentifie pas, et l'empreinte du fichier comparée à celle du serveur attrape ce qui serait passé entier ; le débit seul ne dit rien de la justesse des octets |
| **ping avec le bit de non-fragmentation, à des tailles fixées** | si une trame d'une taille donnée traverse un chemin, et les allers-retours | le test le plus simple de la MTU d'un chemin et d'un port qui reçoit tout court |
| **sysctl** | les CPU (`kern.cp_time`), les compteurs propres des pilotes (`dev.octep.0.*`, `dev.npuep.0.*`), et le cache d'hôtes TCP vidé (`net.inet.tcp.hostcache.purgenow`) | les nombres du noyau lui-même, lus de la même façon à chaque exécution |
| **pfctl** | les états de pf et leurs comptes de paquets et d'octets | ce qu'a vu le pare-feu de l'hôte, à mettre en regard de ce qu'a vu le matériel |
| **les statistiques propres du coprocesseur**, lues via le pilote | ce que le matériel a acheminé : les trames venues du câble, celles qu'il a renvoyées lui-même sur le câble, son compte de chaque connexion | le seul témoin du trafic que l'hôte ne voit plus |
| **mvsw** | les registres de la puce du switch : la limite de trame d'un port, ses compteurs d'erreurs | des erreurs et des limites que seul le switch voit, lues dans ses propres registres |
| **shot-run.ps1** | lance chaque commande de mesure dans sa propre fenêtre de console et photographie cette fenêtre à la fin, avec les pixels de la fenêtre elle-même | la preuve est l'écran tel qu'il était, pas un rendu du texte retapé après coup, et rien d'autre du bureau n'entre dans l'image |
| **tools/mark-images.py** | place le propriétaire et la licence (CC BY 4.0) dans une bande sous chaque photographie ; l'original non marqué est conservé à part | une image qui quitte la page dit encore à qui elle est |
| **tools/readme-art.py** | calcule l'avancement de chaque boîtier d'après la table de la page de sa famille | voir le calcul ci-dessous ; les vérifications échouent si une page indique autre chose |

Les scripts qui pilotent chaque exécution sont nommés sur la page de cette exécution.

## Le calcul

| chiffre | comment il est calculé | pourquoi |
|---|---|---|
| **débit, une exécution** | les bits par seconde du récepteur sur l'exécution, tels qu'iperf3 les affiche | le compte du récepteur est ce qui est arrivé |
| **débit, un test de longue durée** | octets reçus dans les passes réussies × 8, sur les secondes du début de la première passe à la fin de la dernière | une moyenne sur des heures, les échecs non comptés comme livrés |
| **occupation des CPU** | `kern.cp_time` lu avant et après l'exécution ; occupation = (total − repos) ÷ total, sur les différences | ce que l'exécution a coûté sur toute sa durée, là où un coup d'œil à `top` saisit un instant |
| **part des trames en matériel** | des trames que le coprocesseur a reçues du câble sur l'exécution (`RX_WIRE`), la part qu'il a renvoyée lui-même sur le câble (`FROM_WIRE_TO_WIRE`) : 6 630 528 sur 6 657 897, soit 99,6 % | comptées par le matériel qui les a acheminées, pas déduites du silence de l'hôte |
| **un compteur, digne de confiance** | confronté à une quantité connue d'avance : le PC envoie exactement 4 000 000 000 octets, et le compte du coprocesseur et celui de pf ensemble doivent donner ce qui a franchi le câble - ils l'ont donné à 14 paquets près sur 2,74 millions | un compteur n'est cru qu'après avoir concordé avec quelque chose compté indépendamment ; deux ont été trouvés faux ainsi |
| **le résultat d'un test d'intégrité** | les passes échouées (le code de sortie de curl), les passes dont le fichier différait (l'empreinte), et les octets | un test ne passe que si les deux sont à zéro |
| **l'avancement d'un boîtier** | chaque ligne de la table de la page de sa famille a un pourcentage fait et un poids ; avancement = Σ(fait × poids) ÷ N, où les poids totalisent N et une valeur barrée ne compte pas | un pourcentage qui ne peut pas s'écarter de ce qu'il pèse : `tools/readme-art.py` le calcule, et les vérifications échouent si la page ou le README en indique un autre |

## Les règles que tient chaque page

| règle | pourquoi |
|---|---|
| **Comment il a été pris, et ce qu'il ne signifie pas**, sur chaque page | les limites d'un chiffre en font partie : un flux qui se termine sur l'hôte même d'un boîtier mesure le plus petit du PC, de l'acheminement, du lien et du chemin de réception de cet hôte - pas le plafond d'acheminement, qui demande une machine derrière le boîtier distant |
| **La compilation est nommée** - le commit du pilote ou sa somme de contrôle | un nombre appartient au code qui l'a produit |
| **Chaque fenêtre est photographiée** à la fin de son exécution | chacun peut voir l'exécution, pas une copie de ses nombres ; les adresses du labo restent sur les images, les numéros de série et les adresses matérielles sont floutés |
| **Ce qu'un hôte a appris est oublié avant chaque exécution** - son cache d'hôtes TCP, et le cache de MTU de chemin du PC | une MTU de chemin apprise dans une exécution a été reportée dans la suivante : toute une passe a tourné avec des segments de 500 octets et a été jetée |
| **Ce qui a mal tourné dans la mesure est écrit** | les erreurs - une MTU apprise, les trames perdues de WSL - expliquent pourquoi une page a mesuré comme elle l'a fait, et la mesure suivante les évite |
| **Un chiffre retiré est barré, pas effacé**, la correction à côté | le lecteur voit ce qui a changé, et pourquoi |
| **Les chiffres du constructeur sont étiquetés comme ceux du constructeur**, à côté des nôtres | ils viennent de Keysight-Ixia BreakingPoint en conditions idéales, sans indiquer la MTU, les ports ni si les deux sens comptent : pas le même genre de nombre qu'un chiffre pris ici |

## Où sont les nombres

Chaque mesure, par boîtier et par jour : [les mesures](/docs/measurements/) (en anglais).
