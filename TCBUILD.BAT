@echo off
rem Build the sc15 tooling as a 16-bit DOS program with Turbo C 2.01.
rem
rem The third opinion on these sources, reading them as a 1989 compiler rather
rem than a 1988 or a 2002 one. It ships nothing: Borland's runtime is not ours
rem to redistribute, so bin/ carries the Microsoft C build. This build is here
rem to be compiled, not to be shipped.
rem
rem The sources check out CRLF for this compiler's sake; see .gitattributes.
rem Turbo C miscompiles a source file with Unix line endings, emitting a nearly
rem empty object and reporting nothing, and the failure surfaces much later as
rem an undefined _main at link time. skidset measured that; the rule and this
rem warning are mirrored here.
rem
rem Set TCDIR to your installation before running, or edit the default here.

if not "%TCDIR%"=="" goto havedir
set TCDIR=C:\TC
:havedir

rem Look before starting, the same as the other builds do.
if exist %TCDIR%\BIN\TCC.EXE goto havetcc
echo TCBUILD: no TCC.EXE under %TCDIR%\BIN
echo Set TCDIR to your Turbo C 2.01 installation.
goto end
:havetcc
if exist %TCDIR%\LIB\CL.LIB goto havelib
echo TCBUILD: no CL.LIB under %TCDIR%\LIB
echo The large model libraries are missing from this installation.
goto end
:havelib

set PATH=%TCDIR%\BIN;%PATH%

rem Any earlier build goes before this one starts. A batch file here cannot
rem set an errorlevel, so what a caller has to go on is the message and the
rem files; leaving a working binary from last time next to a failed rebuild
rem makes the second of those lie.
if exist SC15.EXE del SC15.EXE
if exist SCCHECK.EXE del SCCHECK.EXE

cd src

rem -ml is the large model, matching the other two DOS builds, and -c because
rem without it TCC links as well and builds its own overlong link line.
tcc -c -ml -I%TCDIR%\INCLUDE util.c mkvce.c cfg.c kms.c block.c main.c
if errorlevel 1 goto fail

rem TLINK is driven from a response file so no command line can outgrow the
rem 127 characters DOS allows. The format is not obvious: a newline REPLACES
rem the comma between fields, and a trailing + continues the object field onto
rem the next line. The four fields are objects, executable, map, libraries;
rem nul discards the map and /c makes the link case sensitive to match the
rem compiler. CL.LIB alone, no MATHL: the tooling is integer arithmetic
rem throughout, deliberately, so the floating point library has nothing to add.
echo /c %TCDIR%\LIB\C0L.OBJ +> tclink.rsp
echo util.obj mkvce.obj cfg.obj +>> tclink.rsp
echo kms.obj block.obj main.obj>> tclink.rsp
echo ..\SC15.EXE>> tclink.rsp
echo nul>> tclink.rsp
echo %TCDIR%\LIB\CL.LIB>> tclink.rsp
tlink @tclink.rsp
if errorlevel 1 goto fail
del tclink.rsp
del *.obj

rem The self-check, from the same sources. main.c is left out because
rem selfchk.c brings its own main().
tcc -c -ml -I%TCDIR%\INCLUDE -I. ..\test\selfchk.c
if errorlevel 1 goto fail
tcc -c -ml -I%TCDIR%\INCLUDE util.c mkvce.c cfg.c kms.c block.c
if errorlevel 1 goto fail
echo /c %TCDIR%\LIB\C0L.OBJ +> tclink.rsp
echo selfchk.obj util.obj mkvce.obj +>> tclink.rsp
echo cfg.obj kms.obj block.obj>> tclink.rsp
echo ..\SCCHECK.EXE>> tclink.rsp
echo nul>> tclink.rsp
echo %TCDIR%\LIB\CL.LIB>> tclink.rsp
tlink @tclink.rsp
if errorlevel 1 goto fail
del tclink.rsp
del *.obj
cd ..

echo.
echo Built SC15.EXE and SCCHECK.EXE. Check the build with:
echo     SCCHECK
goto end

:fail
del *.obj
cd ..
echo BUILD FAILED
rem No exit code: this runs under COMMAND.COM, which has no "exit /b", and a
rem batch file there cannot set its own errorlevel. What a caller has is this
rem message and the absence of the executables.

:end
