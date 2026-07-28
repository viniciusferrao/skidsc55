@echo off
rem Build the sc15 tooling as a 16-bit DOS program with Microsoft C 5.10.
rem
rem The driver this tool builds banks for is a 1990 DOS driver, so the tooling
rem is written to compile on a compiler of the same era and run on the same
rem machine. Nothing here needs a modern host.
rem
rem Set MSCDIR to your installation before running, or edit the default below.
rem TMP must exist and be writable: CL and LINK both spill to it.

if not "%MSCDIR%"=="" goto havedir
set MSCDIR=M:
:havedir
set PATH=%MSCDIR%\BIN;%PATH%
set INCLUDE=%MSCDIR%\INCLUDE
set LIB=%MSCDIR%\LIB
if "%TMP%"=="" set TMP=C:\

cd src

rem /AL selects the large model. Nothing here needs more than 64 KB, so a
rem smaller model would do on paper, but this installation's headers declare
rem the whole library with explicit far pointers: STDIO.H has
rem "FILE far * far _CDECL fopen(const char far *, const char far *)". Compile
rem small and every library call that returns a pointer warns C4060, converting
rem a long address to a short one, which is the compiler telling you the return
rem value is being truncated.
cl /c /AL /W3 util.c
if errorlevel 1 goto fail
cl /c /AL /W3 mkvce.c
if errorlevel 1 goto fail
cl /c /AL /W3 cfg.c
if errorlevel 1 goto fail
cl /c /AL /W3 kms.c
if errorlevel 1 goto fail
cl /c /AL /W3 block.c
if errorlevel 1 goto fail
cl /c /AL /W3 main.c
if errorlevel 1 goto fail

rem Three parts of this line are not guessable.
rem
rem   /NOD    kills the default library request. CL writes a request for
rem           LLIBCE.LIB into each object; an installation that ships LLIBCR.LIB
rem           instead cannot link until the explicit list is made to win.
rem
rem   LIBH    supplies __aNlmul, __aNldiv, __aNlrem and __aNulmul, the 32-bit
rem           helpers that long arithmetic compiles down to. Leave it out and
rem           the link stops on unresolved externals that name no source line.
rem
rem   /STACK  the default 2 KB is tight once argv and a few automatic buffers
rem           are on it, and DOS reports the overflow as a bare abnormal exit.
link /NOI /NOD /STACK:8192 util+mkvce+cfg+kms+block+main,..\SC15.EXE,,LLIBCR+LIBH+EM;
if errorlevel 1 goto fail

rem The self-check. It synthesises its own voice bank, so it needs no game
rem data. Compiled here rather than in test\ so both links can use the same
rem objects; main.obj is left out because this file brings its own main().
cl /c /AL /W3 /I. ..\test\selfchk.c
if errorlevel 1 goto fail
link /NOI /NOD /STACK:8192 selfchk+util+mkvce+cfg+kms+block,..\SCCHECK.EXE,,LLIBCR+LIBH+EM;
if errorlevel 1 goto fail

del *.obj
cd ..

echo.
echo Built SC15.EXE and SCCHECK.EXE.  Check the build with:
echo     SCCHECK
echo.
echo Generate the voice banks with:
echo     SC15 MKVCE VOICES\SCSKIDMS.TXT SCSKIDMS.VCE
echo     SC15 MKVCE VOICES\SCENG1.TXT SCENG1.VCE
echo.
echo The source line in each specification has to name a path DOS can reach,
echo so mount your Stunts directory and point it there, for example
echo     source G:\MTSKIDMS.VCE
goto done

:fail
cd ..
echo BUILD FAILED

:done
