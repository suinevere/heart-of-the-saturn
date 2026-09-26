@ECHO Off
SETLOCAL
IF "%~1"=="" (SET "TGT=build") ELSE (SET "TGT=%~1")
IF "%MSYS2_ROOT%"=="" SET "MSYS2_ROOT=C:\msys64"

SET "MSYSBIN=%MSYS2_ROOT%\usr\bin"
SET "MINGWBIN=%MSYS2_ROOT%\mingw64\bin"

IF NOT EXIST "%MSYSBIN%\make.exe" GOTO notoolchain
IF NOT EXIST "%MINGWBIN%\gcc.exe" GOTO notoolchain

SET "PATH=%MINGWBIN%;%MSYSBIN%;%PATH%"
SET "SRCDIR=%~dp0src"
SET "ROOT=%~dp0.."

IF /I "%TGT%"=="clean" GOTO doclean
IF /I "%TGT%"=="test"  GOTO dotest

make -C "%SRCDIR%" clean
make -C "%SRCDIR%"
IF ERRORLEVEL 1 GOTO failed
COPY /Y "%SRCDIR%\alien.exe" "%ROOT%\alien.exe" >NUL
ECHO.
ECHO Built saturn\src\alien.exe and copied it to the repository root.
ECHO Run it from the repository root:  alien.exe
ECHO Run compile.bat clean before the next Saturn build.
GOTO done

:dotest
"%MSYSBIN%\bash.exe" -c "cd '%~dp0tests' && ./run_tests.sh"
GOTO done

:doclean
make -C "%SRCDIR%" clean
ECHO Host objects removed.
GOTO done

:notoolchain
ECHO.
ECHO Could not find the MSYS2 toolchain under "%MSYS2_ROOT%".
ECHO Expected "%MSYSBIN%\make.exe" and "%MINGWBIN%\gcc.exe".
ECHO Set MSYS2_ROOT to your MSYS2 install and try again.
GOTO done

:failed
ECHO.
ECHO Build FAILED. Nothing was copied to the repository root.

:done
ENDLOCAL
