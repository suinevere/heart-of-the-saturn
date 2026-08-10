@ECHO Off
REM Heart of the Alien: builds the HOST executable (alien.exe), not the Saturn disc.
REM compile.bat is the Saturn half of this pair; the two cannot share one script because
REM they need different toolchains and, more importantly, they clobber each other.
REM
REM The clobber is why "build" always cleans first and is not an optimisation to remove:
REM saturn/src/Makefile and saturn/makefile both write objects into saturn/src with the
REM same names, one lot x86 and one lot SH-2, so a build that reuses the other's objects
REM dies at the link on "relocations in generic ELF (EM: 42)". Run compile.bat clean
REM before the next Saturn build for the same reason, in the other direction.
REM
REM Unlike compile.bat this is a plain batch file with no POSIX first line. That line is a
REM trap -- a shell runs it and exits, skipping the PATH setup below -- and there is nothing
REM to gain from it here, since a shell that can run this already has the toolchain.
REM
REM Toolchain: MSYS2. gcc and sdl2-config come from mingw64, make and sh from usr\bin.
REM Override the root by setting MSYS2_ROOT before calling if yours is elsewhere.
REM
REM Usage: compile_host.bat [build|clean|test]   (default: build)
REM Output: saturn\src\alien.exe, copied to the repository root, which is where it must be
REM         run from -- the host backend opens cd\ relative to the working directory and
REM         panics before reaching a script if started from saturn\.
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
