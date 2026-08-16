:; # === Linux & macOS execution block ===
:; # This file is a polyglot: cmd.exe reads every ':' line as a label and falls
:; # through to the Windows block below, while a POSIX shell reads ':' as the
:; # no-op builtin and runs the rest of the line. Keep the two halves in step.
:; # The file must stay LF-only (see .gitattributes) -- a CR would land inside
:; # these commands. The single-line prologues in saturn/compile.bat get away
:; # with CRLF because their trailing ';' eats the CR; a multi-line block cannot.
:; #
:; # Fills the CD skeleton from your own copy of the disc. Heart of the Alien's
:; # data is not a flat archive the way a DOS release would be -- all 19 blobs
:; # live on the data track of the Sega CD disc, and the music is 41 CD-DA
:; # tracks -- so the source here is a bin/cue rip and the work is done by
:; # extract_disc, not by unzipping.
:; set -eu
:; cd "$(dirname "$0")"
:;
:; cfg() { sed -n "s/^$1=//p" CONFIG.ME | head -1 | tr -d '\r'; }
:; ASSET_DIR=$(cfg ASSET_DIR);     ASSET_DIR=${ASSET_DIR:-./assets}
:; RIP_DIR=$(cfg RIP_DIR);         RIP_DIR=${RIP_DIR:-../../cd}
:; CD_DIR=$(cfg CD_DIR);           CD_DIR=${CD_DIR:-../../saturn/cd}
:; GAME_URL=$(cfg GAME_URL)
:; EXTRACT=$(cfg EXTRACT_DISC);    EXTRACT=${EXTRACT:-../extract_disc}
:;
:; DATA_DIR="$CD_DIR/data"
:; MUSIC_DIR="$CD_DIR/music"
:; mkdir -p "$ASSET_DIR" "$DATA_DIR" "$MUSIC_DIR"
:;
:; # The 19 blobs disc_manifest.h lists. Counting them is the install check:
:; # a partial extract is the failure worth catching, not a missing directory.
:; # Three files share the extension without being ours: 0.bin is the SEGA boot
:; # header the build supplies, and ANOTHER.BIN and memlist.bin are Part I's,
:; # installed beside these by tools/another/fetch.sh and the setup kit's part1
:; # step. Counting either of those turns a correct extract into "installed 20"
:; # and, before that, defeats the already-installed guard below into
:; # re-extracting the whole rip on every build.
:; count_blobs() { find "$DATA_DIR" -maxdepth 1 -type f \( -name '*.BIN' -o -name '*.bin' \) ! -iname '0.bin' ! -iname 'another.bin' ! -iname 'memlist.bin' 2>/dev/null | wc -l | tr -d ' '; }
:;
:; if [ "$(count_blobs)" -eq 19 ] && [ "${1:-}" != "-f" ]; then
:;     echo "Game data already installed in $DATA_DIR (pass -f to rebuild)."; exit 0
:; fi
:;
:; # Locate a cue. Prefer one already unpacked in ASSET_DIR -- that is the cache,
:; # and re-unpacking a 400 MB rip on every run is the thing it exists to avoid.
:; # RIP_DIR is the checkout's own rip, so a source tree repairs itself without
:; # anyone staging a second copy of the disc.
:; find_cue() {
:;     for d in "$ASSET_DIR" "$RIP_DIR"; do
:;         [ -d "$d" ] || continue
:;         c=$(find "$d" -type f -iname '*.cue' 2>/dev/null | sort | head -1)
:;         [ -n "$c" ] && { echo "$c"; return 0; }
:;     done
:; }
:; # One line on purpose: a trailing '\' would splice the next line's ':;' prefix
:; # into the command, and find would take the ':' as a path. No continuations.
:; count_audio() { find "$MUSIC_DIR" -maxdepth 1 -type f \( -iname '*.wav' -o -iname '*.mp3' -o -iname '*.flac' \) 2>/dev/null | wc -l | tr -d ' '; }
:; CUE=$(find_cue)
:;
:; if [ -z "$CUE" ]; then
:;     ARC=$(find "$ASSET_DIR" -maxdepth 1 -type f \( -iname '*.7z' -o -iname '*.zip' \) | sort | head -1)
:;     if [ -z "$ARC" ] && [ -n "$GAME_URL" ]; then
:;         # Fixed cache name rather than the URL's basename: that basename is
:;         # still percent-encoded, and a stable name is what makes the "already
:;         # cached" check above match on the next run.
:;         case "$GAME_URL" in *.zip) ARC="$ASSET_DIR/game.zip" ;; *) ARC="$ASSET_DIR/game.7z" ;; esac
:;         echo "Downloading $GAME_URL"
:;         # .part until it is whole -- set -e aborts before the mv on failure.
:;         if command -v curl >/dev/null 2>&1; then curl -fL --retry 3 -o "$ARC.part" "$GAME_URL"
:;         elif command -v wget >/dev/null 2>&1; then wget -q -O "$ARC.part" "$GAME_URL"
:;         else echo "ERROR: need curl or wget on PATH" >&2; exit 1; fi
:;         mv "$ARC.part" "$ARC"
:;     fi
:;     if [ -n "$ARC" ]; then
:;         echo "Unpacking $(basename "$ARC")"
:;         if command -v 7z >/dev/null 2>&1; then 7z x -y -o"$ASSET_DIR" "$ARC" >/dev/null
:;         elif command -v 7za >/dev/null 2>&1; then 7za x -y -o"$ASSET_DIR" "$ARC" >/dev/null
:;         elif command -v bsdtar >/dev/null 2>&1; then bsdtar -xf "$ARC" -C "$ASSET_DIR"
:;         elif command -v tar >/dev/null 2>&1; then tar -xf "$ARC" -C "$ASSET_DIR"
:;         else echo "ERROR: need 7z, bsdtar or tar to unpack $ARC" >&2; exit 1; fi
:;         CUE=$(find_cue)
:;     fi
:; fi
:;
:; if [ -z "$CUE" ]; then
:;     echo "ERROR: no disc image found." >&2
:;     echo "" >&2
:;     echo "GAME_URL in CONFIG.ME is empty, or what it fetched held no .cue." >&2
:;     echo "Put a copy of the disc in" >&2
:;     echo "  $ASSET_DIR" >&2
:;     echo "as a .cue with its .bin tracks beside it, or as a .7z/.zip holding one," >&2
:;     echo "then run this script again. A Redump-layout rip (one bin per track) is" >&2
:;     echo "what extract_disc expects." >&2
:;     exit 1
:; fi
:;
:; [ -x "$EXTRACT" ] || EXTRACT="$EXTRACT.exe"
:; if [ ! -x "$EXTRACT" ]; then
:;     echo "ERROR: extractor not found or not executable: $EXTRACT" >&2
:;     echo "In a checkout, build it first:  sh ../build.sh" >&2
:;     exit 1
:; fi
:;
:; # A full extract every time. The audio pass rewrites the music files and the
:; # tracklist from the rip, which is the authoritative copy -- regenerating
:; # both is the repair, not a hazard to work around.
:; echo "Extracting from $(basename "$CUE")"
:; "$EXTRACT" "$CUE" "$CD_DIR"
:;
:; n=$(count_blobs)
:; [ "$n" -eq 19 ] || { echo "ERROR: expected 19 data blobs, installed $n" >&2; exit 1; }
:; echo "Installed $n data blobs into $DATA_DIR; $(count_audio) audio tracks in $MUSIC_DIR"
:; exit

@ECHO OFF
REM === Windows execution block ===
REM See the ':' prologue above for why this file is a polyglot.
SETLOCAL ENABLEEXTENSIONS ENABLEDELAYEDEXPANSION
CD /D "%~dp0"

SET "ASSET_DIR="
SET "RIP_DIR="
SET "CD_DIR="
SET "GAME_URL="
SET "EXTRACT_DISC="
FOR /F "usebackq eol=# tokens=1,* delims==" %%A IN ("CONFIG.ME") DO (
    IF "%%A"=="ASSET_DIR" SET "ASSET_DIR=%%B"
    IF "%%A"=="RIP_DIR" SET "RIP_DIR=%%B"
    IF "%%A"=="CD_DIR" SET "CD_DIR=%%B"
    IF "%%A"=="GAME_URL" SET "GAME_URL=%%B"
    IF "%%A"=="EXTRACT_DISC" SET "EXTRACT_DISC=%%B"
)
IF NOT DEFINED ASSET_DIR SET "ASSET_DIR=./assets"
IF NOT DEFINED RIP_DIR SET "RIP_DIR=../../cd"
IF NOT DEFINED CD_DIR SET "CD_DIR=../../saturn/cd"
IF NOT DEFINED EXTRACT_DISC SET "EXTRACT_DISC=../extract_disc"
SET "RIP_DIR=%RIP_DIR:/=\%"

REM Normalize forward slashes so MKDIR / IF EXIST behave, then resolve to full
REM paths -- extract_disc runs with its own idea of the cwd.
SET "ASSET_DIR=%ASSET_DIR:/=\%"
SET "CD_DIR=%CD_DIR:/=\%"
SET "EXTRACT_DISC=%EXTRACT_DISC:/=\%"
IF NOT EXIST "%ASSET_DIR%" MKDIR "%ASSET_DIR%"
IF NOT EXIST "%CD_DIR%\data" MKDIR "%CD_DIR%\data"
IF NOT EXIST "%CD_DIR%\music" MKDIR "%CD_DIR%\music"
FOR %%I IN ("%ASSET_DIR%") DO SET "ASSETS=%%~fI"
FOR %%I IN ("%CD_DIR%") DO SET "CDROOT=%%~fI"
SET "DATA_DIR=%CDROOT%\data"
SET "MUSIC_DIR=%CDROOT%\music"

CALL :countblobs
IF "%BLOBS%"=="19" IF NOT "%~1"=="-f" (
    ECHO Game data already installed in "%DATA_DIR%" ^(pass -f to rebuild^).
    EXIT /B 0
)

REM Prefer a cue already unpacked in ASSET_DIR -- that is the cache.
CALL :findcue
IF DEFINED CUE GOTO gotcue

SET "ARC="
FOR %%F IN ("%ASSETS%\*.7z" "%ASSETS%\*.zip") DO IF NOT DEFINED ARC SET "ARC=%%~fF"

REM Fixed cache name and a .part sibling -- see the POSIX half.
SET "ARCNAME=game.7z"
IF /I "%GAME_URL:~-4%"==".zip" SET "ARCNAME=game.zip"
IF NOT DEFINED ARC IF DEFINED GAME_URL (
    ECHO Downloading %GAME_URL%
    SET "ARC=%ASSETS%\%ARCNAME%"
    curl -fL --retry 3 -o "!ARC!.part" "%GAME_URL%"
    IF ERRORLEVEL 1 ( DEL /Q "!ARC!.part" 2>NUL & ECHO ERROR: download failed & EXIT /B 1 )
    MOVE /Y "!ARC!.part" "!ARC!" >NUL
)

IF DEFINED ARC (
    ECHO Unpacking "%ARC%"
    REM tar.exe on Windows 10+ is bsdtar and reads 7z; 7z.exe wins if installed.
    WHERE 7z >NUL 2>&1 && ( 7z x -y -o"%ASSETS%" "%ARC%" >NUL ) || ( tar -xf "%ARC%" -C "%ASSETS%" )
    IF ERRORLEVEL 1 ( ECHO ERROR: unpack failed & EXIT /B 1 )
    CALL :findcue
)

IF NOT DEFINED CUE (
    ECHO ERROR: no disc image found.
    ECHO.
    ECHO GAME_URL in CONFIG.ME is empty, or what it fetched held no .cue.
    ECHO Put a copy of the disc in
    ECHO   "%ASSETS%"
    ECHO as a .cue with its .bin tracks beside it, or as a .7z/.zip holding one,
    ECHO then run this script again. A Redump-layout rip ^(one bin per track^) is
    ECHO what extract_disc expects.
    EXIT /B 1
)

:gotcue
IF NOT EXIST "%EXTRACT_DISC%" SET "EXTRACT_DISC=%EXTRACT_DISC%.exe"
IF NOT EXIST "%EXTRACT_DISC%" (
    ECHO ERROR: extractor not found: "%EXTRACT_DISC%"
    ECHO In a checkout, build it first:  bash ..\build.sh
    EXIT /B 1
)

REM A full extract every time -- see the POSIX half.
ECHO Extracting from "%CUE%"
"%EXTRACT_DISC%" "%CUE%" "%CDROOT%"
IF ERRORLEVEL 1 ( ECHO ERROR: extraction failed & EXIT /B 1 )

CALL :countblobs
IF NOT "%BLOBS%"=="19" ( ECHO ERROR: expected 19 data blobs, installed %BLOBS% & EXIT /B 1 )
CALL :countaudio
ECHO Installed %BLOBS% data blobs into "%DATA_DIR%"; %AUDIO% audio tracks in "%MUSIC_DIR%"

ENDLOCAL
EXIT /B 0

REM 0.bin is the SEGA boot header the build supplies, and ANOTHER.BIN and
REM memlist.bin are Part I's -- none of the three is a game blob. The POSIX
REM half excludes the same three by name, for the reason given there.
:countblobs
SET /A BLOBS=0
FOR %%F IN ("%DATA_DIR%\*.bin") DO (
    IF /I NOT "%%~nxF"=="0.bin" IF /I NOT "%%~nxF"=="ANOTHER.BIN" IF /I NOT "%%~nxF"=="memlist.bin" SET /A BLOBS+=1
)
EXIT /B 0

REM ASSET_DIR first, then the checkout's own rip -- see the POSIX half.
:findcue
SET "CUE="
FOR /R "%ASSETS%" %%F IN (*.cue) DO IF NOT DEFINED CUE SET "CUE=%%~fF"
IF NOT DEFINED CUE IF EXIST "%RIP_DIR%" FOR /R "%RIP_DIR%" %%F IN (*.cue) DO IF NOT DEFINED CUE SET "CUE=%%~fF"
EXIT /B 0

REM Anything sox can read counts as music already in place.
:countaudio
SET /A AUDIO=0
FOR %%F IN ("%MUSIC_DIR%\*.wav" "%MUSIC_DIR%\*.mp3" "%MUSIC_DIR%\*.flac") DO SET /A AUDIO+=1
EXIT /B 0
