# GaggiMate "Adam" – automatischer Display-Mod

Dieser Fork baut bei jedem neuen **offiziellen Stable-Release** von
[jniebuhr/gaggimate](https://github.com/jniebuhr/gaggimate) automatisch eine
Display-Firmware mit

- eigenem Standby-Bild (`mod/gaggimate_standby.png`, eingebunden über `mod/patch.py`)
- `/flash`-Upload-Seite (`mod/gaggimate_fw_upload_endpoint.patch`, Fallback `mod/apply_upload_fallback.py`)
- Update-Quelle = dieser Fork (statt Upstream)
- **Jungle-Look**: reduzierte Brew-/Wasser-/Dampf-/Mahlen-Screens mit Verlaufs-Hintergrund, Wischen zwischen den Modi, Sprung auf Dampf nach dem Bezug (`mod/apply_jungle.py` + `mod/jungle/`; passt der Code nach einem Upstream-Umbau nicht mehr, wird ohne Jungle-Look gebaut und das steht in den Release-Notes)

und veröffentlicht sie hier als Release – zusammen mit der **unveränderten**
offiziellen Controller-Firmware (`board-firmware.bin`, 1:1 von Upstream kopiert).
Im WebUI (Einstellungen → System, Kanal „stable") lädt der normale **Update**-Knopf
Display + Controller aus diesem Fork; das Standby-Bild bleibt.

## Ablauf

Der Ablauf liegt auf dem Standard-Zweig `master` (`.github/workflows/adam-build.yml` – nur dort läuft der Zeitplan), die Mod-Dateien liegen hier auf `adam`. Die Release-Tags zeigen auf diesen Zweig, der keine Abläufe enthält, damit ein Tag nichts auslöst.

Täglich 05:23 UTC (und per „Run workflow"):

1. neuesten Upstream-Release ermitteln; gibt es ihn hier schon → Ende
2. offizielle `board-firmware.bin` + `display-firmware.bin` laden
3. Upstream-Quellcode exakt dieses Tags holen, Mod anwenden
4. Version = exakt der Upstream-Tag (sonst Update-Schleife), Web-UI + Display bauen
5. Prüfungen: Version, Web-UI eingebettet, Größe ≈ offizielles Image + ~696 KB (mit Jungle-Look etwas mehr)
6. Release `vX.Y.Z` mit `display-firmware.bin` + `board-firmware.bin` anlegen

Passt der Mod nach einem Upstream-Umbau nicht mehr, schlägt der Lauf fehl (Mail von
GitHub), es wird **nichts** veröffentlicht und das Display bleibt auf der letzten Version.

## Hinweise

- Der Fork muss **öffentlich** bleiben (das Display lädt ohne Anmeldung).
- Manuell eine bestimmte Version bauen: Actions → adam-build → Run workflow → Tag eintragen.
- Notfall ohne Update-Knopf: `http://gaggimate.local/flash` (APPONLY-Datei hochladen).
- Nicht „Update Display" aus dem **Upstream**-Kanal drücken – das würde Bild + Upload-Seite überschreiben.
