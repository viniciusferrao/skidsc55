/* Who this program is: its name, its version, and who to blame for it.
 *
 * Separate from sc15.h so that bumping a release does not mean editing the
 * header the voice bank's field offsets live in.
 *
 * This versions the tooling, not the driver. SC15.DRV is a flat image the game
 * far-calls at offset 0, with nowhere to put a version that would not cost
 * bytes the jump table needs; its configuration block at 45h identifies it.
 */
#ifndef SC15_VERSION_H
#define SC15_VERSION_H

#define SC_VERSION "1.0"

/* Bumped whenever anything the DOS executables are built from changes, which
 * the release number is not: 1.0 covers a long stretch of this file's history
 * and says nothing about which build of it is checked into bin/.
 *
 * That gap is not theoretical. release-check once matched only the version and
 * the subcommand names, and the executables from the commit before a whole
 * round of input validation satisfied every clause of it, because they carried
 * the same version and the same four subcommands. This string is what makes a
 * stale binary visible: change anything either executable is built from, which
 * is src/ and test/selfchk.c, change this too, and release-check fails until
 * MSCBUILD.BAT has been run.
 *
 * It costs one string in two utilities and nothing at all in SC15.DRV. */
#define SC_BUILD "b11"

#define SC_NAME "SC15"
#define SC_DESCRIPTION "tooling for the Sound Canvas driver for Stunts 1.1"
#define SC_YEAR "2026"
#define SC_DEV "Vinicius Ferrao <vinicius@ferrao.net.br>"

/* The banner, printed the way a DOS tool of the period announced itself.
 *
 * ASCII, so the author's name loses its accents. Code page 437 has no a-tilde
 * to show at any encoding, and this file's UTF-8 would arrive there as
 * box-drawing characters. LICENSE carries the accented spelling. */
#define SC_TAGLINE SC_NAME " " SC_VERSION "." SC_BUILD ": " SC_DESCRIPTION "\n"
#define SC_COPYRIGHT "Copyright (c) " SC_YEAR " " SC_DEV "\n"

#define SC_BANNER SC_TAGLINE SC_COPYRIGHT

#endif /* SC15_VERSION_H */
