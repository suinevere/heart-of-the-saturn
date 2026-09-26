:; set -eu
:; cd "$(dirname "$0")"
:;
:; echo "== Step 1/3: install Part II data and music =="
:; sh ./data.bat "$@"
:;
:; echo "== Step 2/3: fetch Part I's program =="
:; sh ../another/fetch.sh "$@"
:;
:; echo "== Step 3/3: install Part I data =="
:; if [ -f ./part1/data.bat ]; then if sh ./part1/data.bat "$@"; then echo "Part I: data installed."; else echo "Part I: data unavailable -- OUT OF THIS WORLD will not be playable on this disc."; fi; else echo "Part I: no data step present -- skipping."; fi
:;
:; echo
:; echo "Ready to build: cd saturn && compile.bat"
:; exit

@ECHO OFF
SETLOCAL ENABLEEXTENSIONS
CD /D "%~dp0"

SET "SH_EXE="
FOR %%S IN (sh.exe) DO IF NOT DEFINED SH_EXE IF NOT "%%~$PATH:S"=="" SET "SH_EXE=%%~$PATH:S"
IF NOT DEFINED SH_EXE IF EXIST "%~dp0..\..\SaturnRingLib\Compiler\msys2\usr\bin\sh.exe" SET "SH_EXE=%~dp0..\..\SaturnRingLib\Compiler\msys2\usr\bin\sh.exe"

ECHO == Step 1/3: install Part II data and music ==
CALL "%~dp0data.bat" %*
IF ERRORLEVEL 1 ( ECHO ERROR: Part II data install failed & EXIT /B 1 )

ECHO == Step 2/3: fetch Part I's program ==
IF NOT DEFINED SH_EXE (
    ECHO Part I: no sh.exe on PATH and none in SaturnRingLib\Compiler\msys2 -- skipping the program fetch.
) ELSE (
    "%SH_EXE%" "%~dp0..\another\fetch.sh" %*
    IF ERRORLEVEL 1 ECHO Part I: program fetch failed -- OUT OF THIS WORLD will not appear on this disc.
)

ECHO == Step 3/3: install Part I data ==
IF EXIST "%~dp0part1\data.bat" (
    CALL "%~dp0part1\data.bat" %*
    IF ERRORLEVEL 1 ( ECHO Part I: data unavailable -- OUT OF THIS WORLD will not be playable on this disc. ) ELSE ( ECHO Part I: data installed. )
) ELSE ( ECHO Part I: no data step present -- skipping. )

ECHO.
ECHO Ready to build: cd saturn ^&^& compile.bat
ENDLOCAL
EXIT /B 0
