# skidsc55

A Roland Sound Canvas driver for the Stunts DOS game.

The original Stunts shipped with a great driver list for the time:
  * PC Speaker
  * Tandy Sound
  * AdLib / Sound Blaster (Yamaha OPL2)
  * The glorious Roland MT-32

This adds one for the Sound Canvas. It uses the General MIDI set for the music,
plus the GS variation sets for the in game effects, so it should sound right on
any Sound Canvas compatible, starting with the SC-55.

Click to hear and see the YouTube video:
[![Stunts with the Sound Canvas driver][thumb]][video]

[thumb]: https://img.youtube.com/vi/bZNGdzGi2BI/maxresdefault.jpg
[video]: https://www.youtube.com/watch?v=bZNGdzGi2BI

## Installing

Copy three files from [bin/](bin/) into your game directory:

* `SC15.DRV`
* `SCSKIDMS.VCE`
* `SCENG1.VCE`

To easily select the driver for game usage, see
[skidset](https://github.com/viniciusferrao/skidset).

### I don't want to use this `skidset` thing.

Then start the game like a neanderthal:

```
LOAD.EXE /u MCGA /ssc
```

You'll probably know the flags here.

## Known Issues

* I'm not a musician, that should say a lot.
* The first notes on the menu music may sound a little off.
* Effects are limited to the GS set, there's probably room for enhancement
  if we manipulate the effects better.
* Music is too compressed, which means that I need to work in some dynamic
  range.
* Title music is great, however the end may be a little bit abrupt. Not an
  issue to be honest, but there is room for improvement.
* I don't own a physical Roland Sound Canvas, so the development and test
  were made with DOSBox Staging 0.83.0-RC1, Nuked SC-55 and the Roland
  Sound Canvas VA.

## Build

If you want to build the software yourself, we provide a batch file per tested
compiler and a `Makefile` for Unix-like systems.

### Unix

    make                the tooling and the voice banks
    make driver         the driver; needs TASM and a flat binary linker

Any C89 compiler builds the tooling; `CC` and `CFLAGS` override the defaults.
For the driver, point `TOOLS` at a directory holding `tasm32.exe` and
`wlink.exe`.

### DOS

    MSCBUILD          Microsoft C 5.10, 16-bit, large model
    TCBUILD           Turbo C 2.01, 16-bit, large model
    WCLBUILD          Open Watcom 1.9, 16-bit
    WCLBUILD 386      Open Watcom 1.9, 32-bit, extender built in

Set `MSCDIR`, `TCDIR` or `WATCOM` to your installation. If unset, the script
looks for the compiler in the default location. These are the same compilers
skidset and skidpack build with, and every build is checked the same way:
`SCCHECK` reports 103 checks whichever compiler produced it.

### Windows

    WCLBUILD WIN32    Open Watcom 1.9, Win32 console

Any modern toolchain should work with the Makefile. Example with MinGW-w64:

    mingw32-make CC=gcc

## Tools for development

| Command | Purpose |
|---------|---------|
| `sc15 mkvce <spec> <out.vce>` | build a voice bank from a text specification |
| `sc15 cfg <SC15.DRV> [field=value]` | change `port`, `gm`, `gs`, `volume` |
| `sc15 kms <song.kms> [-events]` | read a song's tracks and events |
| `sc15 block <driver.drv>` | show the skidset block and check it |

## Testing

```
make test           50 checks against the built driver
make selfcheck      103 checks against the tooling
make release-check  what ships in bin/ is what this source builds
make parity         sc15 block against skidset's own reader
```

`make parity` needs a skidset checkout: `make parity SKIDSET_DIR=../skidset`.

## Documentation

* [bin/README.md](bin/README.md): the shipped files, and rebuilding the voice
  banks from your own game copy
* [docs/driver.md](docs/driver.md): the driver ABI, the implementation, and
  the skidset driver block
* [docs/instruments.md](docs/instruments.md): the instrument mapping
* [docs/story.md](docs/story.md): how the music came to sound right, with the
  recordings in [docs/audio/](docs/audio/)
* [docs/testing.md](docs/testing.md): what each test suite actually checks,
  and the DOSBox setups
* [docs/develop.md](docs/develop.md): findings about the game itself

## Credits

- [UnifiedMT15](https://github.com/LowLevelMahn/UnifiedMT15) by LowLevelMahn: a
  reverse engineered `MT15.DRV` in C, used as inspiration.
- [restunts](https://github.com/4d-stunts/restunts): the disassembly the engine
  side of the ABI, the container format and the KMS event format were read from.
- [Nuked SC-55](https://github.com/nukeykt/Nuked-SC55) by nukeykt, and its
  integration into [DOSBox Staging](https://www.dosbox-staging.org/).

## Acknowledgements

A massive thanks to all the members of the [ZakStunts](https://zak.stunts.hu)
community and the [Stunts Forum](https://forum.stunts.hu), who keep the game
alive. Without them there would be no reason to write a new driver for it.

## Licence

MIT. See [LICENSE](LICENSE).
