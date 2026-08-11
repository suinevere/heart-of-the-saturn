:; # === Linux & macOS execution block ===
:; # Polyglot: cmd.exe reads every ':' line as a label and falls through to the
:; # Windows block below; a POSIX shell reads ':' as the no-op builtin and runs
:; # the rest of the line. See data.bat's header. LF-only -- see .gitattributes.
:; #
:; # Two entry points, because this script is both a build step and the thing
:; # shipped to users:
:; #
:; #   update.bat prep   Step 1 only. Fills the CD skeleton so a compile has
:; #                     something to author a disc from. What saturn/compile.bat
:; #                     calls; safe to run on every build, a no-op once filled.
:; #   update.bat        Steps 1 and 2. Folds the data into the engine-only disc
:; #                     that ships alongside this script and emits a burnable
:; #                     bin/cue set. What a user of the release runs.
:; #
:; # The cue is written per-track (one FILE per bin, each INDEX 01 00:00:00)
:; # rather than as one interleaved image. That is the Redump multi-bin layout,
:; # it is what the rip this data came from already looks like, and it needs no
:; # sector arithmetic to stay matched to the bins beside it.
:; set -eu
:; cd "$(dirname "$0")"
:;
:; cfg() { sed -n "s/^$1=//p" CONFIG.ME | head -1 | tr -d '\r'; }
:; CD_DIR=$(cfg CD_DIR);         CD_DIR=${CD_DIR:-../../saturn/cd}
:; BASE_ISO=$(cfg BASE_ISO)
:; DISC_NAME=$(cfg DISC_NAME);   DISC_NAME=${DISC_NAME:-Heart of the Alien (USA)}
:; OUTPUT_DIR=$(cfg OUTPUT_DIR); OUTPUT_DIR=${OUTPUT_DIR:-./$DISC_NAME - Complete}
:; MUSIC_DIR="$CD_DIR/music"
:;
:; echo "== Step 1/2: install game data =="
:; case "${1:-}" in prep|--prep) sh ./data.bat; echo "Prepared."; exit 0 ;; esac
:; sh ./data.bat "$@"
:;
:; echo "== Step 2/2: build disc =="
:; [ -f "$BASE_ISO" ] || { echo "ERROR: base ISO not found: $BASE_ISO" >&2; exit 1; }
:; . lib/inject.sh
:; inject_data "$BASE_ISO" "$CD_DIR/data" "$OUTPUT_DIR" "$DISC_NAME"
:;
:; # inject_data leaves the data track as "<DISC_NAME>.bin". Rename it into the
:; # per-track scheme so every FILE line in the cue reads the same way.
:; # No '\' continuations anywhere in this half: the next line's ':;' prefix
:; # would splice into the command. Keep each statement on one line.
:; if [ -f "$OUTPUT_DIR/$DISC_NAME.bin" ]; then mv "$OUTPUT_DIR/$DISC_NAME.bin" "$OUTPUT_DIR/$DISC_NAME (Track 01).bin"; fi
:;
:; echo "Laying audio tracks"
:; CUE="$OUTPUT_DIR/$DISC_NAME.cue"
:; printf 'FILE "%s (Track 01).bin" BINARY\n  TRACK 01 MODE1/2352\n    INDEX 01 00:00:00\n' "$DISC_NAME" > "$CUE"
:;
:; # Cleaned tracklist to a temp file, then redirect at `done`: a pipe into the
:; # loop would need a continuation, and the loop body must not run in a
:; # subshell anyway -- `track` has to survive each iteration.
:; TL="$OUTPUT_DIR/.tracklist_tmp"
:; sed 's/^[[:space:]]*//;s/[[:space:]]*$//;/^$/d;/^#/d' "$MUSIC_DIR/tracklist" > "$TL"
:; track=2
:; while IFS= read -r entry; do
:;     src="$MUSIC_DIR/${entry%%:*}"
:;     nn=$(printf '%02d' "$track")
:;     dst="$OUTPUT_DIR/$DISC_NAME (Track $nn).bin"
:;     # Strip the 44-byte RIFF header extract_disc wrote; a cue track is raw
:;     # interleaved PCM with nothing in front of it.
:;     case "$src" in
:;         *.wav) tail -c +45 "$src" > "$dst" ;;
:;         *)     cp -f "$src" "$dst" ;;
:;     esac
:;     printf 'FILE "%s (Track %s).bin" BINARY\n  TRACK %s AUDIO\n' "$DISC_NAME" "$nn" "$nn" >> "$CUE"
:;     # 150 frames gap the first audio track where it follows data.
:;     [ "$track" -eq 2 ] && printf '    PREGAP 00:02:00\n' >> "$CUE"
:;     printf '    INDEX 01 00:00:00\n' >> "$CUE"
:;     track=$((track + 1))
:; done < "$TL"
:; rm -f "$TL"
:;
:; echo
:; echo "Ready to burn or mount: $OUTPUT_DIR/$DISC_NAME.cue"
:; exit

@ECHO OFF
REM === Windows execution block ===
SETLOCAL ENABLEEXTENSIONS ENABLEDELAYEDEXPANSION
CD /D "%~dp0"

SET "CD_DIR="
SET "BASE_ISO="
SET "DISC_NAME="
SET "OUTPUT_DIR="
FOR /F "usebackq eol=# tokens=1,* delims==" %%A IN ("CONFIG.ME") DO (
    IF "%%A"=="CD_DIR" SET "CD_DIR=%%B"
    IF "%%A"=="BASE_ISO" SET "BASE_ISO=%%B"
    IF "%%A"=="DISC_NAME" SET "DISC_NAME=%%B"
    IF "%%A"=="OUTPUT_DIR" SET "OUTPUT_DIR=%%B"
)
IF NOT DEFINED CD_DIR SET "CD_DIR=../../saturn/cd"
IF NOT DEFINED DISC_NAME SET "DISC_NAME=Heart of the Alien (USA)"
IF NOT DEFINED OUTPUT_DIR SET "OUTPUT_DIR=./%DISC_NAME% - Complete"
IF NOT DEFINED BASE_ISO SET "BASE_ISO=./%DISC_NAME%/%DISC_NAME%.iso"

REM Normalize forward slashes so IF EXIST / MKDIR behave.
SET "CD_DIR=%CD_DIR:/=\%"
SET "BASE_ISO=%BASE_ISO:/=\%"
SET "OUTPUT_DIR=%OUTPUT_DIR:/=\%"
SET "MUSIC_DIR=%CD_DIR%\music"

ECHO == Step 1/2: install game data ==
IF /I "%~1"=="prep" GOTO doprep
IF /I "%~1"=="--prep" GOTO doprep

CALL "%~dp0data.bat" %*
IF ERRORLEVEL 1 ( ECHO ERROR: data install failed & EXIT /B 1 )

ECHO == Step 2/2: build disc ==
REM Quote every expansion inside a parenthesized block: the disc name contains
REM ( ), which cmd would otherwise parse as block delimiters.
IF NOT EXIST "%BASE_ISO%" ( ECHO ERROR: base ISO not found: "%BASE_ISO%" & EXIT /B 1 )

powershell -NoProfile -ExecutionPolicy Bypass -File ".\lib\inject.ps1" -BaseIso "%BASE_ISO%" -DataDir "%CD_DIR%\data" -OutDir "%OUTPUT_DIR%" -Name "%DISC_NAME%" -Xorriso ".\bin\win\xorriso.exe" -Iso2raw ".\bin\win\iso2raw.exe"
IF ERRORLEVEL 1 ( ECHO ERROR: disc build failed & EXIT /B 1 )

IF EXIST "%OUTPUT_DIR%\%DISC_NAME%.bin" MOVE /Y "%OUTPUT_DIR%\%DISC_NAME%.bin" "%OUTPUT_DIR%\%DISC_NAME% (Track 01).bin" >NUL

ECHO Laying audio tracks
REM Header stripping and cue authoring in one PowerShell pass: cmd has no way to
REM drop 44 leading bytes from a binary file. Single quotes only inside
REM -Command -- cmd does not honour \" and an inner double quote reopens its
REM parser, so a '>' would become a redirect.
powershell -NoProfile -Command ^
 "$out='%OUTPUT_DIR%'; $name='%DISC_NAME%'; $music='%MUSIC_DIR%';" ^
 "$cue = Join-Path $out ($name + '.cue');" ^
 "Set-Content -LiteralPath $cue -Encoding ASCII -Value ('FILE \"' + $name + ' (Track 01).bin\" BINARY');" ^
 "Add-Content -LiteralPath $cue -Encoding ASCII -Value '  TRACK 01 MODE1/2352';" ^
 "Add-Content -LiteralPath $cue -Encoding ASCII -Value '    INDEX 01 00:00:00';" ^
 "$t=2; Get-Content -LiteralPath (Join-Path $music 'tracklist') | ForEach-Object { $l=$_.Trim(); if ($l -and -not $l.StartsWith('#')) {" ^
 "  $src = Join-Path $music ($l -split ':')[0]; $nn = '{0:D2}' -f $t;" ^
 "  $dst = Join-Path $out ($name + ' (Track ' + $nn + ').bin');" ^
 "  if ($src -like '*.wav') { $b=[IO.File]::ReadAllBytes($src); [IO.File]::WriteAllBytes($dst, $b[44..($b.Length-1)]) } else { Copy-Item -LiteralPath $src -Destination $dst -Force };" ^
 "  Add-Content -LiteralPath $cue -Encoding ASCII -Value ('FILE \"' + $name + ' (Track ' + $nn + ').bin\" BINARY');" ^
 "  Add-Content -LiteralPath $cue -Encoding ASCII -Value ('  TRACK ' + $nn + ' AUDIO');" ^
 "  if ($t -eq 2) { Add-Content -LiteralPath $cue -Encoding ASCII -Value '    PREGAP 00:02:00' };" ^
 "  Add-Content -LiteralPath $cue -Encoding ASCII -Value '    INDEX 01 00:00:00'; $t++ } }"
IF ERRORLEVEL 1 ( ECHO ERROR: audio track layout failed & EXIT /B 1 )

ECHO.
ECHO Ready to burn or mount: "%OUTPUT_DIR%\%DISC_NAME%.cue"
ENDLOCAL
EXIT /B 0

:doprep
CALL "%~dp0data.bat"
IF ERRORLEVEL 1 ( ECHO ERROR: data install failed & EXIT /B 1 )
ECHO Prepared.
ENDLOCAL
EXIT /B 0
