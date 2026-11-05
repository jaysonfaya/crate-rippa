# CRATE RIPPA Desktop 1.0.0 (JJ DA PLUG)

YouTube auf dem Plattenspieler: suchen, scratchen, auf 64 Pads choppen, jeden Chop bearbeiten, WAVs exportieren. Dazu gibt es einen CRATE-Reiter mit FAVS und eigenen Playlists. Das ist die Desktop-Version des MPC-Plugins CRATE RIPPA, mit derselben Engine.

## Formate
| System | Dateien |
|---|---|
| Windows | `Crate Rippa.vst3` (VST3), `Crate Rippa.dll` (VST2 für MPC 2), `Crate Rippa.exe` (Standalone) |
| macOS (Apple Silicon + Intel) | `Crate Rippa.vst3`, `Crate Rippa.component` (AU), `Crate Rippa.vst` (VST2), `Crate Rippa.app` |
| Linux | `Crate Rippa.vst3`, Standalone |

## Installieren

### Windows
- **VST3** (Ableton, FL Studio, Cubase, Bitwig, Reaper usw.): Den Ordner `Crate Rippa.vst3` nach `C:\Program Files\Common Files\VST3\` kopieren.
- **MPC 2.15 (VST2):**
  1. `Crate Rippa.dll` in den VST-Ordner der MPC-Software kopieren, z. B. `C:\Program Files\VSTPlugins\`.
  2. In MPC unter *Preferences → Plugins* diesen Ordner eintragen und *Rescan* drücken.
  3. Crate Rippa erscheint unter Instruments (Hersteller JJ DA PLUG).
- **Standalone:** `Crate Rippa.exe` direkt starten.

### macOS
1. Die Dateien kopieren:
   - `Crate Rippa.component` nach `~/Library/Audio/Plug-Ins/Components/`
   - `Crate Rippa.vst3` nach `~/Library/Audio/Plug-Ins/VST3/`
   - `Crate Rippa.vst` nach `~/Library/Audio/Plug-Ins/VST/`
2. Die Dateien sind nur ad hoc signiert. Darum einmal im Terminal die Download-Sperre entfernen:
   `xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/Components/"Crate Rippa.component"`
   Für die anderen Dateien gilt dasselbe.

## Erster Start
- Beim ersten SEARCH lädt das Plugin **yt-dlp** einmal selbst herunter, eine Datei von GitHub. Das dauert ein paar Sekunden.
- Windows und macOS brauchen **kein ffmpeg**. YouTube liefert M4A, und das dekodiert das System selbst (Media Foundation bzw. Core Audio). Unter Linux muss ffmpeg installiert sein.
- **Deno (optional):** YouTube verlangt von yt-dlp immer öfter JavaScript. Meldet der Status „INSTALL DENO", im INFO-Reiter einmal INSTALL DENO drücken.
- Alle Daten liegen in `Dokumente/JJ DA PLUG/Crate Rippa/`:
  - `bin` (yt-dlp, Deno)
  - `cache` (die letzten 120 Songs)
  - `pads` (eingefrorene Chops)
  - `Exports`
  - `Crates/crates.json` (FAVS und Playlists)
  - `history.txt`

## Reiter
- **PLAYER:**
  - Plattenspieler mit Dog-Label. Auf der Platte ziehen = scratchen.
  - Pitch-Fader (oben langsamer, wie bei Technics). RANGE 8/16/50 % oder OCT, KEY LOCK.
  - Rechts die Suche: SONGS, LISTS (Playlists), HISTORY.
  - Herz = FAVS. **+** = in eine Crate.
  - Eine geöffnete YouTube-Playlist lässt sich mit SAVE AS CRATE als Crate speichern.
- **CHOP:**
  - Wellenform des ganzen Songs mit allen Chops; Klick = dorthin springen.
  - 16 Pads, Bänke A–D. HITS, BEATS (mit DETECT und HOST BPM) und LAZY (NEXT PAD/PAD HIT, PADS PLAY/CHOP, TAP).
  - Dazu: SET CUE, UNDO, CLEAR, COPY/MOVE/SWAP/DELETE, BANK COPY/MOVE/SWAP, LEARN PADS, EXPORT WAVS.
  - **Ein Pad in die DAW ziehen** ergibt eine WAV dieses Chops.
- **WAVE:**
  - SONG VIEW oder PAD VIEW. START und END mit der Maus ziehen, Feinschritte ±5/±50 ms.
  - ADSR mit Bild, PITCH, LEVEL, ONE SHOT/GATE, REVERSE, Mute Groups.
- **ISOLATE:** 7 leichte Filter für den Song, das gewählte Pad oder die ganze Bank.
- **CRATE:**
  - Links FAVS und eigene Crates: NEW, RENAME, DELETE, IMPORT, EXPORT.
  - Rechts die Songs: Filter, Sortieren per Spaltenkopf, UP/DOWN oder Ziehen zum Umordnen. Einen Song auf eine Crate links ziehen kopiert ihn dorthin; mit Shift wird er verschoben.
  - Pro Song BPM, KEY, TAGS und NOTE. BPM FROM THE DECK übernimmt das erkannte Tempo.
  - Grüner Punkt = im Cache, spielt sofort und offline.
  - Doppelklick = laden. SHOW IN PLAYER legt die Crate in die Liste des Players.
- **INFO:** Anleitung, UPDATE YT-DLP, INSTALL DENO, Ordner öffnen.

## MIDI
Note 36 (C1) = A1 bis 51 = A16, chromatisch. LEARN PADS: erst Pad 1, dann Pad 16 drücken. Tonleiter-Layouts werden erkannt.

## Bauen (GitHub)
Die Quellen liegen im Repo `crate-rippa` im Ordner `desktop/`, der Workflow in `.github/workflows/desktop.yml`. Der Workflow startet bei jeder Änderung in `desktop/` von selbst, sonst unter *Actions → Build Crate Rippa Desktop → Run workflow*. Nach etwa 20 Minuten liegen unter *Artifacts* die ZIPs für Windows, macOS und Linux.

## Rechtliches
- Nutze nur Inhalte, an denen du die Rechte hast oder die frei sind.
- VST2 ist nur für den eigenen Gebrauch gedacht, Steinberg vergibt keine VST2-Lizenzen mehr. Für einen Verkauf VST3 und AU verwenden.
- Lizenzen der Bausteine: siehe `LICENSE-THIRD-PARTY.md`.
