# SC15.DRV, a Roland Sound Canvas driver for Stunts 1.1

This document describes the audio driver ABI that Stunts 1.1 expects, and how
SC15.DRV implements it over General MIDI. Everything here was derived from the
restunts disassembly (seg027, seg028, seg029, seg012) and from disassembling the
shipped MT15.DRV, AD15.DRV, PC15.DRV and TD15.DRV binaries. Where a conclusion
is a guess rather than a reading of the code it says so.

An independent reverse engineering of MT15.DRV exists at
https://github.com/LowLevelMahn/UnifiedMT15 and agrees with the slot layout and
the calling conventions below.


## 1. How the game loads a driver

`audio_load_driver()` in seg027 does the work.

1. It takes the two characters after `/s` on the command line and stores them in
   `audiodriverstring`, defaulting to `pc`. `GAME.EXE /sSC` therefore selects
   `SC15.DRV`. The one special case in the original executable is `sb`, which is
   rewritten to `ad`.
2. It builds the filename `<name>.DRV` and loads it with
   `file_load_binary_nofatal()`, which allocates through `mmgr_alloc_pages()`.
   That allocator always returns a pointer with offset zero, so the image is
   loaded at offset 0 of its own segment. A driver may therefore address its own
   data with absolute `cs:[offset]` operands, and every shipped driver does.
3. It records the first two characters of the driver's own file name in
   `audiodriverstring2`. Every later resource load is prefixed with that string,
   so `SC15.DRV` makes the game look for `SCSKIDMS.VCE` and `SCENG1.VCE`.
4. It far-calls offset 0 of the image, the init slot, and looks at AL:
   - 0 or 0FFh means the driver is unusable. `audio_load_driver` returns 2 and
     `main()` shuts the game down.
   - a value above 7Fh selects the MIDI code paths in the music engine. The
     voice count is forced to 16 and the internal flag `byte_40634` is set.
   - anything else is taken literally as the number of voices. PC15 answers 7,
     TD15 answers 6.
   MT15 answers 0FFF6h, so SC15 answers 0FFF6h as well.
5. If `byte_40634` is set it loads `MT32.PLB`, hands it to slot 42h, frees it,
   and then sets the master volume to 100 through slot 3Fh. SC15 ignores the
   patch library but must still accept the call, and the game's memory map stays
   identical to the MT-32 build either way.
6. It registers `audiodriver_timer` as a timer callback and marks the engine
   ready.

Resource lookups are case insensitive: `audioresource_compare_chunknames` is
always invoked with its case sensitive flag clear. That is why the containers
hold `HDR1` while the engine searches for `hdr1`.


## 2. Calling convention

Every slot is a far procedure using the Borland C++ medium model cdecl
convention, which is what the game is compiled with.

- Arguments are pushed right to left and the caller cleans the stack. The first
  argument is at `[bp+6]` after the usual `push bp` / `mov bp,sp`.
- Byte and word arguments both occupy a word. Pointers into the game data
  segment are passed as a bare near offset; pointers to loaded resources are
  passed as a far segment:offset pair.
- The return value is in AX.
- DS holds the game data segment on entry and must be preserved. So must BP, SI,
  DI, SS and SP. AX, BX, CX, DX, ES and the flags are scratch.
- The direction flag is clear on entry and must be clear on return.
- Slots are called both from normal game code and from inside the timer
  interrupt handler, so they must not assume they own the CPU. They may not
  block indefinitely.

The engine guards against re-entering the sequencer (`word_407AA` in
`audiodriver_timer`), so a slot is never re-entered from a timer tick while it
is already running.


## 3. Jump table

The image begins with 23 slots of exactly three bytes each, a near jump per
slot, ending at offset 45h. The game far-calls `driver_base + slot * 3`.
SC15.DRV builds the table from an explicit `db 0E9h` / `dw` macro so the
assembler cannot shrink an entry into a two byte short jump.

The last two slots, 3Fh and 42h, only exist in MT15.DRV. AD15, PC15 and TD15
stop at 3Ch and have ordinary code where 3Fh would be. The engine only reaches
those two slots when `byte_40634` is set, which only happens for a driver that
reported a voice count above 7Fh.

### 3.1 Why SC15 declares itself MT-32 class, and what that ties down

A driver chooses its own class. The engine sets `byte_40634` purely from what
the init slot returns, with no check on the filename:

```asm
call    audiodriverbinary       ; far call driver_seg:0000
mov     byte_459D2, al
...
cmp     byte_459D2, 7Fh
jbe     short loc_379B8         ; <= 7Fh: ordinary driver, AL voices
mov     byte_459D2, 10h         ; > 7Fh: clamp the voice count to 16
mov     byte_40634, 1           ; ... and flag MT-32 class
```

SC15 returns 0FFF6h, so AL is 0F6h and we are MT-32 class. That is the reason
the driver receives MT-32 SysEx at all: it is opted into, not imposed.

Seven call sites branch on that flag, and the two branches are different
protocols rather than the same work with one call added. `audio_unk` is the
clearest case. MT-32 class sends the master volume through slot 3Fh and is
finished. FM class instead walks 24 channels writing per-channel volume, then
rebuilds the driver's state from the engine's own records:

```asm
    mov     [bp+var_C], 10h         ; 16 voices
loc_3729A:
    mov     si, di                  ; VOICE record at 0A2B6h, stride 2Eh
    ...
    add     ax, 27h                 ; slot 27h, voice_update, once per voice
    call    [bp+var_A]
    add     di, 2Eh
    dec     [bp+var_C]
    jnz     short loc_3729A
    mov     ax, 0A2B6h
    add     ax, 30h                 ; slot 30h, refresh
    call    [bp+var_A]
```

So the class declaration has a consequence that is easy to miss. Slot 30h is a
stub in SC15 only because we are MT-32 class. An FM class driver has to
implement it, because that is how the engine restores voice state. Ours is a
bare `retf`, which is correct today and would fail silently the moment the
class changed. Slot 27h would also be reached in a context it was not written
for, being called sixteen times in succession as a state rebuild rather than
once per tick for a sounding voice, and SC15's implementation restrikes notes
when it sees the pitch offset move.

Dropping to FM class is therefore a real option but not a small one. It would
remove MT-32 SysEx from the driver entirely, and cost nothing in polyphony,
since returning 10h directly produces the same voice count the MT-32 clamp
produces. What it costs is a working slot 30h and an audit of slot 27h, both in
the part of the driver that runs inside the timer interrupt. Translating one
SysEx address in twenty lines is the cheaper side of that trade.

Only `audio_unk` has been traced in full. `sub_372F4`, the tail of
`audio_driver_func3F` and `audiodrv_atexit` also branch on the flag and have
not been examined, so the list above is a floor, not a complete cost.

| Slot | Offset | Signature                                                        | SC15 behaviour |
|------|--------|------------------------------------------------------------------|----------------|
| 0    | 00h    | `int init(void)`                                                  | probe, reset, UART mode, silence, GM System On, master volume; returns 0FFF6h |
| 1    | 03h    | `void shutdown(void)`                                             | All Notes Off and Reset All Controllers on channels 0..15 |
| 2    | 06h    | `void all_notes_off(void)`                                        | same as slot 1 |
| 3    | 09h    | `void note_on(word ch, VOICE near *v, CHUNK near *c, int note, word vel, INSTR far *i)` | 9n note vel |
| 4    | 0Ch    | `void note_off(word ch, VOICE near *v)`                           | 8n note 00 |
| 5    | 0Fh    | `void voice_free(word ch, VOICE near *v)`                         | nothing |
| 6    | 12h    | `void set_volume(word ch, word unused, word vol)`                 | CC 7, through `vol_curve`: GS renders CC 7 at 40 log where the MT-32 is nearer linear, so the value is mapped by 127*sqrt(v/127), silence and full pinned. MT15 forwards it raw; the same bytes that fade on an MT-32 were ten seconds of silence on an SC-55 (issue 1, measured) |
| 7    | 15h    | `void set_controller(word ch, word unused, word cc, word val)`    | CC cc |
| 8    | 18h    | `void reset_controllers(void)`                                    | CC 121 on channels 0..15 |
| 9    | 1Bh    | `void pitch_bend(word unused, int bend, word ch)`                 | En with bend + 2000h |
| 10   | 1Eh    | `void channel_off(word ch)`                                       | CC 123 |
| 11   | 21h    | `void program_change(word ch, word unused, CHUNK near *c, INSTR far *i)` | Cn, RPN 0, CC 7, CC 10 |
| 12   | 24h    | `void pitch_set(word ch, VOICE near *v, word val)`                | En with val * 60, unbiased |
| 13   | 27h    | `void voice_update(word ch, VOICE near *v, word unused, INSTR far *i)` | portamento retrigger, LFO and envelope routing |
| 14   | 2Ah    | unused                                                            | nothing |
| 15   | 2Dh    | unused                                                            | nothing |
| 16   | 30h    | `void refresh(VOICE near *v0)`                                    | nothing |
| 17   | 33h    | unused                                                            | nothing |
| 18   | 36h    | `int query(void)`                                                 | returns 0FFh |
| 19   | 39h    | `void raw_midi(word count, byte near *data)`                      | writes the bytes verbatim |
| 20   | 3Ch    | `int midi_in(void)`                                               | dequeues a received byte, or -1 |
| 21   | 3Fh    | `void sysex_dt1(word count, byte far *data)`                      | translated to GM master volume |
| 22   | 42h    | `void load_patchlib(byte far *plb)`                               | ignored |

Slots 2Ah, 2Dh, 33h and 3Ch have no call site anywhere in the game. They are
implemented anyway so the table shape stays identical to MT15.DRV.


## 4. Structures the engine passes

### 4.1 INSTR, one record of a .VCE voice bank

93 bytes for the MT-32, PC speaker and Tandy banks; the AdLib bank uses 100.
The size is stored in the first word of the record. Most fields belong to the
music engine, which reads envelope rates and levels, an LFO, an eight entry
arpeggio table and a note transpose from it. The driver reads only these:

| Offset | Meaning |
|--------|---------|
| 05h    | record type. 5 selects the drum kit dispatch described in 4.4. Read by the engine, not the driver. |
| 10h    | signed note transpose, added by the engine before the note reaches slot 09h |
| 12h    | pitch bend sensitivity in semitones, sent as RPN 0 |
| 15h    | velocity sensitivity. 0 means every note plays at velocity 127 |
| 19h    | envelope destination. 2 routes it to pitch bend, 4 to CC 1 |
| 28h    | LFO destination, same encoding |
| 35h    | 1 enables the portamento retrigger in slot 27h |
| 43h    | MIDI channel. Values below 10h pin the part to that channel; anything else leaves the engine to derive it |
| 44h    | program number |
| 45h    | channel volume for CC 7. 0 means do not send it |
| 46h    | channel pan for CC 10 |
| 47h    | unused by the game. SC15 reads it as a bank select MSB, see section 7 |

Two engine-side fields matter more than the table above suggests, established
by measurement rather than disassembly: banks with byte groups zeroed were
played through the real game under DOSBox and compared by octave band.

- 0Ch gates the part. Every sounding record in the shipped banks carries 16,
  and a record with 0 there is silent in the game entirely, notes and all.
  HRM1 ships with 0 alongside a zero volume, which is how the composer parked
  it.
- 1Ch through 26h, the envelope levels and the arpeggio table, audibly shape
  the music: with the block zeroed the songs play but sustain flat, about 5 dB
  hot with the 250 Hz octave up 7 dB. With both groups intact and everything
  else zeroed, the title song matches a stock bank to 0.1 dB in every octave
  band, so 02h and 32h made no measurable difference there. Which of the
  eleven bytes does what has not been separated.

### 4.2 VOICE, one of 16 polyphony slots

At `0A2B6h` in the game data segment, stride 2Eh. The driver touches:

| Offset | Meaning |
|--------|---------|
| 03h    | note as written in the song |
| 04h    | velocity used for the note on |
| 06h    | note currently sounding, maintained by the driver |
| 14h    | envelope output |
| 1Ch    | LFO output |
| 22h    | pitch offset in semitones, moved by the engine's arpeggio table |
| 2Ch    | MIDI channel |

### 4.3 CHUNK, one music or effect track

At `81FCh`, stride 4Ch, 24 entries. Indices 0 to 0Fh are music tracks and 10h to
17h are sound effects. The driver receives a pointer but never dereferences it;
it is listed here because the pointer occupies an argument slot.

The channel a track plays on is `((index & 0Fh) + 1) & 0Fh` unless the
instrument pins one through its 43h field. Track 8 therefore lands on MIDI
channel 9, which is channel 10 counting from one, the percussion channel. That
is the same convention the MT-32 uses for its rhythm part, and it is why the
existing songs work on a General MIDI device without any remapping.

### 4.4 The drum kit dispatch

When an instrument's type byte at 05h is 5, `sub_38DE6` ignores the instrument
and picks one of seven named resources out of the voice bank instead, indexed by
`note - 24`: BASD, TOMM, SNAR, TOMM, TOMM, TOMM, CHHT, TOMM, OHHT, TOMM, OHHT,
TOMM, TOMM, RIDE, TOMM, CRSH.

The AdLib and Tandy banks provide those resources; the MT-32 bank does not, and
none of its instruments has type 5. On the MT-32, and therefore on SC15, drums
are instead an ordinary instrument named DRUM that is pinned to the percussion
channel. The dispatch is dead code for this driver.


## 5. Lifecycle

### Startup

```
audio_load_driver()
    slot 00h   init
    sub_38178()
        slot 1Eh   once per voice, all notes off
        slot 18h   reset all controllers
        slot 06h   all notes off
    timer_reg_callback(audiodriver_timer)
    slot 42h   MT32.PLB, only for a MIDI class driver
    slot 3Fh   master volume 100, only for a MIDI class driver
```

### Update loop

`timer_setup_interrupt()` programs PIT channel 0 with a divisor of 2E9Ch, so
the interrupt runs at 1193182 / 11932, that is 100 Hz. Every tick,
`audiodriver_timer` calls:

- `sub_39700`, which advances the envelope and LFO of all 16 voices and calls
  slot 27h for each sounding voice, plus slot 0Fh when a voice finishes.
- `sub_3868A`, which advances the sequencer and emits slots 09h, 0Ch, 12h, 15h,
  1Bh, 21h, 24h and 39h as the KMS event stream requires.
- `sub_386D6`, which does the same for the eight sound effect tracks.

Nothing in the driver measures time. It has no interrupt hook, no timer and no
state machine. Tempo, note lengths and looping are entirely the engine's, which
is why a driver swap cannot change them.

### Shutdown

`audiodrv_atexit()` runs from the exit handler chain:

```
timer_remove_callback()
slot 3Fh   master volume 100, only for a MIDI class driver
slot 06h   all notes off
slot 03h   shutdown
mmgr_release()
```


## 6. What SC15 does differently from MT15

Every behavioural difference is listed here.

1. No MT-32 display message. MT15 init writes the 20 character string
   `    (C) 1990 DSI    ` to MT-32 system area 20 00 00. SC15 sends nothing
   there.
2. No MT-32 patch or timbre memory programming. MT15 slot 42h uploads MT32.PLB
   as a series of DT1 writes to address 05 xx yy, eight bytes per patch, plus a
   246 byte timbre to 08 zz 00 whenever the patch selects the memory group.
   SC15 ignores the library.
3. No Roland exclusive at all. MT15 wraps everything in `F0 41 10 16 12 ... F7`.
   SC15 emits only two universal SysEx messages, GM System On
   (`F0 7E 7F 09 01 F7`) and Master Volume (`F0 7F 7F 04 01 ll mm F7`), plus the
   optional GS Reset described in section 7.
4. Master volume is translated rather than passed through. Slot 3Fh is only
   ever called by the engine with the MT-32 system area address 10 00 16, the
   master volume, which the engine ramps from 100 down to 0 to fade a song out.
   SC15 recognises that address, rescales the 0..100 value to 0..127, and sends
   the universal Master Volume message. Any other address is dropped, so no
   MT-32 specific byte can reach a General MIDI device.
5. GM System On is sent at init, followed by a settle delay. The MT-32 has no
   such message and MT15 sends none.
6. Program numbers mean something different. On the MT-32 a program change
   selects one of the 128 entries of the synth's patch memory. Stunts leaves
   that memory at the factory default except for patches 0 to 4, so program n
   plays preset timbre n. On a General MIDI device the same number is a GM
   program. The numbers in the voice bank therefore had to be re-derived; see
   instruments.md.
7. Channel 0 is no longer skipped when silencing. MT15's all notes off loop
   runs `dec dh` / `jg`, so it covers channels 15 down to 1 and leaves channel 0
   sounding. The songs do use channel 0, both through the HRM1 part and through
   music track 15. SC15 covers 15 down to 0. This is a bug fix, not a
   compatibility break.
8. The RPN address is parked after use. After writing pitch bend sensitivity,
   SC15 sets RPN to the null address 7Fh 7Fh so that a later data entry
   controller coming out of a KMS controller event cannot silently change the
   bend range. MT15 leaves the RPN selected.
9. A missing interface no longer costs indefinite spinning. MT15's transmit
   loop has an unreachable timeout and spins forever if the MPU-401 never
   becomes ready. SC15 probes the status port at init; if the port never
   reports ready it marks itself silent, still returns success so the game does
   not exit, and drops every byte afterwards. The transmit loop also has a real
   timeout.
10. The MPU-401 base port is configurable, see section 7. MT15 hardcodes 330h.

Everything else is deliberately identical, including the note on and note off
byte sequences, the forced velocity of 127 when the instrument is not velocity
sensitive, the unbiased `value * 60` bender position of slot 24h, the portamento
retrigger of slot 27h and the 40 byte class of MIDI input ring buffer.


## 7. Configuration block

The driver carries a small patchable block at a fixed offset so an installed
SC15.DRV can be adjusted without rebuilding it.

| Offset | Size | Default | Meaning |
|--------|------|---------|---------|
| 45h    | 8    | `SC15CFG` and a NUL | signature, locates the block |
| 4Dh    | 2    | 0330h   | MPU-401 data port. The status and command port is this plus one |
| 4Fh    | 1    | 1       | send GM System On at init |
| 50h    | 1    | 1       | send GS Reset at init, and enable bank select |
| 51h    | 1    | 100     | initial master volume, 0 to 100 |
| 52h    | 6    | 0       | reserved |

GS is on by default because the reference device is an SC-55 and the sound
effects need its variation banks. It does two things: it sends
`F0 41 10 42 12 40 00 7F 00 41 F7` after the GM reset, and it makes slot 21h
emit CC 0 and CC 32 ahead of every program change, carrying the instrument
record's byte at offset 47h. Clear it with `sc15 cfg SC15.DRV gs=0` and neither
happens, so a device that knows nothing about GS never sees a Roland message.

Bank 0 is sent like any other. The driver skipped it until the effects and the
music were found to share channels: the effects sit in variation banks 1 to 4,
the music is all bank 0, and the engine derives a channel for any part the voice
bank leaves unpinned, so the two sets land on the same channels. Saying nothing
for bank 0 left the channel on whatever variation the last effect had selected,
and the next song played through it. An SC-55 covers for that, because GS falls
back to the capital tone when no variation exists at that program, so the music
still sounds right; Roland's SOUND Canvas VA does not, and the menu music after
a race was audibly wrong. Six bytes per program change, and program changes
happen when a part starts rather than per note.


## 7.1 skidset driver block

The image also carries a block of 7-bit ASCII with LF line endings, which
`skidset` reads. `skidset` replaces the game's `SETUP.EXE`, which has no Sound
Canvas row and never will; it scans every `*.DRV` in the game directory for the
magic `SKIDSETDRV01` and grows its menu from what it finds, so the row exists
exactly when the driver does.

```
SKIDSETDRV01
; SC15.DRV - Roland Sound Canvas driver for Stunts 1.1
; Copyright (c) 2026 Vinicius Ferrao. MIT licence.
; https://github.com/viniciusferrao/skidsc55
sound
label Roland Sound Canvas
brief Sound Canvas
help Select if you have a Roland Sound Canvas on the MPU-401 port. Requires an SC-55 or compatible.
SKIDSETEND
```

One `help` key carrying the whole paragraph. It used to repeat and the values
were joined with a space; a second one is a duplicate key now, like a second
`label`. That is what the 448 character line limit is for: the largest paragraph
the window can hold is 15 rows of 26 columns and all of it has to fit on the one
line. This one is 99 characters.

A value is 20h to 7Eh, which refuses tab and every other control code as well as
DEL. Comments are exempt. `disk` is no longer part of the format, and `brief` is
no longer checked for brackets, because skidset draws its own around whatever it
is handed.

A line of the block is a line of the bytes emitted, not a line of the source
that emits them, and both newlines around the terminator are mandatory. The LF
before `SKIDSETEND` is the only thing that ends the `help` value: without it the
value swallows the terminator and the block is refused for having none. The LF
after it is required too. skidset reads the block into a fixed buffer, so it
cannot tell the end of the file from a NUL inside the binary, nor from a
terminator that lands on the last byte it read; rather than guess, it asks for
the byte, which costs a driver nothing. Which `db` directive carries a byte makes
no difference, so a long value may be split across as many as the source needs
provided none of them puts an LF inside it.

The first of those is the only failure here that is silent. A driver whose block
has no `SKIDSETEND` is passed over exactly as if it had no block at all, so it
does not appear in skidset's menu and nothing on screen says why. `sc15 block`
is the cheapest place to catch it, and `make test` runs the case.

Neither token may appear anywhere else in the block, in a comment no more than
in a value, and a driver carries one block and only one. Both rules exist for
readers that find a block by searching bytes rather than by comparing lines,
which is what `sc15 block` and skidset both do to locate it: such a reader handed
a comment containing `SKIDSETEND` reads a block that stops early and has no way
to know that is what happened.

`or compatible` is the idiom the period used for this, as in `IBM PC or
compatible`, and it carries both halves in one word: later Roland models, and
GS modules from anyone else. Naming GS as well would say the same thing twice,
since SC-55 compatibility is what GS means.

The wording follows `SETUP.EXE`'s own menu, whose rows read `Roland MT-32`,
`Sound Blaster card` and `Internal PC speaker`, with the short forms `(MT-32)`,
`(Sound Blaster)` and `(PC speaker)`. Manufacturer and model in the label, model
alone in the brief, and `card` only where it is one. The family rather than a
model, because nothing in the driver is specific to an SC-55.

326 bytes, currently at offset 1418, though nothing depends on where: skidset
searches the whole file. It is data and is never executed. It follows the last
instruction of slot 42h, which is a `retf`, and no jump table entry reaches it.

The `/ssc` switch is deliberately absent. `LOAD.EXE` derives the driver filename
from the switch, so the switch is the filename and skidset derives it back. The
format is `DRVBLOCK.md` in the skidset repository.

`sc15 block SC15.DRV` reads it back out of a built driver and checks it against
every limit above, wording its refusals the way skidset words its own, so a
block that passes here reads the same when it fails there. It also prints the
help paragraph wrapped the width skidset draws it.

The block is its own source, `skidset.inc`, included by `sc15.asm` only when
`NO_SKIDSET` is undefined, so it can be left out:

```
make driver NOSKIDSET=1
```

That build is 1418 bytes and byte identical to the driver as it was before the
block existed, which is what makes the two separable rather than merely
separate: the block is the only thing that changes, and it changes nothing
else. Both variants pass the 50 checks in `test/`.


## 7.2 If the driver grows past about 2.4 KB, measure it

Making a driver bigger is a change to the game, not just to whatever reads it.
`PC15.DRV`, the PC speaker driver, is 2227 bytes. Grown past roughly 2400 it
still loads and the game still runs, but the music loses about a third of its
note attacks and the level drops a fifth, with no error of any kind. It depends
on the size alone rather than on what the added bytes are.

SC15.DRV shows no such effect and has been tested to 2440 bytes, so 1744 leaves
wide margin. The reason to record it is that the failure is silent: nobody would
connect a thinner sounding game back to a driver that grew. Any future growth
past about 2.4 KB should be measured.

The method needs no hardware. Capture the emulator with `SDL_AUDIODRIVER=disk`
and `SDL_VIDEODRIVER=dummy`, reading the result as `f32le`, since the mixer
writes `AUDIO_F32` and signed 16 bit produces convincing nonsense. Capture the
current driver twice before capturing the new one, because two identical runs
are what establishes the noise floor. Compare level, note onset count and the
proportion of silent frames, all measured from the frame the music starts rather
than from the first sample: a cold host file cache delays the game by about a
second and otherwise swamps the silence figure.


## 8. Open questions

These are things the disassembly did not settle. None of them affect the driver,
but they are worth recording.

- The purpose of slot 36h. MT15 answers 0FFh and nothing calls it. It may be a
  capability word for a driver revision the game never shipped.
- The purpose of slots 2Ah, 2Dh and 33h. All four shipped drivers make them
  stubs and the game never calls them.
- Slot 30h is a stub in every shipped driver, and the name "refresh" is still a
  guess, but the call site is now known: the FM class branch of `audio_unk`
  runs it with the base of the voice array immediately after calling slot 27h
  once for each of the 16 voices. An MT-32 class driver never reaches it. See
  section 3.1; that is why SC15 can leave it empty.
- The second argument of slots 06h, 09h, 0Fh, 12h, 15h and 21h. The engine
  always passes something meaningful there, usually a chunk pointer or a zero,
  but no shipped driver reads it.
- The first word of a resource container. `audioresource_find` never reads it.
  In the KMS files it equals the file size; in the VCE banks it does not match
  any obvious length. mkvce writes the content size.
- KMS event 0E8h copies a payload to `717Eh` and the dispatcher then hands
  `byte_42A08 - 4` bytes to slot 39h. The minus four does not line up with the
  number of bytes the parser consumed. No shipped song uses the event, so the
  discrepancy was not chased.
- Whether the MT-32 reverses CC 10 relative to General MIDI. This is asserted
  often enough that it is worth flagging, and it decides whether the pan values
  in the voice bank should be mirrored. See instruments.md.


## 9. Building

The driver has to be a flat image whose first byte is offset 0 of its segment.
`wlink format dos com` produces exactly that from a source that uses `org 0`,
with no .COM style 100h bias, which the repository's own toolchain can do
without any extra downloads.

```
make driver banks
```

That writes `SC15.DRV`, `SCSKIDMS.VCE` and `SCENG1.VCE` at the top of the
tree. Copy the three next to `GAME.EXE` yourself, or let skidset manage the
game directory; nothing installs automatically. `make test` runs the harness
described in testing.md.

The driver itself needs nothing beyond `tools\bin`. The two voice banks are
generated by `sc15 mkvce`, which is strict C89 with no dependencies and builds
with any C compiler, including a 1990 one. There is no scripting runtime in the
build at all.

wlink prints `no starting address found` while linking. That is expected: giving
the module an entry point makes wlink emit the image from that entry point
onward and lose the jump table.


## 10. Tools

Three groups, in different places, because they answer to different audiences.

`src/` builds `sc15`, the only tooling a user needs. Strict C89 with no
dependencies, so it compiles on a modern host and on a 1990 DOS compiler alike;
both were checked and both emit byte identical voice banks.

| Command | Purpose |
|---------|---------|
| `sc15 mkvce <spec> <out.vce>` | build a voice bank from a text specification |
| `sc15 cfg <SC15.DRV> [field=value]` | read or change the driver configuration block |
| `sc15 block <driver.drv>` | show the skidset driver block, and check it against every limit skidset enforces |

`test/` holds development tooling, not needed to build or play.

| Tool | Purpose |
|------|---------|
| `drvtest.c`, `emu86.c` | executes the built driver| executes the built driver in a 16 bit interpreter and checks the MIDI it emits, including stack depth and protocol validity |

`sc15 kms` decodes a `.KMS` song into its instruments, tracks and events. It is
read only, and it is where the instrument mapping came from: what a song
actually plays is not visible anywhere else, including the CC 7 that some tracks
send and others do not.

A Perl 8086 disassembler, and decoders for `MT32.PLB` and for `.VCE` banks, were
used during the reverse engineering and are not part of this repository. They
belong to restunts, which is where the disassembly lives.
