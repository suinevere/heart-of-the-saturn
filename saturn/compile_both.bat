:; set -e; T="${1:-debug}"; HERE="$(cd "$(dirname "$0")" && pwd)"; AW="${ANOTHER_SATURN_DIR:-$HERE/../Another-Saturn}"; [ -f "$AW/saturn/compile.bat" ] || { echo "ERROR: no Another-Saturn at $AW -- run: git submodule update --init Another-Saturn" >&2; exit 1; }; if [ ! -d "$AW/SaturnRingLib/Compiler" ]; then [ -z "${ANOTHER_SATURN_DIR:-}" ] || { echo "ERROR: $AW has no SaturnRingLib toolchain of its own; install it, or unset ANOTHER_SATURN_DIR to build the submodule" >&2; exit 1; }; export SRL_INSTALL_ROOT="../../SaturnRingLib"; fi; (cd "$AW/saturn" && sh compile.bat "$T"); unset SRL_INSTALL_ROOT; if [ "$T" != "clean" ]; then [ -f "$AW/saturn/cd/data/0.bin" ] || { echo "ERROR: Another-Saturn built no saturn/cd/data/0.bin" >&2; exit 1; }; cp -f "$AW/saturn/cd/data/0.bin" "$HERE/cd/data/ANOTHER.BIN"; echo "Part I: staged $AW/saturn/cd/data/0.bin as ANOTHER.BIN"; fi; cd "$HERE" && HOTA_PART1=0 sh compile.bat "$T"; exit;
@ECHO Off
SETLOCAL
IF "%~1"=="" (SET "TGT=debug") ELSE (SET "TGT=%~1")
IF DEFINED ANOTHER_SATURN_DIR (SET "AWDIR=%ANOTHER_SATURN_DIR%") ELSE (SET "AWDIR=%~dp0..\Another-Saturn")
IF NOT EXIST "%AWDIR%\saturn\compile.bat" (
    ECHO ERROR: no Another-Saturn at %AWDIR% -- run: git submodule update --init Another-Saturn
    EXIT /B 1
)
IF EXIST "%AWDIR%\SaturnRingLib\Compiler\sh2eb-elf\bin" GOTO awbuild
IF DEFINED ANOTHER_SATURN_DIR (
    ECHO ERROR: %AWDIR% has no SaturnRingLib toolchain of its own; install it, or unset ANOTHER_SATURN_DIR to build the submodule
    EXIT /B 1
)
SET "SRL_COMPILER_DIR=%~dp0..\SaturnRingLib\Compiler"
SET "SRL_INSTALL_ROOT=../../SaturnRingLib"
:awbuild
PUSHD "%AWDIR%\saturn"
CALL "%AWDIR%\saturn\compile.bat" %TGT%
IF ERRORLEVEL 1 (
    POPD
    ECHO ERROR: Another-Saturn's %TGT% build failed
    EXIT /B 1
)
POPD
SET "SRL_INSTALL_ROOT="
SET "SRL_COMPILER_DIR="
IF /I "%TGT%"=="clean" GOTO build
IF NOT EXIST "%AWDIR%\saturn\cd\data\0.bin" (
    ECHO ERROR: Another-Saturn built no saturn\cd\data\0.bin
    EXIT /B 1
)
COPY /Y "%AWDIR%\saturn\cd\data\0.bin" "%~dp0cd\data\ANOTHER.BIN" >NUL
ECHO Part I: staged %AWDIR%\saturn\cd\data\0.bin as ANOTHER.BIN
:build
SET "HOTA_PART1=0"
PUSHD "%~dp0"
CALL "%~dp0compile.bat" %TGT%
SET "RC=%ERRORLEVEL%"
POPD
EXIT /B %RC%
