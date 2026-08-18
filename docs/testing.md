# SC15.DRV testing

Two layers of testing. The first runs here, against the built binary, with no
DOS and no synthesiser. The second needs real playback and is written out as a
procedure with explicit pass criteria.


## 1. Automated harness

`test/` holds a small 16 bit real mode interpreter in C89, `emu86.c`,
and the checks that drive it, `drvtest.c`. It loads
the built `SC15.DRV` image at offset 0 of a segment, exactly as
`mmgr_alloc_pages()` does, far-calls the jump table the way the music engine
does, models an MPU-401 UART at 330h and 331h, and checks the resulting MIDI
byte stream.

It executes the shipped binary rather than a re-implementation, so it catches
assembler and linker problems as well as logic problems. The interpreter only
covers the instruction subset the driver uses and aborts on anything else, which
means an unexpected opcode is itself a reported failure.

```
make test
```

or directly:

```
./drvtest SC15.DRV SCSKIDMS.VCE
```

It is strict C89 with no dependencies like the rest of the tooling, and has
been built and run both with clang and with Microsoft C 5.10 into a DOS
executable, so a period machine can verify its own driver build. Memory is a
sparse page table rather than a flat megabyte, which is what makes that
possible: the driver touches three segments, so about six 4 KB pages are ever
allocated.

### Current result

50 checks, all passing.

| Area | Checks |
|------|--------|
| init | reports a MIDI class voice count, resets the MPU-401 and selects UART mode, silences all 16 channels, sends GM System On, sends GS Reset after it when GS is on and no Roland exclusive at all when it is off, sets master volume to full |
| program change | correct byte sequence for BASS on channel 2, correct kit selection and centred pan for DRUM on channel 10, bank select emitted for both although both are bank 0 |
| notes | velocity forced to 127 for a non velocity sensitive instrument, sounding note recorded in the voice record, note off releases the recorded note, a drum note plus the bank transpose lands on GM Bass Drum 1 |
| controllers | channel volume, arbitrary controller, pitch bend up and centred, single channel all notes off, coarse pitch set scaled by 60 |
| SysEx | the MT-32 master volume address is translated to a GM master volume message, every other MT-32 address is dropped, MT32.PLB emits nothing |
| shutdown | all 16 channels silenced, including channel 0 which MT15.DRV skipped |
| MIDI input | empty queue returns -1, inbound bytes are drained while the transmitter is busy and can be read back in order |
| stubs | slots 2Ah, 2Dh, 30h and 33h emit nothing, slot 36h answers 0FFh |
| no hardware | init still reports success so the game does not exit, and no bytes are emitted afterwards |
| retrigger | a note is restruck on schedule when an instrument asks for it, is silent between restrikes, and is never restruck when the period is 0 |
| re-init | a second init still reports success, still resets the interface and reselects UART mode, and the driver still plays afterwards, which is the path a second launch inside one DOS session takes |
| transmit timeout | a transmitter that never becomes ready emits nothing rather than part of a message, stays silent afterwards even once the interface recovers, and comes back on the next init |
| ABI | every slot returns DS, SS, SI, DI and BP as it found them, which correct MIDI does not prove |
| stack | peak depth across every slot, which matters because the engine calls them from inside its timer interrupt |
| protocol | across every case above, no data byte was emitted without a status byte and no status byte appeared inside a SysEx |

### What it does not cover

Timing, and anything that depends on the engine actually running: tempo,
looping, voice stealing, the interaction between music and sound effects. Those
need section 2.

### Continuous integration

`.github/workflows/ci.yml` builds the tooling as strict C90 with gcc
and clang on Linux and macOS, runs cppcheck and the GCC analyzer over it,
checks the formatting against a pinned clang-format 22, and repeats the build
under the address and undefined behaviour sanitizers.

It does not build the driver and it does not run the harness above. The driver
is 16-bit assembly and needs TASM plus a linker that emits a flat binary, and
the harness needs a built driver image to load, so both are checked locally.

What it can check is the tooling, on data it generates rather than data it
ships. `mkvce` copies instrument records out of the game's own MT-32 voice
banks, which this project does not redistribute, so `test/selfchk.c`
synthesises a two-instrument bank with a known value in every field and reads
those fields back out of the result: that the program number comes from the
specification, that `volumescale` is applied, that `=` preserves what the
source bank held, that a trailing `name=value` overrides it, that the SC15 bank
byte is written where MT-32 records have nothing, and that the container header
describes what follows. It then patches a driver image with `cfg` and checks
that the named fields moved and the unnamed ones did not, and feeds both
commands a missing source bank, an instrument that is not in it, a short name,
a short line, an out of range value, an unknown trailing field, an instrument
before any `source` directive and an absent specification, requiring each to
return an error and leave no half-written output behind. It also builds a
driver-shaped file around a skidset block and checks that a good one is read
and every malformed one refused. 103 checks in all, and
it runs locally:

```
make selfcheck
```

The sanitizer job runs the same program, which is where a half-parsed record or
a double free on the refusal path would surface. It found the size check in
`cfg` reading 150 bytes into a block that ends at 82, which no real driver was
ever short enough to expose.

`kms` cannot be reached that way, because it needs a song and songs are game
data this repository does not carry. It gets a generated corpus instead: random
files at every length, and headers whose chunk count does not fit what follows.
Each has to be refused or decoded without the sanitizers objecting. It is the
parser least able to trust its input, since every offset it follows comes out of
the file it is reading.

The self-check is a C program for the same reason the harness above is: every
check it makes has to be makeable on the machine the tooling targets, and the
16-bit build is the one that most needs checking. `MSCBUILD.BAT` builds it
alongside the tool as `SCCHECK.EXE`.

The DOS build system mirrors skidset's and skidpack's: the same three batch
files driving the same compilers, so anyone building one project can build the
others. All verified 2026-07-30, each producing an `SCCHECK` that reports its
103 checks passing:

| build | compiler | output |
|-------|----------|--------|
| `MSCBUILD.BAT` | Microsoft C 5.10, large model | `SC15.EXE`, `SCCHECK.EXE`, what `bin/` ships |
| `TCBUILD.BAT` | Turbo C 2.01, large model | the same pair, not shipped |
| `WCLBUILD.BAT` | Open Watcom 1.9, 16-bit, 8086 | the same pair, not shipped |
| `WCLBUILD 386` | Open Watcom 1.9, 32-bit | `SC1532.EXE`, `SCCHK32.EXE`, DOS/32A bound in |
| `WCLBUILD win32` | Open Watcom 1.9, `-bt=nt` | `SC15W.EXE`, `SCCHKW.EXE` |

Turbo C is why the C sources check out CRLF; see `.gitattributes`. The 32-bit
build is also a second opinion on the sources, since `int` is 32 bits there,
and the win32 build is the same program for a modern console, possible because
the tooling is plain stdio with no BIOS anywhere.

It links `cmd_mkvce()` and `cmd_cfg()` and calls them in its own process, so a
refusal is read from the return value the code produced, and the sanitizers see
the refusal paths rather than watching from outside a child process.

What that leaves uncovered is `main()`'s dispatch: twelve lines that pick a
subcommand by name and print usage. The argument handling behind it is
exercised; the `strcmp` is not.


## 2. Song and bank analysis

`sc15 kms` decodes a .KMS song into its instrument list, track
list and event stream. It was used to derive the percussion mapping and to find
the missing STRT instrument, and it is the quickest way to check what a song
expects before listening to it.

```
sc15 kms ../stunts/SKIDTITL.KMS
sc15 kms ../stunts/SKIDTITL.KMS -events
```

Confirmed for all four songs: every instrument a song names is present in
`SCSKIDMS.VCE`, which is more than can be said for `MTSKIDMS.VCE`, and every
drum note lands inside the General MIDI percussion map. See
instruments.md section 3.1 for the full note table.


## 3. Playback testing

Played end to end against an emulated Roland SC-55 v1.21, using DOSBox Staging
0.83 with Nuked SC-55 and the real firmware ROMs. Menu music, race music,
engine, skid, crash, blowout, scrape and jump impact all confirmed working.
Loading and init are structurally confirmed too: `main()` shuts the game down
when `audio_load_driver` reports failure, so a game that reaches its intro has
a driver that answered the init slot correctly.

Getting there took three attempts at a synthesiser and they are worth recording,
because two of them produce misleading results:

- DOSBox 0.74 on Windows can only reach whatever the system MIDI mapper
  points at. Usable, but it is whatever the host happens to have.
- A SoundFont, through FluidSynth in either DOSBox-X or Staging. Fine for
  the music, useless for the effects. Five SC-55 SoundFonts were checked and
  every one carries General MIDI bank 0 and nothing else, so every car sound
  falls back to Helicopter. Several rounds of effect mapping were wasted before
  this was noticed; reading a SoundFont's preset table answers it in seconds.
- Nuked SC-55, cycle accurate emulation from the firmware ROMs. This is the
  one. It has the GS variation banks, so the effects are the sounds Roland
  built for them.

Remaining open items: the balance of the menu music, and re-tuning the engine
bend range now that it is driving a real Car-Engine sample rather than a
substitute.

A fourth synthesiser, Roland's own SOUND Canvas VA, found a driver bug that the
other three had hidden. The music after a race was wrong, and only there. The
captured stream is identical whichever synth is selected, so the difference was
in the interpretation: the driver was leaving channels on the variation bank the
last sound effect had selected, and an SC-55 falls back to the capital tone when
a variation does not exist at that program while the VA does not. Slot 21h now
sends bank select on every program change, bank 0 included. Worth remembering
next time something is "confirmed working": three synthesisers agreeing can mean
three synthesisers being equally forgiving.

This is the procedure and the criteria.

### Install

```
make driver banks
cp SC15.DRV SCSKIDMS.VCE SCENG1.VCE /path/to/stunts/
```

Or copy the same three files from `bin/`. No build installs the files. You
copy them yourself, or skidset does. Start the game with

```
GAME.EXE /sSC
```

The two characters after `/s` become the driver name prefix, so `/sSC` selects
`SC15.DRV` and the `SC` prefixed voice banks.

That is the reconstructed executable restunts builds. A shipped release has no
`GAME.EXE` at all: `LOAD.EXE` assembles it in memory at each launch from
`<variant>.HDR + EGA.CMN + <variant>.DIF + <variant>.COD`. The drivers are still
ordinary files on disk, so the same three files go in the same directory and the
switch is delivered one level further out, either by editing the second line of
`SETUP.DAT` that `STUNTS.COM` executes, or directly:

```
LOAD.EXE /u EGA /ssc
```

Verified against an untouched 1.1 release both ways. The negative control is
what makes it conclusive: removing `SC15.DRV` and leaving everything else alone
stops the same command line at `Can't find driver!`, so the switch is reaching
the assembled game rather than being consumed by the loader. See the README for
the `SETUP.DAT` details and the caveat that `SETUP.EXE` rewrites it.

### Targets

| Target | Setup |
|--------|-------|
| DOSBox Staging with Nuked SC-55 | `mididevice=soundcanvas` and the SC-55 ROMs in `soundcanvas-roms`. The reference setup, see `dosbox-staging.conf.example`. Cycle accurate emulation of the real hardware, and the only software option that has the GS variation banks the effects need |
| DOSBox Staging with Roland SOUND Canvas VA | `mididevice=port` and `midiconfig` set to the index of the port VST MIDI Driver publishes the VA on. See `scva.conf.example`. Roland's own emulation, and the other software option with the GS variation banks |
| DOSBox-X with FluidSynth | `mididevice=fluidsynth` and `fluid.soundfont` pointed at a General MIDI SoundFont. Music only: every SoundFont tested carries General MIDI bank 0 alone, so the car effects fall back to Helicopter. See `dosbox-x.conf.example` |
| DOSBox-X with an emulated MT-32 | `mididevice=mt32` and `mt32.romdir`, running `GAME.EXE /sMT`. The reference the mapping was derived from |
| DOSBox 0.74 | `mpu401=intelligent` and `midiconfig` pointed at a system MIDI device. Only reaches what the system MIDI mapper offers |
| VirtualMIDISynth | any General MIDI SoundFont, as a system wide MIDI device |
| Real MPU-401 with a Sound Canvas, SC-55 to SC-88 Pro | base port 330h, or patch the configuration block, see below |

### Checks

1. Loading. The game starts and reaches the title screen. `Can't find driver!`
   means the game looked for `SC15.DRV` in its own directory and did not find
   it, which also confirms the `/s` switch arrived. The game exiting immediately
   after the video mode change means init returned failure instead, which SC15
   only does if the image is corrupt, since a missing interface is reported as
   success.
2. Menu music. The title theme plays. Expected parts: drums on channel 10, a
   synth bass, a synth brass lead, a French horn harmony and a clean electric
   guitar, the last of which is the reconstructed STRT part and is silent on an
   MT-32.
3. Car and track selection music, the victory theme and the game over theme all
   play. Each uses the same bank; SKIDSLCT is the one with two guitar parts an
   octave apart.
4. Looping. Let the title theme run through at least two full cycles. It should
   loop without a gap and without a stuck note.
5. Tempo. Compare against the AdLib driver, `GAME.EXE /sAD`, which is unaffected
   by this work. The tempo comes entirely from the engine, so any difference
   points at the host MIDI path rather than the driver.
6. Percussion. Kick, snare, closed and pedal hi-hat, crash, ride and three toms
   should all be recognisable, and all on channel 10. A melodic note where a
   drum should be means the DRUM record lost its channel 9 pinning.
7. Program changes. Each part should have its own timbre from its first note.
   Everything sounding like a piano means the program changes are not arriving.
8. Fade out. Leaving a menu fades the music out rather than cutting it. That
   exercises the master volume translation in slot 3Fh. A device that ignores
   the universal Master Volume message will cut instead of fading; that is a
   device limitation, not a driver fault.
9. Sound effects. Start a race. Engine pitch should sweep smoothly with the
   revs, which exercises the 24 semitone bend range. Skidding, scraping,
   crashing and kerb bumps should each produce a distinct sound.
10. No stuck notes. Quit from a menu with music playing, and quit mid race with
    the engine running. Nothing should be left sounding. This is the check that
    MT15.DRV fails on channel 0.
11. No crashes. Play through a full race, a replay and the track editor.

### If the interface is not at 330h

Patch the configuration block in the built `SC15.DRV`. It starts at offset 45h
with the ASCII signature `SC15CFG`, and the base port is the little endian word
at offset 4Dh. For an interface at 300h:

```
offset 4Dh: 00 03
```

The same block holds the GM reset flag at 4Fh, the GS reset flag at 50h and the
initial master volume at 51h. See driver.md section 7.

### GS

Setting the byte at offset 50h to 1 makes the driver send a GS Reset after the
GM reset, and enables bank select for instruments that ask for a non-zero bank.
Test this only after every check above passes on plain General MIDI. Then
re-run check 2 and check 9 on a non-GS device. This confirms that the driver
stays correct when the flag is 0.


## 3.1 Launching twice in one DOS session

Start the game, quit it, and start it again without leaving DOS, and the second
launch dies. It enters mode 13h and never returns to text, so `COMMAND.COM`
draws its next prompt into the 320x200 graphics framebuffer: a `C:\>` in huge
characters on a black screen, which reads as a freeze and is not one. The shell
is alive; `MODE CO80` restores the display.

This is not the driver. It was isolated with a single variable control, two
configurations differing in exactly one line:

```
LOAD.EXE /u MCGA /ssc     SC15.DRV
LOAD.EXE /u MCGA /smt     MT15.DRV, the driver BrÃ¸derbund shipped
```

Same emulator, same SC-55, same MPU-401 path, same video mode, same cycles,
same directory. Both fail identically on the second launch, so the audio driver
is not the variable. Two other hypotheses were tested and rejected on the way:
`core = dynamic` in DOSBox Staging, which is a bad idea here for other reasons
and is covered in `dosbox-staging.conf.example`, and the driver's own re-init
path, which the harness now exercises against an interface left in UART mode by
a previous run.

Nor is it a regression in the emulator. DOSBox Staging 0.83.0-RC1 and 0.82.2,
the current stable, fail identically on the same configuration with MIDI held at
`none` in both so that the emulator version is the only variable. The 0.83 line
is where Nuked SC-55 arrives, so an older build is not a way out of this.

It is treated here as an emulator issue and not chased further. Both versions
tested share the same codebase, so a fault common to them is consistent with
that. What would separate emulator from game is a run on real hardware, or on
the 0.74 line, and neither has been done.

The mechanism is unknown, and the obvious candidate is already ruled out.
Reading `audiodrv_atexit` in `src/restunts/asm/seg027.asm`, the shutdown path
calls `timer_remove_callback` before `mmgr_release`, so the timer interrupt is
detached before the driver's memory is handed back. A stale interrupt handler
firing into memory the next program has just claimed is not what is happening.

Stunts is an unusual case for a second launch either way: `LOAD.EXE` assembles a
200 KB executable in conventional memory from `HDR + CMN + DIF + COD` and jumps
into it at every launch, rather than being an executable DOS loads and frees.
Running that twice in one session is not something anyone had reason to test in
1991, when you ran the game and then turned the machine off.

Quitting the emulator between sessions avoids it entirely, which is why it is a
footnote rather than a blocker.


## 3.2 Silent music on the evaluation screen

The results screen after a race sometimes has no music, and once it happens the
music stays gone until something else loads a song successfully. It is a defect
in the game, measured rather than argued: on a silent results screen the engine
emits no MIDI at all, while sound effects keep flowing through the same driver.

See [develop.md](develop.md) for the reproduction, the trace and the capture.


## 4. Known limitations

- The settle delay after GM System On is an I/O bound loop on port 61h, roughly
  120 ms on real hardware. Under an emulator that does not tie port 61h to real
  time the delay may be shorter, and a synthesiser that needs longer to process
  a GM reset could miss the first program change. If that shows up, the fix is
  to raise the two `io_delay` counts in `gm_init`.
- The driver has no MIDI thru, no running status compression and no output
  buffering. Every byte is written synchronously from inside the timer
  interrupt, the same as MT15.DRV. At 31250 baud a dense passage on a slow
  machine can therefore steal time from the interrupt handler, exactly as it
  did on an MT-32.
- Velocity is always 127 unless an instrument sets its velocity sensitivity
  byte, and none of the shipped instruments does. That is faithful to the MT-32
  build but means dynamics come only from CC 7.
