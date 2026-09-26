:; set -eu
:; cd "$(dirname "$0")"
:;
:; cfg() { sed -n "s/^$1=//p" CONFIG.ME | head -1 | tr -d '\r'; }
:; ASSET_DIR=$(cfg ASSET_DIR);     ASSET_DIR=${ASSET_DIR:-./assets}
:; RIP_DIR=$(cfg RIP_DIR);         RIP_DIR=${RIP_DIR:-../../cd}
:; CD_DIR=$(cfg CD_DIR);           CD_DIR=${CD_DIR:-../../saturn/cd}
:; GAME_URL=$(cfg GAME_URL)
:; GAME_MD5=$(cfg GAME_MD5)
:; EXTRACT=$(cfg EXTRACT_DISC);    EXTRACT=${EXTRACT:-../extract_disc}
:;
:; DATA_DIR="$CD_DIR/data"
:; MUSIC_DIR="$CD_DIR/music"
:; mkdir -p "$ASSET_DIR" "$DATA_DIR" "$MUSIC_DIR"
:;
:; count_blobs() { find "$DATA_DIR" -maxdepth 1 -type f \( -name '*.BIN' -o -name '*.bin' \) ! -iname '0.bin' ! -iname 'another.bin' ! -iname 'memlist.bin' 2>/dev/null | wc -l | tr -d ' '; }
:;
:; if [ "$(count_blobs)" -eq 19 ] && [ "${1:-}" != "-f" ]; then
:;     echo "Game data already installed in $DATA_DIR (pass -f to rebuild)."; exit 0
:; fi
:;
:; find_cue() {
:;     for d in "$ASSET_DIR" "$RIP_DIR"; do
:;         [ -d "$d" ] || continue
:;         c=$(find "$d" -type f -iname '*.cue' 2>/dev/null | sort | head -1)
:;         [ -n "$c" ] && { echo "$c"; return 0; }
:;     done
:; }
:; count_audio() { find "$MUSIC_DIR" -maxdepth 1 -type f \( -iname '*.wav' -o -iname '*.mp3' -o -iname '*.flac' \) 2>/dev/null | wc -l | tr -d ' '; }
:; CUE=$(find_cue)
:;
:; if [ -z "$CUE" ]; then
:;     ARC=$(find "$ASSET_DIR" -maxdepth 1 -type f \( -iname '*.7z' -o -iname '*.zip' \) | sort | head -1)
:;     if [ -z "$ARC" ] && [ -n "$GAME_URL" ]; then
:;         case "$GAME_URL" in *.zip) ARC="$ASSET_DIR/game.zip" ;; *) ARC="$ASSET_DIR/game.7z" ;; esac
:;         echo "Downloading $GAME_URL"
:;         got=0
:;         if command -v curl >/dev/null 2>&1; then if curl -fL --retry 3 -o "$ARC.part" "$GAME_URL"; then got=1; fi
:;         elif command -v wget >/dev/null 2>&1; then if wget -q -O "$ARC.part" "$GAME_URL"; then got=1; fi
:;         else echo "ERROR: need curl or wget on PATH" >&2; exit 1; fi
:;         if [ "$got" -ne 1 ] || [ ! -s "$ARC.part" ]; then
:;             rm -f "$ARC.part"
:;             echo "ERROR: GAME_URL could not be fetched: $GAME_URL" >&2
:;             echo "It has moved or gone. Put your own rip in $ASSET_DIR instead." >&2
:;             exit 1
:;         fi
:;         if [ -n "$GAME_MD5" ]; then
:;             sum=""
:;             if command -v md5sum >/dev/null 2>&1; then sum=$(md5sum "$ARC.part" | cut -d' ' -f1); fi
:;             if [ -z "$sum" ] && command -v md5 >/dev/null 2>&1; then sum=$(md5 -q "$ARC.part"); fi
:;             if [ -z "$sum" ]; then echo "No md5sum or md5 on PATH -- skipping the archive check."
:;             elif [ "$sum" != "$GAME_MD5" ]; then
:;                 rm -f "$ARC.part"
:;                 echo "ERROR: the downloaded archive is not the one GAME_MD5 describes." >&2
:;                 echo "  expected $GAME_MD5" >&2
:;                 echo "  got      $sum" >&2
:;                 echo "The file at GAME_URL has changed, or the download was corrupted." >&2
:;                 exit 1
:;             else echo "Archive matches GAME_MD5."
:;             fi
:;         fi
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
:;     if [ -n "$GAME_URL" ]; then echo "What GAME_URL fetched held no .cue." >&2
:;     else echo "CONFIG.ME has no GAME_URL: the released kit ships without one and never" >&2; echo "downloads game data." >&2; fi
:;     echo "Put a copy of the disc in" >&2
:;     echo "  $ASSET_DIR" >&2
:;     echo "as a .cue with its .bin tracks beside it, or as a .7z/.zip holding one," >&2
:;     echo "then run this script again. A Redump-layout rip (one bin per track) is" >&2
:;     echo "what extract_disc expects." >&2
:;     exit 1
:; fi
:;
:; if [ ! -x "$EXTRACT" ] && [ -x "$EXTRACT.exe" ]; then EXTRACT="$EXTRACT.exe"; fi
:; if [ ! -x "$EXTRACT" ]; then
:;     UNAME_S=$(uname -s)
:;     BUNDLED=./bin/win/extract_disc.exe
:;     if [ "$UNAME_S" = Linux ]; then BUNDLED=./bin/lin/extract_disc; fi
:;     if [ "$UNAME_S" = Darwin ] && [ "$(uname -m)" = arm64 ]; then BUNDLED=./bin/mac/arm64/extract_disc; fi
:;     if [ "$UNAME_S" = Darwin ] && [ "$(uname -m)" != arm64 ]; then BUNDLED=./bin/mac/amd64/extract_disc; fi
:;     if [ -x "$BUNDLED" ]; then EXTRACT="$BUNDLED"; fi
:; fi
:; if [ ! -x "$EXTRACT" ]; then
:;     echo "ERROR: extractor not found or not executable: $EXTRACT" >&2
:;     echo "In a checkout, build it first:  sh ../build.sh" >&2
:;     exit 1
:; fi
:;
:; echo "Extracting from $(basename "$CUE")"
:; "$EXTRACT" "$CUE" "$CD_DIR"
:;
:; n=$(count_blobs)
:; [ "$n" -eq 19 ] || { echo "ERROR: expected 19 data blobs, installed $n" >&2; exit 1; }
:; echo "Installed $n data blobs into $DATA_DIR; $(count_audio) audio tracks in $MUSIC_DIR"
:; exit

@ECHO OFF
SETLOCAL ENABLEEXTENSIONS ENABLEDELAYEDEXPANSION
CD /D "%~dp0"

SET "ASSET_DIR="
SET "RIP_DIR="
SET "CD_DIR="
SET "GAME_URL="
SET "GAME_MD5="
SET "EXTRACT_DISC="
FOR /F "usebackq eol=# tokens=1,* delims==" %%A IN ("CONFIG.ME") DO (
    IF "%%A"=="ASSET_DIR" SET "ASSET_DIR=%%B"
    IF "%%A"=="RIP_DIR" SET "RIP_DIR=%%B"
    IF "%%A"=="CD_DIR" SET "CD_DIR=%%B"
    IF "%%A"=="GAME_URL" SET "GAME_URL=%%B"
    IF "%%A"=="GAME_MD5" SET "GAME_MD5=%%B"
    IF "%%A"=="EXTRACT_DISC" SET "EXTRACT_DISC=%%B"
)
IF NOT DEFINED ASSET_DIR SET "ASSET_DIR=./assets"
IF NOT DEFINED RIP_DIR SET "RIP_DIR=../../cd"
IF NOT DEFINED CD_DIR SET "CD_DIR=../../saturn/cd"
IF NOT DEFINED EXTRACT_DISC SET "EXTRACT_DISC=../extract_disc"
SET "RIP_DIR=%RIP_DIR:/=\%"

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

CALL :findcue
IF DEFINED CUE GOTO gotcue

SET "ARC="
FOR %%F IN ("%ASSETS%\*.7z" "%ASSETS%\*.zip") DO IF NOT DEFINED ARC SET "ARC=%%~fF"

SET "ARCNAME=game.7z"
IF /I "%GAME_URL:~-4%"==".zip" SET "ARCNAME=game.zip"
IF NOT DEFINED ARC IF DEFINED GAME_URL (
    ECHO Downloading %GAME_URL%
    SET "ARC=%ASSETS%\%ARCNAME%"
    curl -fL --retry 3 -o "!ARC!.part" "%GAME_URL%"
    IF ERRORLEVEL 1 (
        DEL /Q "!ARC!.part" 2>NUL
        ECHO ERROR: GAME_URL could not be fetched: %GAME_URL%
        ECHO It has moved or gone. Put your own rip in "%ASSET_DIR%" instead.
        EXIT /B 1
    )
    IF DEFINED GAME_MD5 (
        powershell -NoProfile -Command "$want='%GAME_MD5%'; $got=(Get-FileHash -LiteralPath '!ARC!.part' -Algorithm MD5).Hash.ToLower(); if ($got -ne $want) { Write-Host 'ERROR: the downloaded archive is not the one GAME_MD5 describes.'; Write-Host ('  expected ' + $want); Write-Host ('  got      ' + $got); exit 1 }; Write-Host 'Archive matches GAME_MD5.'"
        IF ERRORLEVEL 1 (
            DEL /Q "!ARC!.part" 2>NUL
            ECHO The file at GAME_URL has changed, or the download was corrupted.
            EXIT /B 1
        )
    )
    MOVE /Y "!ARC!.part" "!ARC!" >NUL
)

IF DEFINED ARC (
    ECHO Unpacking "%ARC%"
    WHERE 7z >NUL 2>&1 && ( 7z x -y -o"%ASSETS%" "%ARC%" >NUL ) || ( tar -xf "%ARC%" -C "%ASSETS%" )
    IF ERRORLEVEL 1 ( ECHO ERROR: unpack failed & EXIT /B 1 )
    CALL :findcue
)

IF NOT DEFINED CUE (
    ECHO ERROR: no disc image found.
    ECHO.
    IF DEFINED GAME_URL ( ECHO What GAME_URL fetched held no .cue. ) ELSE ( ECHO CONFIG.ME has no GAME_URL: the released kit ships without one and never downloads game data. )
    ECHO Put a copy of the disc in
    ECHO   "%ASSETS%"
    ECHO as a .cue with its .bin tracks beside it, or as a .7z/.zip holding one,
    ECHO then run this script again. A Redump-layout rip ^(one bin per track^) is
    ECHO what extract_disc expects.
    EXIT /B 1
)

:gotcue
IF NOT EXIST "%EXTRACT_DISC%" SET "EXTRACT_DISC=%EXTRACT_DISC%.exe"
IF NOT EXIST "%EXTRACT_DISC%" IF EXIST "%~dp0bin\win\extract_disc.exe" SET "EXTRACT_DISC=%~dp0bin\win\extract_disc.exe"
IF NOT EXIST "%EXTRACT_DISC%" (
    ECHO ERROR: extractor not found: "%EXTRACT_DISC%"
    ECHO In a checkout, build it first:  bash ..\build.sh
    EXIT /B 1
)

ECHO Extracting from "%CUE%"
"%EXTRACT_DISC%" "%CUE%" "%CDROOT%"
IF ERRORLEVEL 1 ( ECHO ERROR: extraction failed & EXIT /B 1 )

CALL :countblobs
IF NOT "%BLOBS%"=="19" ( ECHO ERROR: expected 19 data blobs, installed %BLOBS% & EXIT /B 1 )
CALL :countaudio
ECHO Installed %BLOBS% data blobs into "%DATA_DIR%"; %AUDIO% audio tracks in "%MUSIC_DIR%"

ENDLOCAL
EXIT /B 0

:countblobs
SET /A BLOBS=0
FOR %%F IN ("%DATA_DIR%\*.bin") DO (
    IF /I NOT "%%~nxF"=="0.bin" IF /I NOT "%%~nxF"=="ANOTHER.BIN" IF /I NOT "%%~nxF"=="memlist.bin" SET /A BLOBS+=1
)
EXIT /B 0

:findcue
SET "CUE="
FOR /R "%ASSETS%" %%F IN (*.cue) DO IF NOT DEFINED CUE SET "CUE=%%~fF"
IF NOT DEFINED CUE IF EXIST "%RIP_DIR%" FOR /R "%RIP_DIR%" %%F IN (*.cue) DO IF NOT DEFINED CUE SET "CUE=%%~fF"
EXIT /B 0

:countaudio
SET /A AUDIO=0
FOR %%F IN ("%MUSIC_DIR%\*.wav" "%MUSIC_DIR%\*.mp3" "%MUSIC_DIR%\*.flac") DO SET /A AUDIO+=1
EXIT /B 0
