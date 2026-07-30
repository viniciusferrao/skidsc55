# Prebuilt binaries

Everything here is built from this repository, so you do not need TASM, a C
compiler or a DOS toolchain to use the driver.

| file | what | built with |
|------|------|------------|
| `SC15.DRV` | the driver itself | TASM 32 and wlink, `format dos com` |
| `SCSKIDMS.VCE` | instruments for the four songs | `sc15 mkvce`, from `voices/scskidms.txt` |
| `SCENG1.VCE` | instruments for the sound effects | `sc15 mkvce`, from `voices/sceng1.txt` |
| `SC15.EXE` | builds the voice banks, edits the driver's configuration block, reads a song, and checks the skidset block | Microsoft C 5.10, large model |
| `SCCHECK.EXE` | self-check for the tooling, needs no game data | Microsoft C 5.10, large model |

Both `.EXE` files are 16-bit DOS programs. They run on DOS, in DOSBox, and
under anything else that runs real mode. They will not run on 64-bit Windows,
which dropped NTVDM.


## About the voice banks

The banks are not new music and carry none. They are the game's instrument
settings, adapted so the same songs and effects play on a synthesiser the game
never supported: `mkvce` starts from the MT-32 instrument records and rewrites
the fields this driver reads, which is what `SETUP.EXE` era drivers did with
their own `.VCE` pairs. The adjustments are what make it sound right on a
Sound Canvas. They ship here so that installing the driver takes a copy and
nothing else.

If you would rather build them yourself from your own game copy, that still
works, one command each:

```
SC15 MKVCE VOICES\SCSKIDMS.TXT SCSKIDMS.VCE
SC15 MKVCE VOICES\SCENG1.TXT SCENG1.VCE
```

The `source` line at the top of each file in `voices/` has to name a path that
reaches your game directory; `mkvce` resolves it relative to the specification
file. On DOS that cannot climb above the mount root, so give it an absolute
path there, for example `source G:MTSKIDMS.VCE`. `make release-check` proves
the shipped pair is byte for byte what the build produces.


## Installing

Copy `SC15.DRV`, `SCSKIDMS.VCE` and `SCENG1.VCE` into your game directory,
beside `LOAD.EXE`. Then:

```
LOAD.EXE /u MCGA /ssc
```

`/u` is your video mode: `EGA`, `CGA`, `TDY` or `MCGA`.

`SETUP.EXE` cannot select the Sound Canvas, because its menus list six sound
drivers and were fixed in 1991. Calling `LOAD.EXE` yourself sidesteps that. See
the README for the `SETUP.DAT` route and its caveat.


## Verifying

These are checked in, so check them. `SC15.DRV` should be 1744 bytes, and the
driver test in `test/` will load and exercise this exact image:

```
make test
```

`SCCHECK.EXE` needs nothing but itself and reports 103 checks on the tooling.
