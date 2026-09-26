:; set -eu
:; cd "$(dirname "$0")"
:;
:; DISC_NAME="Heart of the Alien - Out of This World Parts 1 and 2 (USA)"
:; RIP_DIR="./(put sega multi-bin and cue here)"
:; PART1_DIR="./(put bank and memlist files here)"
:; BASE_ISO="./bin/$DISC_NAME.iso"
:; TMP_DIR=./tmp
:; DATA_DIR="$TMP_DIR"
:; MUSIC_DIR="$TMP_DIR/music"
:; OUTPUT_DIR="./$DISC_NAME"
:;
:; mkdir -p "$DATA_DIR" "$MUSIC_DIR" "$RIP_DIR" "$PART1_DIR"
:;
:; UNAME_S=$(uname -s)
:; EXTRACT=./bin/win/extract_disc.exe
:; if [ "$UNAME_S" = Linux ]; then EXTRACT=./bin/lin/extract_disc; fi
:; if [ "$UNAME_S" = Darwin ] && [ "$(uname -m)" = arm64 ]; then EXTRACT=./bin/mac/arm64/extract_disc; fi
:; if [ "$UNAME_S" = Darwin ] && [ "$(uname -m)" != arm64 ]; then EXTRACT=./bin/mac/amd64/extract_disc; fi
:; [ -x "$EXTRACT" ] || { echo "ERROR: no extractor for this platform at $EXTRACT" >&2; exit 1; }
:;
:; ensure_xorriso() {
:;     if command -v xorriso >/dev/null 2>&1; then return 0; fi
:;     INSTALL_CMD=""
:;     if [ "$UNAME_S" = Darwin ]; then
:;         if command -v brew >/dev/null 2>&1; then INSTALL_CMD="brew install xorriso"; fi
:;     elif command -v apt-get >/dev/null 2>&1; then INSTALL_CMD="sudo apt-get install -y xorriso"
:;     elif command -v dnf >/dev/null 2>&1; then INSTALL_CMD="sudo dnf install -y xorriso"
:;     elif command -v pacman >/dev/null 2>&1; then INSTALL_CMD="sudo pacman -S --noconfirm xorriso"
:;     elif command -v zypper >/dev/null 2>&1; then INSTALL_CMD="sudo zypper install -y xorriso"
:;     fi
:;     echo "xorriso is not installed, and building the disc needs it."
:;     if [ -z "$INSTALL_CMD" ]; then
:;         echo "Install xorriso with your package manager, then run this again." >&2
:;         return 1
:;     fi
:;     if [ ! -t 0 ]; then
:;         echo "Install it with: $INSTALL_CMD" >&2
:;         return 1
:;     fi
:;     printf 'Install it now with "%s"? [y/N] ' "$INSTALL_CMD"
:;     read -r REPLY || REPLY=n
:;     AGREED=0
:;     case "$REPLY" in y|Y|yes|Yes|YES) AGREED=1 ;; esac
:;     if [ "$AGREED" -ne 1 ]; then echo "Not installing. Run that yourself, then run this again." >&2; return 1; fi
:;     echo "Running: $INSTALL_CMD"
:;     if ! $INSTALL_CMD; then echo "That did not work. Install xorriso yourself, then run this again." >&2; return 1; fi
:;     if ! command -v xorriso >/dev/null 2>&1; then echo "xorriso still is not on PATH." >&2; return 1; fi
:;     echo "xorriso installed."
:;     return 0
:; }
:;
:; if [ "$UNAME_S" = Linux ] || [ "$UNAME_S" = Darwin ]; then ensure_xorriso || exit 1; fi
:;
:; unpack() {
:;     echo "Unpacking $(basename "$1")"
:;     if command -v 7z >/dev/null 2>&1; then 7z x -y -o"$2" "$1" >/dev/null
:;     elif command -v 7za >/dev/null 2>&1; then 7za x -y -o"$2" "$1" >/dev/null
:;     elif command -v bsdtar >/dev/null 2>&1; then bsdtar -xf "$1" -C "$2"
:;     elif command -v unzip >/dev/null 2>&1; then unzip -qo "$1" -d "$2"
:;     else echo "ERROR: need 7z, bsdtar or unzip to unpack $1" >&2; exit 1; fi
:; }
:;
:; echo "== Step 1/3: Heart of the Alien, from your Sega CD rip =="
:; CUE=$(find "$RIP_DIR" -type f -iname '*.cue' 2>/dev/null | sort | head -1)
:; if [ -z "$CUE" ]; then
:;     ARC=$(find "$RIP_DIR" -maxdepth 1 -type f \( -iname '*.7z' -o -iname '*.zip' \) 2>/dev/null | sort | head -1)
:;     if [ -n "$ARC" ]; then unpack "$ARC" "$RIP_DIR"; CUE=$(find "$RIP_DIR" -type f -iname '*.cue' 2>/dev/null | sort | head -1); fi
:; fi
:; if [ -z "$CUE" ]; then
:;     echo "ERROR: no disc image found in" >&2
:;     echo "  $RIP_DIR" >&2
:;     echo "" >&2
:;     echo "Put your own copy of the Sega CD disc in that folder: one .bin per" >&2
:;     echo "track and a .cue naming them in order, unpacked or still as a .7z or" >&2
:;     echo ".zip. This kit never downloads game data." >&2
:;     exit 1
:; fi
:; echo "Extracting from $(basename "$CUE")"
:; "$EXTRACT" "$CUE" "$TMP_DIR" . music
:; BLOBS=$(find "$DATA_DIR" -maxdepth 1 -type f \( -name '*.BIN' -o -name '*.bin' \) ! -iname '0.bin' ! -iname 'another.bin' ! -iname 'memlist.bin' 2>/dev/null | wc -l | tr -d ' ')
:; [ "$BLOBS" -eq 19 ] || { echo "ERROR: expected 19 data blobs from the rip, got $BLOBS" >&2; exit 1; }
:;
:; echo "== Step 2/3: Out of This World, from your DOS files =="
:; P1ARC=$(find "$PART1_DIR" -maxdepth 1 -type f \( -iname '*.7z' -o -iname '*.zip' \) 2>/dev/null | sort | head -1)
:; if [ -n "$P1ARC" ]; then unpack "$P1ARC" "$PART1_DIR"; fi
:; P1LIST="$DATA_DIR/.part1list"
:; find "$PART1_DIR" -type f \( -iname 'bank??' -o -iname 'memlist.bin' \) 2>/dev/null | sort > "$P1LIST"
:; N1=0
:; while IFS= read -r f; do b=$(basename "$f" | tr 'A-Z' 'a-z'); cp -f "$f" "$DATA_DIR/$b"; N1=$((N1 + 1)); done < "$P1LIST"
:; rm -f "$P1LIST"
:; if [ "$N1" -eq 14 ]; then echo "Installed 14 files -- OUT OF THIS WORLD will be playable."
:; elif [ "$N1" -eq 0 ]; then echo "Nothing in $PART1_DIR -- OUT OF THIS WORLD will not be playable on this disc."
:; else echo "ERROR: expected 14 files there (bank01..bank0d and memlist.bin), found $N1" >&2; exit 1
:; fi
:;
:; echo "== Step 3/3: build the disc =="
:; [ -f "$BASE_ISO" ] || { echo "ERROR: the kit's disc image is missing: $BASE_ISO" >&2; exit 1; }
:; . bin/lib/inject.sh
:; inject_data "$BASE_ISO" "$DATA_DIR" "$OUTPUT_DIR" "$DISC_NAME"
:;
:; if [ -f "$OUTPUT_DIR/$DISC_NAME.bin" ]; then mv "$OUTPUT_DIR/$DISC_NAME.bin" "$OUTPUT_DIR/$DISC_NAME (Track 01).bin"; fi
:;
:; echo "Laying audio tracks"
:; CUEOUT="$OUTPUT_DIR/$DISC_NAME.cue"
:; printf 'FILE "%s (Track 01).bin" BINARY\n  TRACK 01 MODE1/2352\n    INDEX 01 00:00:00\n' "$DISC_NAME" > "$CUEOUT"
:; TL="$OUTPUT_DIR/.tracklist_tmp"
:; TRACKLIST="$MUSIC_DIR/tracklist"
:; [ -f "$TRACKLIST" ] || TRACKLIST=./bin/music/tracklist
:; sed 's/^[[:space:]]*//;s/[[:space:]]*$//;/^$/d;/^#/d' "$TRACKLIST" > "$TL"
:; track=2
:; while IFS= read -r entry; do
:;     src="$MUSIC_DIR/${entry%%:*}"
:;     nn=$(printf '%02d' "$track")
:;     dst="$OUTPUT_DIR/$DISC_NAME (Track $nn).bin"
:;     if [ "${src%.wav}" != "$src" ]; then tail -c +45 "$src" > "$dst"; else cp -f "$src" "$dst"; fi
:;     printf 'FILE "%s (Track %s).bin" BINARY\n  TRACK %s AUDIO\n' "$DISC_NAME" "$nn" "$nn" >> "$CUEOUT"
:;     [ "$track" -eq 2 ] && printf '    PREGAP 00:02:00\n' >> "$CUEOUT"
:;     printf '    INDEX 01 00:00:00\n' >> "$CUEOUT"
:;     track=$((track + 1))
:; done < "$TL"
:; rm -f "$TL"
:;
:; rm -rf "$TMP_DIR"
:;
:; echo
:; echo "Ready to burn or mount: $OUTPUT_DIR/$DISC_NAME.cue"
:; exit

@ECHO OFF
SETLOCAL ENABLEEXTENSIONS ENABLEDELAYEDEXPANSION
CD /D "%~dp0"

SET "DISC_NAME=Heart of the Alien - Out of This World Parts 1 and 2 (USA)"
SET "RIP_DIR=(put sega multi-bin and cue here)"
SET "PART1_DIR=(put bank and memlist files here)"
SET "BASE_ISO=bin\%DISC_NAME%.iso"
SET "TMP_DIR=tmp"
SET "DATA_DIR=%TMP_DIR%"
SET "MUSIC_DIR=%TMP_DIR%\music"
SET "OUTPUT_DIR=%DISC_NAME%"
SET "EXTRACT=bin\win\extract_disc.exe"

IF NOT EXIST "%TMP_DIR%"   MKDIR "%TMP_DIR%"
IF NOT EXIST "%MUSIC_DIR%" MKDIR "%MUSIC_DIR%"
IF NOT EXIST "%RIP_DIR%"   MKDIR "%RIP_DIR%"
IF NOT EXIST "%PART1_DIR%" MKDIR "%PART1_DIR%"
IF NOT EXIST "%EXTRACT%" ( ECHO ERROR: the kit's extractor is missing: "%EXTRACT%" & EXIT /B 1 )

ECHO == Step 1/3: Heart of the Alien, from your Sega CD rip ==
SET "CUE="
FOR /R "%RIP_DIR%" %%F IN (*.cue) DO IF NOT DEFINED CUE SET "CUE=%%~fF"
IF NOT DEFINED CUE CALL :unpackrip
IF NOT DEFINED CUE (
    ECHO ERROR: no disc image found in
    ECHO   "%RIP_DIR%"
    ECHO.
    ECHO Put your own copy of the Sega CD disc in that folder: one .bin per
    ECHO track and a .cue naming them in order, unpacked or still as a .zip.
    ECHO This kit never downloads game data.
    EXIT /B 1
)
ECHO Extracting from "!CUE!"
"%EXTRACT%" "!CUE!" "%TMP_DIR%" "." "music"
IF ERRORLEVEL 1 ( ECHO ERROR: extraction failed & EXIT /B 1 )

SET /A BLOBS=0
FOR %%F IN ("%DATA_DIR%\*.bin") DO CALL :countblob "%%~nxF"
IF NOT "!BLOBS!"=="19" ( ECHO ERROR: expected 19 data blobs from the rip, got !BLOBS! & EXIT /B 1 )

ECHO == Step 2/3: Out of This World, from your DOS files ==
SET "P1ARC="
FOR %%F IN ("%PART1_DIR%\*.7z" "%PART1_DIR%\*.zip") DO IF NOT DEFINED P1ARC SET "P1ARC=%%~fF"
IF DEFINED P1ARC CALL :unpackpart1
SET /A N1=0
FOR /R "%PART1_DIR%" %%F IN (bank?? memlist.bin) DO CALL :installpart1 "%%~fF" "%%~nxF"
IF "!N1!"=="14" (
    ECHO Installed 14 files -- OUT OF THIS WORLD will be playable.
) ELSE IF "!N1!"=="0" (
    ECHO Nothing in "%PART1_DIR%" -- OUT OF THIS WORLD will not be playable on this disc.
) ELSE (
    ECHO ERROR: expected 14 files there ^(bank01..bank0d and memlist.bin^), found !N1!
    EXIT /B 1
)

ECHO == Step 3/3: build the disc ==
IF NOT EXIST "%BASE_ISO%" ( ECHO ERROR: the kit's disc image is missing: "%BASE_ISO%" & EXIT /B 1 )
powershell -NoProfile -ExecutionPolicy Bypass -File ".\bin\lib\inject.ps1" -BaseIso "%BASE_ISO%" -DataDir "%DATA_DIR%" -OutDir "%OUTPUT_DIR%" -Name "%DISC_NAME%" -Xorriso ".\bin\win\xorriso.exe" -Iso2raw ".\bin\win\iso2raw.exe"
IF ERRORLEVEL 1 ( ECHO ERROR: disc build failed & EXIT /B 1 )

IF EXIST "%OUTPUT_DIR%\%DISC_NAME%.bin" MOVE /Y "%OUTPUT_DIR%\%DISC_NAME%.bin" "%OUTPUT_DIR%\%DISC_NAME% (Track 01).bin" >NUL

ECHO Laying audio tracks
powershell -NoProfile -Command ^
 "$out='%OUTPUT_DIR%'; $name='%DISC_NAME%'; $music='%MUSIC_DIR%';" ^
 "$cue = Join-Path $out ($name + '.cue');" ^
 "Set-Content -LiteralPath $cue -Encoding ASCII -Value ('FILE \"' + $name + ' (Track 01).bin\" BINARY');" ^
 "Add-Content -LiteralPath $cue -Encoding ASCII -Value '  TRACK 01 MODE1/2352';" ^
 "Add-Content -LiteralPath $cue -Encoding ASCII -Value '    INDEX 01 00:00:00';" ^
 "$tl = Join-Path $music 'tracklist';" ^
 "if (-not (Test-Path -LiteralPath $tl)) { $tl = Join-Path 'bin' (Join-Path 'music' 'tracklist') };" ^
 "$t=2; Get-Content -LiteralPath $tl | ForEach-Object { $l=$_.Trim(); if ($l -and -not $l.StartsWith('#')) {" ^
 "  $src = Join-Path $music ($l -split ':')[0]; $nn = '{0:D2}' -f $t;" ^
 "  $dst = Join-Path $out ($name + ' (Track ' + $nn + ').bin');" ^
 "  if ($src -like '*.wav') { $b=[IO.File]::ReadAllBytes($src); [IO.File]::WriteAllBytes($dst, $b[44..($b.Length-1)]) } else { Copy-Item -LiteralPath $src -Destination $dst -Force };" ^
 "  Add-Content -LiteralPath $cue -Encoding ASCII -Value ('FILE \"' + $name + ' (Track ' + $nn + ').bin\" BINARY');" ^
 "  Add-Content -LiteralPath $cue -Encoding ASCII -Value ('  TRACK ' + $nn + ' AUDIO');" ^
 "  if ($t -eq 2) { Add-Content -LiteralPath $cue -Encoding ASCII -Value '    PREGAP 00:02:00' };" ^
 "  Add-Content -LiteralPath $cue -Encoding ASCII -Value '    INDEX 01 00:00:00'; $t++ } }"
IF ERRORLEVEL 1 ( ECHO ERROR: audio track layout failed & EXIT /B 1 )

RMDIR /S /Q "%TMP_DIR%" 2>NUL

ECHO.
ECHO Ready to burn or mount: "%OUTPUT_DIR%\%DISC_NAME%.cue"
ENDLOCAL
EXIT /B 0

:unpackrip
SET "ARC="
FOR %%F IN ("%RIP_DIR%\*.7z" "%RIP_DIR%\*.zip") DO IF NOT DEFINED ARC SET "ARC=%%~fF"
IF NOT DEFINED ARC GOTO :eof
ECHO Unpacking "!ARC!"
powershell -NoProfile -Command "Expand-Archive -LiteralPath '!ARC!' -DestinationPath '%RIP_DIR%' -Force"
IF ERRORLEVEL 1 ( ECHO Could not unpack "!ARC!" -- a .7z needs 7-Zip, or unpack it yourself. )
FOR /R "%RIP_DIR%" %%F IN (*.cue) DO IF NOT DEFINED CUE SET "CUE=%%~fF"
GOTO :eof

:unpackpart1
ECHO Unpacking "!P1ARC!"
powershell -NoProfile -Command "Expand-Archive -LiteralPath '!P1ARC!' -DestinationPath '%PART1_DIR%' -Force"
GOTO :eof

:countblob
IF /I "%~1"=="0.bin" GOTO :eof
IF /I "%~1"=="ANOTHER.BIN" GOTO :eof
IF /I "%~1"=="MEMLIST.BIN" GOTO :eof
SET /A BLOBS+=1
GOTO :eof

:installpart1
powershell -NoProfile -Command "$n='%~2'.ToLower(); Copy-Item -LiteralPath '%~1' -Destination (Join-Path '%DATA_DIR%' $n) -Force"
SET /A N1+=1
GOTO :eof
