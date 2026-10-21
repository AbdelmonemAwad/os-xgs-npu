---
title: Wie gemessen wird
source: docs/measurements/methods.md
source_reviewed: 2026-10-21
---

# Wie die Messungen entstehen

*Übersetzung der Seite [How the measurements are taken](/docs/measurements/methods/). Bei Abweichungen
gilt der englische Text.*

Jede Messseite sagt, wie ihre Zahlen entstanden sind und was sie nicht bedeuten. Diese Seite sammelt,
was allen gemeinsam ist: den Prüfstand, wo eine Zahl abgelesen wird, die Werkzeuge, die Rechnung hinter
jeder Art von Zahl und die Regeln, die jede Seite einhält - jeweils mit dem Grund, warum es so gemacht
wird. Denn eine Zahl ohne ihre Methode beweist nichts, und die, die am besten aussehen, messen meist
etwas anderes als das, was sie behaupten.

## Der Prüfstand

Zwei XGS 3300 und eine XGS 136, zueinander geroutet, und ein PC an der mittleren mit 10 Gbit/s - wie auf
[der Seite des Prüfstands](/docs/measurements/bench-2026-10-17/) (Englisch), wo das Fenster jedes Laufs
fotografiert ist:

| Verbindung | von | nach | Medium |
|---|---|---|---|
| der PC | die 10GBASE-T-Netzwerkkarte des PCs | F1 der zweiten XGS 3300 | Kupfer, 10 Gbit/s |
| die Glasfaser | Modulport A1 der zweiten XGS 3300 | F1 der ersten XGS 3300 | Glasfaser, 10 Gbit/s |
| die Gigabit-Verbindung | Port8 der zweiten XGS 3300 | Port8 der XGS 136 | Kupfer, 1 Gbit/s |

| was | warum |
|---|---|
| **Eine Hardware-Gegenstelle an beiden Enden jedes Tunnels.** | Bis zum 17.10.2026 war das ferne Ende eines gemessenen Tunnels eine Firewall, die ESP in Software rechnete; eine Zahl konnte also die jener Firewall sein statt die des Geräts. |
| **Jede Verbindung ein eigenes /30, außerhalb der LAN-Bridge, mit einer Pass-Regel und einer statischen Route.** | Der Verkehr wird durch das Gerät geroutet, nicht an ihm vorbei geswitcht, und nichts anderes im Netz teilt den Weg. |
| **Nie ein öffentlicher Testserver als fernes Ende.** | Das sind Server anderer Leute, und sie lehnen ab, was wie eine Flut aussieht; eine abgelehnte Zeile hinterließ einmal drei „Downloads“ in einer Tabelle, die nie gelaufen waren. Alles Parallele oder Wiederholte geht an ein fernes Ende auf dem Prüfstand. |

## Wo eine Zahl abgelesen wird

**Beim Empfänger, am fernen Ende - nie an den eigenen Zählern des Geräts.** Eine Verbindung in Hardware
geht innerhalb des Koprozessors von Port zu Port und erreicht den Host nie: der Zustand in pf, die
Schnittstellenzähler und jeder darauf gebaute Graph hören auf zu wachsen. Eine Anzeige, die null zeigt,
wenn alles funktioniert, ist keine Anzeige - ein Lauf wurde einmal von einer Regel gestoppt, die eine
solche Null als Zusammenbruch las, den es nie gab.

**Die Zahl des Empfängers, nicht die des Senders.** iperf3 gibt beide aus: der Sender zählt, was er in
seinen Socket schrieb, der Empfänger, was ankam. Nur das Zweite ist Durchsatz.

**Eine Zeile wird erst gelesen, wenn feststeht, dass sie gelaufen ist** - empfangene Bytes, nicht nur
bewegte Zähler.

## Die Werkzeuge

| Werkzeug | was es hier misst | warum dieses |
|---|---|---|
| **iperf3** - 3.20 unter WSL auf dem PC zum Senden, iperf3 für Windows zum Empfangen, 3.22 (das Paket von OPNsense) als Server auf dem Host eines Geräts | Durchsatz, ein TCP-Strom oder vier, zehn Sekunden pro Lauf | dasselbe Werkzeug an beiden Enden jedes Laufs, seine Version genannt. Zwei Clients auf dem PC, weil das gespiegelte Netzwerk von WSL einen Frame über 1500 Bytes hinaus-, aber nicht hereinlässt: jeder Lauf, in dem der PC über 1500 empfing, maß 0,00 Bit/s, bis er mit der Windows-Version neu gemessen wurde |
| **curl über HTTPS und SHA-256** | die Integritäts-Dauerläufe: eine 1-GiB-Datei aus Zufallsbytes, stundenlang in Schleifen geholt | TLS verwirft jeden Record, der sich nicht authentifiziert, und der Hash der Datei gegen den des Servers fängt alles, was ganz durchkam; Durchsatz allein sagt nichts darüber, ob die Bytes stimmten |
| **ping mit gesetztem Don't-Fragment-Bit, in festen Größen** | ob ein Frame einer bestimmten Größe einen Weg passiert, und Umlaufzeiten | der einfachste Test der MTU eines Wegs und eines Ports, der überhaupt empfängt |
| **sysctl** | die CPUs (`kern.cp_time`), die eigenen Zähler der Treiber (`dev.octep.0.*`, `dev.npuep.0.*`) und der geleerte TCP-Host-Cache (`net.inet.tcp.hostcache.purgenow`) | die eigenen Zahlen des Kernels, bei jedem Lauf gleich gelesen |
| **pfctl** | die Zustände von pf und ihre Paket- und Bytezahlen | was die Firewall des Hosts sah, neben das zu stellen, was die Hardware sah |
| **die eigenen Statistiken des Koprozessors**, über den Treiber gelesen | was die Hardware weiterleitete: Frames von der Leitung, Frames, die sie selbst zurück auf die Leitung schickte, ihre Zählung jeder Verbindung | der einzige Zeuge des Verkehrs, den der Host nicht mehr sieht |
| **mvsw** | die Register des Switch-Chips: das Frame-Limit eines Ports, seine Fehlerzähler | Fehler und Grenzen, die nur der Switch sieht, aus seinen eigenen Registern gelesen |
| **shot-run.ps1** | startet jeden Messbefehl in einem eigenen Konsolenfenster und fotografiert dieses Fenster, wenn er endet, mit den eigenen Pixeln des Fensters | der Beleg ist der Bildschirm, wie er war, nicht eine nachträglich abgetippte Wiedergabe, und nichts anderes vom Desktop gerät ins Bild |
| **tools/mark-images.py** | setzt den Inhaber und die Lizenz (CC BY 4.0) in einen Streifen unter jedes Foto; das unmarkierte Original wird getrennt aufbewahrt | ein Bild, das die Seite verlässt, sagt noch, wem es gehört |
| **tools/readme-art.py** | berechnet den Fertigstellungsgrad jedes Geräts aus der Tabelle seiner Familienseite | siehe die Rechnung unten; die Prüfungen schlagen fehl, wenn eine Seite etwas anderes angibt |

Die Skripte, die jeden Lauf steuern, sind auf der Seite dieses Laufs genannt.

## Die Rechnung

| Zahl | wie sie berechnet wird | warum |
|---|---|---|
| **Durchsatz, ein Lauf** | die Bits pro Sekunde des Empfängers über den Lauf, wie iperf3 sie ausgibt | die Zählung des Empfängers ist das, was ankam |
| **Durchsatz, ein Dauerlauf** | in den erfolgreichen Durchgängen empfangene Bytes × 8, geteilt durch die Sekunden vom Beginn des ersten bis zum Ende des letzten Durchgangs | ein Mittel über Stunden, Fehlschläge nicht als geliefert gezählt |
| **CPU-Auslastung** | `kern.cp_time` vor und nach dem Lauf gelesen; Auslastung = (gesamt − Leerlauf) ÷ gesamt, aus den Differenzen | was der Lauf über seine ganze Dauer kostete, wo ein Blick auf `top` einen Moment herausgreift |
| **Anteil der Frames in Hardware** | von den Frames, die der Koprozessor über den Lauf von der Leitung empfing (`RX_WIRE`), der Anteil, den er selbst zurück auf die Leitung schickte (`FROM_WIRE_TO_WIRE`): 6.630.528 von 6.657.897 sind 99,6 % | gezählt von der Hardware, die sie weiterleitete, nicht aus dem Schweigen des Hosts gefolgert |
| **ein Zähler, dem vertraut wird** | gegen eine vorher bekannte Menge gestellt: der PC sendet genau 4.000.000.000 Bytes, und die Zählung des Koprozessors und die von pf zusammen müssen ergeben, was über die Leitung ging - sie ergaben es bis auf 14 Pakete von 2,74 Millionen | einem Zähler wird erst vertraut, wenn er mit etwas unabhängig Gezähltem übereinstimmte; zwei wurden auf diese Weise als falsch erkannt |
| **das Ergebnis eines Integritäts-Dauerlaufs** | fehlgeschlagene Durchgänge (Exit-Code von curl), Durchgänge mit abweichender Datei (der Hash) und Bytes | ein Dauerlauf besteht nur, wenn beide null sind |
| **der Fertigstellungsgrad eines Geräts** | jede Zeile der Tabelle seiner Familienseite hat einen Erledigt-Prozentsatz und ein Gewicht; Fertigstellung = Σ(erledigt × Gewicht) ÷ N, wobei die Gewichte N ergeben und ein durchgestrichener Wert nicht zählt | ein Prozentsatz, der nicht von dem abweichen kann, was er gewichtet: `tools/readme-art.py` berechnet ihn, und die Prüfungen schlagen fehl, wenn die Seite oder das README einen anderen nennt |

## Die Regeln, die jede Seite einhält

| Regel | warum |
|---|---|
| **Wie gemessen wurde und was es nicht bedeutet**, auf jeder Seite | die Grenzen einer Zahl gehören zu ihr: ein Strom, der am eigenen Host eines Geräts endet, misst das Kleinste aus PC, Weiterleitung, Verbindung und Empfangspfad dieses Hosts - nicht die Obergrenze der Weiterleitung, die eine Maschine hinter dem fernen Gerät braucht |
| **Der Build wird genannt** - der Commit des Treibers oder seine Prüfsumme | eine Zahl gehört zu dem Code, der sie erzeugt hat |
| **Jedes Fenster wird fotografiert**, wenn sein Lauf endet | jeder kann den Lauf sehen, nicht eine Kopie seiner Zahlen; Laboradressen bleiben in den Bildern, Seriennummern und Hardware-Adressen werden unkenntlich gemacht |
| **Was ein Host gelernt hat, wird vor jedem Lauf vergessen** - sein TCP-Host-Cache und der Path-MTU-Cache des PCs | eine in einem Lauf gelernte Path-MTU wurde in den nächsten mitgenommen: ein ganzer Durchgang lief mit 500-Byte-Segmenten und wurde verworfen |
| **Was beim Messen schiefging, wird aufgeschrieben** | die Fehler - eine gelernte MTU, die verlorenen Frames von WSL - sind der Grund, warum eine Seite so gemessen hat, wie sie es tat, und die nächste Messung vermeidet sie |
| **Eine zurückgezogene Zahl wird durchgestrichen, nicht gelöscht**, die Korrektur daneben | ein Leser sieht, was sich geändert hat und warum |
| **Die Herstellerangaben werden als die des Herstellers gekennzeichnet**, neben unseren | sie stammen von Keysight-Ixia BreakingPoint unter idealen Bedingungen, ohne Angabe von MTU, Ports und ob beide Richtungen zählen: nicht dieselbe Art von Zahl wie eine hier gemessene |

## Wo die Zahlen stehen

Jede Messung, nach Gerät und nach Tag: [die Messungen](/docs/measurements/) (Englisch).
