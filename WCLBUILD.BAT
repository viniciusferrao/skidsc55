@echo off
rem Build the sc15 tooling with Open Watcom 1.9.
rem
rem MSCBUILD.BAT is the period build and the one bin/ ships; this one exists
rem because Open Watcom is redistributable and still maintained, so anyone can
rem reproduce a DOS binary without hunting down a 1988 compiler, and because
rem each extra compiler is another opinion on the sources.
rem
rem   WCLBUILD          SC15.EXE and SCCHECK.EXE, 16-bit, 8086
rem   WCLBUILD 386      SC1532.EXE and SCCHK32.EXE, 32-bit, DOS/32A bound in
rem   WCLBUILD win32    SC15W.EXE and SCCHKW.EXE, Windows console
rem
rem The 16-bit pair runs on anything the game runs on. The 32-bit pair wants a
rem 386 and carries the extender bound in, and is also a second opinion: int is
rem 32 bits there, so code that assumed the 16-bit width stops compiling rather
rem than going quietly wrong. The win32 pair is the same program for a modern
rem console, since the tooling is plain stdio with no BIOS anywhere.
rem
rem Set WATCOM to your installation before running, or edit the default here.

if not "%WATCOM%"=="" goto havedir
set WATCOM=C:\WATCOM
:havedir

rem The DOS-hosted tools are in BINW and the Windows-hosted ones in BINNT. Only
rem cmd.exe sets OS, so this picks the set that can actually run: a 16-bit BINW
rem tool does not start on 64-bit Windows at all.
set WCLBIN=%WATCOM%\BINW
if "%OS%"=="Windows_NT" set WCLBIN=%WATCOM%\BINNT

rem Look before starting. The headers are checked separately from the tools:
rem WATCOM pointing at a tools-only tree gets through every compile line before
rem the first #include fails.
if exist %WCLBIN%\WCL.EXE goto havewcl
echo WCLBUILD: no WCL.EXE under %WCLBIN%
echo Set WATCOM to your Open Watcom 1.9 installation.
goto badenv
:havewcl
if exist %WATCOM%\H\STDIO.H goto haveinc
echo WCLBUILD: no STDIO.H under %WATCOM%\H
echo This installation has tools but no headers.
goto badenv
:haveinc

rem BINW is on the path for both hosts, because the 32-bit link binds the
rem extender and wlink looks for DOS32A.EXE along PATH. It ships in BINW only.
set PATH=%WCLBIN%;%WATCOM%\BINW;%PATH%
set INCLUDE=%WATCOM%\H

rem wcl expands a wildcard itself but hands wcc the bare file name, so
rem "wcl src\*.c" compiles the first file and then cannot find the second.
rem Building from inside src is what works; -fe puts the executable back up
rem top.
cd src

if "%1"=="386" goto build386
if "%1"=="32" goto build386
if "%1"=="WIN32" goto win32
if "%1"=="win32" goto win32
if "%1"=="NT" goto win32
if "%1"=="nt" goto win32

rem -ml is the large model, matching MSCBUILD.BAT, and -0 keeps the output on
rem the 8086 the game ran on.
rem
rem -k8192 is the stack, the same 8 KB MSCBUILD.BAT asks the Microsoft linker
rem for. The default is tight once argv and a few automatic buffers are on it,
rem and running out is not diagnosed: the program stops mid printf with a half
rem flushed buffer, which reads like a failed check rather than a crash.
rem
rem Last time's binaries go before this build starts, and only the two this
rem target produces: a 16-bit build has no business deleting the 386 pair.
if exist ..\SC15.EXE del ..\SC15.EXE
if exist ..\SCCHECK.EXE del ..\SCCHECK.EXE

wcl -bt=dos -ml -0 -w3 -k8192 *.c -fe=..\SC15.EXE
if errorlevel 1 goto fail
del *.obj

rem The self-check, from the same sources. main.c is left out because
rem selfchk.c brings its own main(). One physical line: the caret is cmd.exe's
rem continuation and COMMAND.COM has no such thing, and this fits inside the
rem 127 characters DOS allows.
wcl -bt=dos -ml -0 -w3 -k8192 -i=. ..\test\selfchk.c util.c mkvce.c cfg.c kms.c block.c -fe=..\SCCHECK.EXE
if errorlevel 1 goto fail
del *.obj
cd ..

echo.
echo Built SC15.EXE and SCCHECK.EXE. Check the build with:
echo     SCCHECK
if "%OS%"=="Windows_NT" exit /b 0
goto end

rem The 32-bit build. -l=dos32a binds the extender into the executable so
rem there is one file to ship and nothing to install; wcl386 targets a 386 by
rem default, which is what the extender needs anyway. No -k here: the extender
rem gives the program a flat stack of its own.
:build386
if exist ..\SC1532.EXE del ..\SC1532.EXE
if exist ..\SCCHK32.EXE del ..\SCCHK32.EXE
wcl386 -bt=dos -l=dos32a -w3 *.c -fe=..\SC1532.EXE
if errorlevel 1 goto fail
del *.obj
if exist __wcl__.lnk del __wcl__.lnk

wcl386 -bt=dos -l=dos32a -w3 -i=. ..\test\selfchk.c util.c mkvce.c cfg.c kms.c block.c -fe=..\SCCHK32.EXE
if errorlevel 1 goto fail
del *.obj
if exist __wcl__.lnk del __wcl__.lnk
cd ..

echo.
echo Built SC1532.EXE and SCCHK32.EXE. Check the build with:
echo     SCCHK32
if "%OS%"=="Windows_NT" exit /b 0
goto end

rem An exit code where there is one to be had, and only under cmd.exe:
rem COMMAND.COM has no "exit /b", and there it would close the shell, which
rem under DOSBox closes the emulator on a successful build.
rem
rem Every path jumps to :end, and :end is the last line in the file. It has to
rem be: a label does not stop a batch file, so COMMAND.COM reaching :end simply
rem carries on into whatever is written below it.
:fail
del *.obj
cd ..
echo BUILD FAILED
if "%OS%"=="Windows_NT" exit /b 1
goto end

:badenv
if "%OS%"=="Windows_NT" exit /b 1
goto end

rem The Windows console build. -bt=nt and nothing else, because the tooling is
rem plain stdio: no BIOS, no screen drawing, nothing to port. The NT headers
rem are a directory below the common ones and wcl386 -bt=nt does not add it to
rem INCLUDE by itself.
:win32
set INCLUDE=%WATCOM%\H;%WATCOM%\H\NT
if exist ..\SC15W.EXE del ..\SC15W.EXE
if exist ..\SCCHKW.EXE del ..\SCCHKW.EXE
wcl386 -bt=nt -w3 *.c -fe=..\SC15W.EXE
if errorlevel 1 goto fail
del *.obj
if exist __wcl__.lnk del __wcl__.lnk

wcl386 -bt=nt -w3 -i=. ..\test\selfchk.c util.c mkvce.c cfg.c kms.c block.c -fe=..\SCCHKW.EXE
if errorlevel 1 goto fail
del *.obj
if exist __wcl__.lnk del __wcl__.lnk
cd ..

echo.
echo Built SC15W.EXE and SCCHKW.EXE. Check the build with:
echo     SCCHKW
if "%OS%"=="Windows_NT" exit /b 0

rem Last line in the file, and nothing may be written below it.
:end
