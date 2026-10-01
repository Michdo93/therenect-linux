# Therenect 0.9.2 – Linux-Port (Raspberry Pi OS / Ubuntu, Kinect V1)

Virtuelles Theremin für die Kinect V1 (Xbox 360, Modelle 1414/1473). Port der
Originalversion von Martin Kaltenbrunner (Interface Culture Lab, Kunstuniversität
Linz, 2010, GPL-2.0-or-later) von macOS/openFrameworks 0.06 auf Linux.

Tracking (zwei virtuelle Antennen, gewichteter Schwerpunkt, Kalman-Glättung),
Klangsynthese (Theremin/Sinus/Sägezahn/Rechteck, Glide), Tonleitern und MIDI
entsprechen dem Original 1:1. Ersetzt wurden nur die nicht mehr verfügbaren
Bibliotheken.

| Original (2010)              | Linux-Port                                   |
|------------------------------|----------------------------------------------|
| openFrameworks 0.06 + GLUT   | SDL2 (Fenster, Rendering, Audio)             |
| ofxKinect                    | libfreenect direkt (`src/KinectV1.*`)        |
| ofxOpenCv / ofxCvKalman      | skalarer Kalman-Filter (`src/Kalman1D.h`), kein OpenCV nötig |
| ofxMidi (CoreMIDI)           | RtMidi über ALSA-Sequencer (`src/MidiOut.*`) |
| ofxControlPanel + XML        | Dear ImGui + `~/.config/therenect/settings.ini` |
| ofSoundStream                | SDL-Audio (PipeWire/PulseAudio/ALSA)         |

Alle Abhängigkeiten außer Dear ImGui kommen aus den Paketquellen; ImGui (v1.91.9)
lädt CMake beim ersten Konfigurieren von GitHub.

## Voraussetzungen

- **Raspberry Pi OS Bookworm oder Trixie** (32/64 Bit) bzw. **Ubuntu 22.04/24.04**.
  Bullseye reicht nicht (SDL < 2.0.18).
- Raspberry Pi 4 oder 5 empfohlen (Pi 3 läuft, aber knapp).
- Kinect V1 **mit eigenem Netzteil/USB-Adapter** – über USB allein bekommt sie
  nicht genug Strom.
- Desktop-Sitzung (X11 oder Wayland). Ohne Desktop läuft SDL über KMS/DRM.

## Installation

```bash
./scripts/install.sh            # Pakete, udev-Regeln, gspca-Blacklist, Build
./scripts/install.sh --install  # zusätzlich nach /usr/local installieren
```

Manuell:

```bash
sudo apt install build-essential cmake git pkg-config libfreenect-dev libsdl2-dev librtmidi-dev
sudo cp udev/51-kinect.rules /etc/udev/rules.d/
sudo cp udev/blacklist-gspca-kinect.conf /etc/modprobe.d/
sudo udevadm control --reload-rules && sudo udevadm trigger
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
```

Offline-Build: `-DTHERENECT_IMGUI_DIR=/pfad/zu/imgui`. Ohne MIDI: `-DTHERENECT_MIDI=OFF`.

## Start

```bash
./build/therenect                # Fenster 1120x640
./build/therenect -f             # Vollbild (auch F11)
./build/therenect -b 1024        # größerer Audiopuffer, falls es auf dem Pi knackt
./build/therenect -d 1           # zweite Kinect
./build/therenect -n             # ohne Kinect, nur Mausbedienung im Oszilloskop
```

Das Layout skaliert auf jede Fenstergröße, z. B. das 7"-Touch-Display (800x480);
Touch funktioniert wie Maus.

## Bedienung

Rechte Hand: Tonhöhe (Antenne rechts), linke Hand: Lautstärke (Antenne links unten).
Hand nahe an der Lautstärke-Antenne = leise, wie beim echten Theremin.

| Taste      | Funktion                                    |
|------------|---------------------------------------------|
| `,` / `<`  | Antennen näher zum Spieler                  |
| `.` / `>`  | Antennen näher zur Kinect                   |
| `+` / `-`  | Kinect-Neigung (±27°)                       |
| `0`–`3`    | Theremin, Sinus, Sägezahn, Rechteck         |
| `f c i p`  | stufenlos, chromatisch, Dur, Pentatonik     |
| `m`        | MIDI an/aus                                 |
| `d`        | Anzeige aus/an (spart CPU)                  |
| `F11`/`Esc`| Vollbild / Beenden                          |

Maus: Ziehen in der Punktwolke dreht die Ansicht, Klicken/Ziehen im Oszilloskop
spielt manuell (x = Tonhöhe, y = Lautstärke).

## MIDI

Ist MIDI aktiv, verstummt der interne Oszillator; Therenect sendet Note On/Off
und CC 7 (Lautstärke) auf dem gewählten Kanal. Mit MIDI wird automatisch eine
Tonleiter aktiviert. Eintrag 0 der Geräteliste ist ein **virtueller Port
„Therenect“**, z. B. mit FluidSynth:

```bash
sudo apt install fluidsynth fluid-soundfont-gm
fluidsynth -a pulseaudio -m alsa_seq -is /usr/share/sounds/sf2/FluidR3_GM.sf2 &
aconnect -l                      # Client-Nummern ansehen
aconnect Therenect:0 FLUID       # bzw. Nummern verwenden
```

Hardware-Synths per USB-MIDI erscheinen direkt in der Liste („Rescan MIDI ports“).

## Fehlersuche

- **`could not open Kinect` / `LIBUSB_ERROR_ACCESS`**: udev-Regel fehlt oder
  Gerät nach dem Installieren nicht neu angesteckt.
- **Kein Tiefenbild, Kamera belegt**: `lsmod | grep gspca_kinect` – Modul
  entladen (`sudo modprobe -r gspca_kinect`), Blacklist ist im Skript enthalten.
- **„no motor access, tilt disabled“**: Bei Modell 1473 und Kinect for Windows
  ist der Motor über libfreenect teils nicht erreichbar; das Theremin selbst
  funktioniert trotzdem.
- **USB-Fehler/Bildaussetzer**: anderen Port oder aktiven USB-2-Hub probieren,
  Kinect-Netzteil prüfen.
- **Audio knackt**: `-b 1024` oder `-b 2048`.

## Abweichungen vom Original

- Der GUI-Regler „Kinect angle“ bewegt jetzt tatsächlich den Motor (im Original
  wurde nur der Wert gespeichert); Bereich auf die mechanischen ±27° begrenzt.
- MIDI wird im Hauptthread statt im Audio-Callback gesendet; CC 7 nur bei
  Änderung, bei Frequenz < 32,7 Hz nur Note Off (statt Note On mit Note 0).
- Tonleiter-Quantisierung ist für negative Notennummern abgesichert, die
  Feldvisualisierung gegen Überlauf (255 − 256).
- Das 8-Bit-Tiefenbild nutzt dieselbe lineare Abbildung (Rohwert 200…1120 →
  255…0), mit der das Original die Antennendistanz rechnet.
- Der Kalman-Filter des Originals (`cvCreateKalman(1,1,0)`, F = H = 1,
  Q = R = 1e-8) ist ohne OpenCV nachgebaut; der dortige `memcpy` über die
  1×1-Matrix hinaus entfällt.
- Einstellungen werden beim Beenden gespeichert und beim Start geladen.

## Lizenz

GPL-2.0-or-later (siehe `COPYING`). Original © 2010 Martin Kaltenbrunner,
Interface Culture Lab, Kunstuniversität Linz. Originale Readme: `README.original.txt`.
