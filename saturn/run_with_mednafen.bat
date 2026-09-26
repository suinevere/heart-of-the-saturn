:; "../SaturnRingLib/tools/scripts/run.sh" mednafen; exit;
@ECHO Off
SETLOCAL
SET "CUE=%~dp0BuildDrop\Heart of the Alien (USA).cue"
IF NOT EXIST "%CUE%" (ECHO Build first: compile.bat debug & GOTO :eof)
SET "MEDNAFEN=%~dp0..\SaturnRingLib\emulators\mednafen\mednafen.exe"
IF NOT EXIST "%MEDNAFEN%" SET "MEDNAFEN=mednafen.exe"
START "" "%MEDNAFEN%" "%CUE%"
ENDLOCAL
