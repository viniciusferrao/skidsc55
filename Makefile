# sc15 - Roland Sound Canvas driver for Stunts 1.1
#
# Two halves that need different tools.
#
# The tooling is strict C89 with no dependencies and builds anywhere, which is
# what "make" does. For a 16-bit DOS build of the tooling, run MSCBUILD.BAT
# under Microsoft C 5.10.
#
# The driver itself is 16-bit assembly and needs TASM plus a linker that can
# emit a flat binary, so "make driver" only works where those exist; MinGW
# make on Windows works the same way.
CC      ?= cc
CFLAGS  ?= -std=c89 -pedantic -Wall -Wextra -O2
SRC      = src/util.c src/mkvce.c src/cfg.c src/kms.c src/block.c src/main.c
OBJ      = $(SRC:.c=.o)

# Where TASM and WLINK live. The default assumes a restunts checkout beside
# this one:
#
#   make driver TOOLS=/path/to/tasm-and-wlink
#
# Nothing here writes into your game directory. Building the banks reads the
# MT-32 originals from wherever the source line at the top of each file in
# voices/ points, and installing is a copy you make yourself, or skidset's
# job; see README.md.
TOOLS      ?= ../restunts/tools/bin

# The driver carries a skidset driver block, so skidset can offer a Sound
# Canvas row in its setup menu when this file is in the game directory. See
# docs/driver.md section 7.1. To build a driver without it:
#
#   make driver NOSKIDSET=1
#
# That build is byte identical to the driver as it was before the block. The
# assembler switch is spelt out in the rule rather than passed in, because a
# value beginning with a slash is rewritten into a path by an MSYS shell before
# make ever sees it.
NOSKIDSET  ?=

all: sc15 banks

sc15: $(OBJ)
	$(CC) $(CFLAGS) -o $@ $(OBJ)

$(OBJ): src/sc15.h

# main.c is the only source that reads version.h, and it is where the banner
# comes from. Without this an incremental build after a version change keeps the
# old object and ships a binary whose banner disagrees with its source.
src/main.o: src/version.h


banks: SCSKIDMS.VCE SCENG1.VCE

# The voice banks are generated rather than shipped: each instrument record is
# copied out of your own MTSKIDMS.VCE and MTENG1.VCE so the engine parameters
# the composer wrote survive untouched. The path is the source line at the top
# of each specification.
SCSKIDMS.VCE: sc15 voices/scskidms.txt
	./sc15 mkvce voices/scskidms.txt $@

SCENG1.VCE: sc15 voices/sceng1.txt
	./sc15 mkvce voices/sceng1.txt $@

# The driver has to be a flat image whose first byte is offset 0, because the
# game loads it with mmgr_alloc_pages() and far-calls a jump table at the very
# start. "format dos com" emits exactly that as long as the source says org 0,
# with no .COM style 100h bias. wlink warns "no starting address found", which
# is expected: giving the module an entry point makes it emit the image from
# that point onward and lose the table.
driver: SC15.DRV

SC15.DRV: sc15.asm skidset.inc
	$(TOOLS)/tasm32 /m9 /ml /zn $(NOSKIDSET:1=/dNO_SKIDSET) sc15.asm, sc15.obj
	$(TOOLS)/wlink format dos com option quiet name $@ file sc15.obj

# Byte-compare the generated banks against a known good pair, so a change in
# the tooling cannot silently alter what the game loads.
#   make check REF=/path/to/reference
check: banks
	cmp SCSKIDMS.VCE "$(REF)/SCSKIDMS.VCE"
	cmp SCENG1.VCE "$(REF)/SCENG1.VCE"

# Development only. Runs the built driver in a 16-bit interpreter and checks
# the MIDI it emits. Not required to build or play the game; see test/ and
# docs/testing.md.
DEVSRC = test/emu86.c test/drvtest.c

drvtest: $(DEVSRC) test/emu86.h src/sc15.h
	$(CC) $(CFLAGS) -o $@ $(DEVSRC)

test: drvtest driver banks
	./drvtest SC15.DRV SCSKIDMS.VCE

# What ships in bin/ is what a user installs, and it is not what `make test`
# builds. Twice now it has drifted from the source beside it: once shipping
# without the kms subcommand, once carrying a banner the source had already
# corrected. This is the target that says so before a release rather than after.
#
# The DOS executables cannot be rebuilt here, so they are checked for the
# strings they should carry rather than regenerated; MSCBUILD.BAT is what makes
# them. See docs/testing.md.
release-check: driver drvtest banks
	@echo "== bin/SC15.DRV is the driver this source builds"
	cmp SC15.DRV bin/SC15.DRV
	@echo "== the shipped voice banks are what this source builds"
	cmp SCSKIDMS.VCE bin/SCSKIDMS.VCE
	cmp SCENG1.VCE bin/SCENG1.VCE
	@echo "== the harness passes against the image that ships"
	./drvtest bin/SC15.DRV SCSKIDMS.VCE
	@echo "== both DOS binaries carry the build in src/version.h"
# Anchored to the #define, because SC_TAGLINE names both macros again on one
# line and an unanchored match picks that up too: SC_VERSION "." yields ".",
# SC_BUILD ": " yields ": ". Two lines each, and grep -F reads a newline in a
# pattern as an alternative, so the test passed on "SC15 1.0" alone and took
# any build at all. It accepted a b7 binary against a version.h saying b8,
# which is the one thing this clause exists to refuse.
	@v=`sed -n 's/^#define SC_VERSION "\([^"]*\)".*/\1/p' src/version.h`; \
	 b=`sed -n 's/^#define SC_BUILD "\([^"]*\)".*/\1/p' src/version.h`; \
	 test -n "$$v" -a -n "$$b" || \
	   { echo "no SC_VERSION or SC_BUILD in src/version.h"; exit 1; }; \
	 case "$$v$$b" in \
	   *' '*|*'	'*) echo "SC_VERSION or SC_BUILD matched more than the \
define"; exit 1;; \
	 esac; \
	 for f in bin/SC15.EXE bin/SCCHECK.EXE; do \
	   grep -a -F -q "SC15 $$v.$$b:" $$f || \
	     { echo "$$f is not built from this source: no SC15 $$v.$$b"; \
	       exit 1; }; \
	 done
	@echo "== SC15.EXE has every subcommand main.c dispatches"
	@for c in mkvce cfg kms block; do \
	   grep -a -F -q "$$c" bin/SC15.EXE || \
	     { echo "bin/SC15.EXE has no $$c"; exit 1; }; \
	 done
	@echo "== nothing in bin/ still says Copyleft"
	@if grep -l -a -F Copyleft bin/SC15.EXE bin/SCCHECK.EXE bin/SC15.DRV \
	     2>/dev/null | grep -q .; then \
	   echo "a binary in bin/ still says Copyleft"; exit 1; \
	 fi
	@echo "== sizes"
	@ls -l bin/SC15.DRV bin/SC15.EXE bin/SCCHECK.EXE

# Exercise the tooling on a bank it generates, so it needs no game data. This
# is what CI runs; see test/selfchk.c.
SELFOBJ = src/util.o src/mkvce.o src/cfg.o src/kms.o src/block.o

selfcheck: test/selfchk.c $(SELFOBJ) src/sc15.h
	$(CC) $(CFLAGS) -I src -o selfcheck test/selfchk.c $(SELFOBJ)
	./selfcheck

# Does sc15 block agree with the reader it is promising about? Two separate
# implementations in two repositories, so it is measured rather than intended.
# Needs a skidset checkout, which is not vendored here and which CI has no way
# to supply; see test/parity/README.md.
SKIDSET_DIR ?= ../skidset

parity: sc15
	@test -f "$(SKIDSET_DIR)/src/drvblk.c" || \
	  { echo "no skidset at $(SKIDSET_DIR); set SKIDSET_DIR"; exit 1; }
	@mkdir -p build
	$(CC) $(CFLAGS) -I "$(SKIDSET_DIR)/src" -o build/refjudge \
	  test/parity/refjudge.c "$(SKIDSET_DIR)/src/drvblk.c"
	@sh test/parity/corpus.sh build/parity-cases
	@cd build && sh ../test/parity/diff.sh ../sc15 parity-cases

# Apply the house style. CLANG_FORMAT lets you point at a pinned build.
CLANG_FORMAT ?= clang-format
STYLED = src/*.c src/*.h test/*.c test/*.h

format:
	$(CLANG_FORMAT) -i --style=file $(STYLED)

format-check:
	$(CLANG_FORMAT) --dry-run --Werror --style=file $(STYLED)

lint:
	cppcheck --std=c89 --enable=warning,performance,portability \
	         --inline-suppr --error-exitcode=1 \
	         --suppress=missingIncludeSystem src/ test/
	@for f in $(SRC) $(DEVSRC); do \
	  $(CC) -std=c90 -pedantic-errors -Wall -Wextra -Wshadow -Wcast-qual \
	        -Wstrict-prototypes -Wmissing-prototypes -Wwrite-strings \
	        -fanalyzer -O2 -I src -c $$f -o /dev/null || exit 1; \
	done

clean:
	rm -f $(OBJ) sc15 sc15.exe sc15.obj drvtest drvtest.exe selfcheck \
	      selfcheck.exe SC15.DRV SCSKIDMS.VCE SCENG1.VCE
	rm -rf build

.PHONY: all banks driver check selfcheck test release-check parity \
	format format-check lint clean
