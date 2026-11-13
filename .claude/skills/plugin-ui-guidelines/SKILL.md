---
name: plugin-ui-guidelines
description: Linee guida visive dei plugin "Self Made Plugins" (b-aftercab, b-distort, b-focus). Usala prima di toccare l'interfaccia di un plugin di questo progetto, e come riferimento per design-critique / accessibility-review / design-system.
---

# Linee guida UI — Self Made Plugins

Regole decise dall'utente. Vincolano ogni modifica e ogni revisione: una critica
di design può proporre di affinarle, mai di contraddirle senza chiedere.

## Stile
- Minimal e geometrico. **Nessun angolo arrotondato** (border-radius 0 ovunque).
- **Solo linee continue da 1 px.** Niente linee tratteggiate (riquadri, griglie, checkbox, pulsanti).
- Tipografia **IBM Plex** (font in `shared/Resources`, tema in `shared/smp/Theme.h`):
  - logotipo: `b` Plex Sans Bold nel colore del plugin, `-` Light `#666666`, nome Medium `#E0E0E0` con tracking -0.02em;
  - etichette descrittive (delverb, freq, mix...): Plex Sans Medium, 1 px più piccole dei controlli, `#8E8E8E`, minuscolo;
  - selettori/bottoni testuali: inattivi Plex Sans Regular `#CCCCCC`, attivi SemiBold scuro sul colore di accento;
  - valori nei box: Plex Mono Medium `#F0F0F0`; unità (`dB`, `kHz`, `Hz`, `ms`, `%`, `s`) in Plex Mono Regular `#888888`, 1 px più piccole;
  - numeri sugli assi e dei nodi: Plex Mono Regular piccoli e grigi; etichette interne ai grafici in MAIUSCOLO, Sans SemiBold 10, tracking 0.08em (`OUT`, `IN`);
  - un valore con unità dentro un grafico (`380 ms`, `8 kHz 1.73 s`) non è un'etichetta: `ui::drawValueTag` (Plex Mono, mai in maiuscolo, così `Hz` / `kHz` / `ms` restano scritti giusti);
  - istruzioni e suggerimenti nei grafici ("drag or scroll") in testo normale `info` #808080, mai in maiuscolo né nel grigio dei controlli spenti (contrasto AA);
  - un testo o un meter sopra un grafico ha un fondo pieno `background`, così curve e spettri non lo attraversano;
  - quando un plugin è spento o non fa nulla (es. b-drop a 0 semitoni) lo dice nel grafico, in `info`.
  - bottoni con numeri (`tap 128`, conto alla rovescia di `learn`): Plex Mono.
- Numeri con il **punto decimale** (`-12.0`, `1.00`) e unità standard (`dB`, `kHz`, `Hz`).
- Il testo non cambia dimensione quando si ridimensionano i controlli.

## Colori
| Ruolo | Valore |
|---|---|
| Sfondo (finestre, riquadri, b-rig) | `#0D0D0F` |
| Testo principale (nomi delle funzioni, valori) | `#A3A3A3` |
| Testi informativi (dB, Hz, stato) — un filo più scuri | `#808080` |
| Testi / funzioni disattivate, riquadro principale | `#525252` |
| Griglia interna (molto scura) | `#1F1F1F`, linea dello zero `#303030` |
| Accento b-aftercab (funzioni attive, checkbox) | `#EAB308` |
| Accento b-distort | rosso `#EF4444` |
| Accento b-focus | blu `#3B82F6` |
- Gerarchia: nomi interattivi più chiari, testi informativi più scuri.
- Su un elemento pieno di accento il testo diventa scurissimo (`#121212`).
- Dentro b-rig il modulo aperto non ha contorno: sta sullo stesso sfondo dell'app.

## Componenti
- **Checkbox**: spenta = solo bordo 1 px, sfondo trasparente; accesa = quadrato pieno di accento.
- **Toggle / selettori a segmenti**: inattivi = bordo 1 px e sfondo trasparente; attivo = pieno di accento, testo scuro. Disabilitati = grigio `#525252` (l'etichetta accanto resta chiara).
- **Box valore**: il riquadro avvolge solo il numero, **equidistante dai 4 bordi**; le frecce `<` `>` stanno subito fuori, con padding unificato. Numeri con il punto (`15.00`). Il delay manuale lavora su ms interi; solo l'allineamento di fase dà decimali.
  - **REGOLA GLOBALE – larghezza fissa dal caso peggiore (layout shift prevention)**, per tutti i plugin attuali e futuri:
    1. Ogni riquadro con un valore testuale ha una larghezza **fissa**, mai adattiva al valore corrente.
    2. La larghezza si calcola **a priori sul caso peggiore**: la stringa più larga che quel parametro può mostrare, compresi segno negativo, decimali massimi e unità (es. range -100…100 dB → "-100.0 dB"). In `ui::ValueBox` è automatico: si scorre tutto il range del parametro (513 punti più minimo, massimo e default) con lo stesso `format`, più gli eventuali `sizeSamples`, e si prende l'inchiostro più largo; poi il padding fisso (`extraWidth` facoltativo). Se il riquadro cambia parametro (es. banda EQ selezionata) non si restringe mai.
    3. Il testo è **centrato** nel riquadro (centratura sull'inchiostro).
    4. **Verifica obbligatoria nei test**: `ui::checkFixedValueBoxes (editor, report)` (`smp/TestUtil.h`) rende per ogni riquadro il valore più corto e il più lungo (tutto il range a 64 passi) e fallisce se la larghezza cambia o non coincide con quella del riquadro. Ogni suite di test deve chiamarla sull'editor.
    Vale anche per testi non in un ValueBox (tasti con conto alla rovescia, valori dei meter): larghezza calcolata sulla stringa più lunga (es. "tap 300", "-36.0 dB").
  - **Barra di riempimento** (tutti i plugin, attuali e futuri): dentro ogni controllo numerico scorrevole, sempre visibile, da sinistra (minimo) a destra (massimo) sulla scala normalizzata del parametro. Colore `valueFill` #2C2C2C (poco più chiaro dello sfondo ma ben visibile), `valueFillOff` #1C1C1C da disattivato. Nei controlli verticali (es. OUT) si riempie dal basso.
  - **Doppio clic** sul numero: torna al default (quello del preset, se c'è) **e** apre la modifica a tastiera con il numero tutto selezionato, evidenziato in `editHighlight` #C8C8C8 (chiaro neutro, mai il blu di sistema) con testo scuro. Se non si scrive nulla, Invio o clic fuori lasciano il default; Esc annulla. Accetta virgola o punto e "k" per le migliaia.
- **Plugin nuovi semplici** (b-adapt, b-double): struttura comune `smp/Shell.h` (titolo, selettore in alto a destra, riquadro con riga controlli e grafico), filtri comuni in `smp/Filters.h`. Plugin con valori grandi e gruppi (b-verb, b-delay, b-comp): `BigValue`, `Section`, `TextToggle` e la misura `kRoomy`.
- **Colori di accento**: tutti in `shared/smp/Palette.h` (un solo posto; i CMakeLists usano `SMP_ACCENT_ARGB=BSUITE_ACCENT_...`, i moduli di b-rig e la sua libreria le stesse macro), per ruolo: PRE/DI freddi (b-focus #3B82F6, b-drop #8B5CF6, b-comp #84CC16, b-fs #F59E0B), CORE/AMP caldi (b-amp #E11D48, b-distort #EF4444, b-starve #FB7185, b-wire #6366F1, b-blend #14B8A6), POST/SPACE aria e acqua (b-aftercab #EAB308, b-delay #60A5FA, b-dynaverb #06B6D4, b-verb #22D3EE, b-space #A78BFA), MASTER/UTIL neon (b-meter #38BDF8, b-adapt #D946EF; avviso audition #10B981). Solo plugin: b-double #22D3EE, b-scenery #F1F5F9, b-synthesia #E2E8F0. Test comuni in `smp/TestKit.h` (snapshot + larghezze fisse; chiamare `editorBeingDeleted` prima di distruggere un editor nei test). Nei selettori a segmenti i segmenti hanno larghezza uguale: etichette brevi.
- **Memoria per preset** (solo sessione, `smp/PresetMemory.h`): ogni preset (tipo di compressione, timbro, tipo di distorsione…) ricorda i valori modificati finché il plugin resta caricato; doppio clic sul preset = suoi default. Non si salva col progetto.
- **Checkbox del compressore = meter**: accesa = solo bordo di accento; l'interno si riempie dall'alto in proporzione alla gain reduction (pieno = 12 dB), in tempo reale.
- **Pulsante smart phase**: quadrato di accento, stessa misura della checkbox, con `=` ritagliato.

## Misure (token in `Source/UI/Theme.h`, uguali nei due plugin)
- Padding interno dei riquadri `kFramePad` = 21 px, **uguale sui 4 lati** (misurato sull'inchiostro: scala a sinistra, bordo griglia a destra, riga controlli sopra, linea di base delle etichette sotto). Stesso valore nell'estensione e tra riquadro e riga sottostante.
- Spaziature: etichetta → controllo `kGapLabel` 8; nello stesso gruppo `kGapInner` 12; tra gruppi `kGapGroup` 24.
- Controlli: altezza `kControlH` 19, checkbox `kBoxSize` 15 con area cliccabile +4 px per lato.
- Testo centrato sull'altezza delle cifre (`drawLabel`), così una riga condivide la stessa linea di base; etichette di misure diverse sulla stessa riga usano `drawAtBaseline`.
- Contrasto: testo informativo `#808080` (4,7:1, AA).

## Layout
- Header: nome del plugin in alto a sinistra senza contenitore, la `b` nel colore di accento, il resto in grigio.
- b-aftercab: sopra il riquadro EQ due righe di effetti incolonnate — delverb (sulla linea di base del titolo) e sotto il compressore; checkbox e selettori sulle stesse colonne; nel comp `thr` e `auto` seguono il selettore a distanza `kGapGroup`; il tasto tempo sul bordo destro della griglia.
- b-focus: meter `hf` e pulsante `learn` centrati in verticale sul titolo (mezzeria tra linea di base e altezza delle maiuscole).
- Spettri dal vivo (b-aftercab nel grafico EQ, b-distort a destra della curva): area tenue + contorno, ingresso grigio e uscita nel colore di accento, calcolati solo a interfaccia aperta.
- Riquadro principale con linea continua `#525252`; i controlli si allineano **al grafico**, non al riquadro: a sinistra al bordo della scala, a destra al bordo destro della griglia.
- b-aftercab: estensione in basso a sinistra con i controlli della banda, margine **uguale sui 4 lati**; spread sulla stessa riga della banda, allineato a destra.
- La curva dell'EQ è tagliata ai bordi del grafico; i punti hanno padding interno per non essere tagliati; i punti bell stanno esattamente sulla curva.
- Asse frequenze b-aftercab: 1k–10k più ampia, 100–1k più stretta; etichette intermedie (50, 75, 250, 500, 750, 2k…8k) un pizzico più piccole.

## Plugin con tanti controlli (b-verb, b-delay): tre livelli
L'utente ha bocciato la versione con tutti i controlli uguali in riga e colonne: servono differenza, gruppi e on/off.
- **Valori grandi** (`ui::BigValue`): i 3-4 controlli principali in una riga in alto, ciascuno centrato in una colonna di
  uguale larghezza; etichetta sopra, riquadro fisso (caso peggiore) con il numero Plex Mono 24 allineato a destra in
  uno spazio fisso (le cifre non si spostano mai), riempimento grigio come nei box e il bordo inferiore del riquadro
  nel colore del plugin fino al valore (nessuna barra separata). I pulsanti di quella riga (freeze, sync, tap) stanno in una colonna loro, centrati sul numero.
- **Grafico** in mezzo, a tutta larghezza.
- **Gruppi** (`ui::Section`) in basso, in colonne di uguale larghezza con il contenuto centrato e una linea sottile
  (`separator` #2A2A2A) tra una colonna e l'altra. Titolo del gruppo Sans SemiBold 14 chiaro; i gruppi facoltativi hanno
  la casella on/off accanto al titolo: spento = titolo grigio, etichette #525252, controlli disabilitati.
- **Pulsanti on/off con il nome** (`ui::TextToggle`) per gli interruttori (freeze, ping-pong, trails): bordo da spento,
  pieno di accento da acceso. Accendere o spegnere un gruppo fa scivolare il valore: mai click.
- Margine interno un po' più largo del padding standard (+6 px); poche misure di testo (24, 14, 13, 12, 11).
- Il tipo resta l'unico selettore in alto a destra.

## Versione e aggiornamenti
- Il pannello about mostra `version 0.1.0 beta` (Plex Mono, `info`) sulla riga di "crafted in Italy", allineata a destra.
- Aggiornamento disponibile (`smp/Updates.h`, release GitHub con tag `<prodotto>-v<versione>`):
  - avviso nell'angolo in basso a destra (16 px dai bordi, alto 52): fondo `#121212`, bordo 1 px nel colore del plugin, "update available" (`info`, 12) sopra "b-xxx 0.2.0 beta" (SemiBold 13, colore del plugin), x a destra; un clic apre il pannello about, la x lo chiude; non ricompare fino alla prossima apertura;
  - quadratino pieno 6×6 nel colore del plugin in alto a destra del logotipo, all'altezza delle maiuscole (resta anche dopo aver chiuso l'avviso);
  - nel pannello about la riga "b-xxx 0.2.0 beta is available" nel colore del plugin e il tasto pieno `download` allineato a destra.
- Il controllo parte solo con la finestra sullo schermo, al massimo una volta al giorno per tutta la suite; dentro b-rig i moduli non controllano.

## Dentro b-rig
- I plugin sono compilati anche dentro l'app b-rig con `SMP_IN_RIG=1`: `ui::kInRig` è vero. `drawTitle` non disegna il logotipo e il pannello about non si apre (il nome del modulo è già nell'app).
- Strumenti che l'app ha già (tuner, metronomo) vanno nascosti con `ui::kInRig` (es. il tasto tuner di b-amp). Ogni plugin nuovo segue la stessa regola.
- b-rig è anche un plugin (AU, VST3: `RigComponent` con `plugin` vero): niente metronomo, tempo, tap, setup né scorciatoie da tastiera; il tempo arriva dalla DAW ai moduli (il loro sync).
- Tap tempo, in tutta la suite: il tempo cambia dal quarto tocco.

## Verifica
Dopo ogni modifica: compilare, generare gli snapshot (`*Tests` scrive i PNG in `build/`), guardarli ingranditi e misurare i pixel quando la regola è numerica (es. margini uguali).
