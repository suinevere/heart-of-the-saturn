:; set -eu
:; cd "$(dirname "$0")"
:;
:; echo "== Step 1/3: install Part II data and music =="
:; sh ./data.bat "$@"
:;
:; echo "== Step 2/3: fetch Part I's program =="
:; sh ../another/fetch.sh "$@"
:;
:; echo "== Step 3/3: check Part I data =="
:; n=$(find ../../saturn/cd/data -maxdepth 1 -type f \( -iname 'bank??' -o -iname 'memlist.bin' \) 2>/dev/null | wc -l | tr -d ' ')
:; if [ "$n" -eq 14 ]; then echo "Part I: data present."; else echo "Part I: data absent ($n of 14) -- copy bank01 to bank0d and memlist.bin into saturn/cd/data, or OUT OF THIS WORLD will not be playable on this disc."; fi
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

ECHO == Step 3/3: check Part I data ==
SET /A P1=0
FOR %%F IN ("%~dp0..\..\saturn\cd\data\bank??") DO SET /A P1+=1
IF EXIST "%~dp0..\..\saturn\cd\data\memlist.bin" SET /A P1+=1
IF "%P1%"=="14" ( ECHO Part I: data present. ) ELSE ( ECHO Part I: data absent, %P1% of 14 -- copy bank01 to bank0d and memlist.bin into saturn\cd\data, or OUT OF THIS WORLD will not be playable on this disc. )

ECHO.
ECHO Ready to build: cd saturn ^&^& compile.bat
ENDLOCAL
EXIT /B 0
