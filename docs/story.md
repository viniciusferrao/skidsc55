# How SC15.DRV got its sound

This is the account of one night's work, written down because the interesting
part is not the driver. The driver was the easy half. The hard half was finding
out why the music sounded wrong, and the answer was found by measurement after
an evening of confident guessing had failed.

The audio in `audio/` is the evidence. Play it in order.


## 1. The easy half

Stunts 1.1 loads an audio driver as a raw image at offset 0 of its own segment
and calls into a jump table of 23 three byte entries. `MT15.DRV`, the MT-32
driver, is the specification. The game, the songs and the music engine are not
modified, and nothing about them is patched: a new driver and two voice banks
are dropped next to the game and selected with a switch it already had.

That part was reverse engineering, and it went the way reverse engineering goes.
The ABI, the resource container, the KMS event format, the calling convention,
the timer interrupt constraints. A harness that loads the built binary into a
small 8086 interpreter, far calls each slot, models an MPU-401, and checks the
MIDI that comes out. It passes 44 assertions and it caught real faults.

Two findings from that phase matter later.

General MIDI has no car sounds. Not an engine, not a tyre screech, not a crash,
anywhere in its 128 programs. They exist only in the Roland GS variation banks
under capital tone 125, which is why the driver is named after the device rather
than the standard.

And the engine note is not played from a track. `sub_39050` builds an event in
memory with the note byte set to `0FFh`, which the engine sign extends to -1, so
the note played is `transpose - 1`. With the MT-32's transpose of 12 that is
MIDI note 11, forty nine semitones below the root key of any sampled program.
An MT-32 got away with it because Engine 1 is a ring modulated stack rather than
a sample, and an oscillator at note 11 is a low buzz, which is what an engine
is. A sample has to be played near its root key, so the effects bank sets
`transpose=61`.

Remember that shape. An MT-32 timbre tolerating a pitch a sampler cannot.


## 2. The wrong half

The driver worked. The effects were right. The music was not, and the report
was, reasonably, "the menu music sounds a bit off".

What followed was several hours of adjusting things by ear. The guitar was
raised because it sounded buried. The drums were lowered because they sounded
loud. A `volumescale` factor was added to pull the whole score down under the
engine. Each change was plausible, each was defended with a paragraph of
reasoning, and none of them addressed the actual fault.

Then the suggestion that changed the evening: capture the audio, run an FFT, and
compare against the MT-32, because the MT-32 is the benchmark.

The first response to that was a caveat, and the caveat was half wrong. MT-32 is
LA synthesis and the SC-55 is sample playback, so the same nominal instrument
has legitimately different spectra and a raw spectral diff lights up everywhere.
That is true. What it misses is that the MT-32 defines what "right" means here,
because the composer wrote against one. Balance is measurable even when timbre
is not. The reframing was the user's, and it was correct.


## 3. Building the instrument

Capturing DOSBox audio turned out to be the hard part, and the first two
attempts were bad engineering.

DOSBox has a capture hotkey. Driving it from a script requires the emulator
window to have focus, and Windows refuses to let a background process take
focus. Every keystroke went into the chat window instead. That failed silently,
produced no recording, and could have gone somewhere worse.

The second attempt was going to be a hand written WAV reader and FFT in C89.
The instruction that stopped it was blunt and correct: download ffmpeg instead
of writing software, be wise.

The third attempt was the right one, and again it came from the user: is there
no way to redirect the audio out of DOSBox? There is. SDL has a disk audio
driver. Set `SDL_AUDIODRIVER=disk` and `SDL_DISKAUDIOFILE`, and the mixer output
goes straight to a file, headless, no window, no hotkey, no focus. It writes at
about twice real time because nothing is pacing the emulation, which makes a
three minute capture take ninety seconds.

One trap: DOSBox Staging's mixer is `AUDIO_F32`, not signed 16 bit. Read as
`s16le` the numbers look like clipped noise at -0.3 LUFS. Read as `f32le` they
look like music at -23 dB RMS. The first analysis was run on the wrong
interpretation and had to be thrown away.

With that working, the rig is: capture the same passage from an emulated MT-32
and from SC15, convert with ffmpeg, and compare energy across eight octave
bands. Normalise both to remove any overall level difference, because the level
was a deliberate choice and the shape is what is in question.


## 4. What the measurements found

The first comparison, full mix:

| band  | normalised, SC15 minus MT-32 |
|-------|------------------------------|
| 63 Hz | +0.4 |
| 125   |  0.0 |
| 250   | -1.3 |
| 500   | -1.1 |
| 1 kHz | +1.0 |
| 2 kHz | +1.3 |
| 4 kHz | +3.0 |
| 8 kHz | +2.1 |

Thin and bright. Missing body where bass fundamentals and horn warmth live,
excess where hi-hat and pick attack live. A tilt of about four decibels.

The obvious suspect was the drums, since the title theme plays 122 closed hi-hat
hits against 26 kicks and a General MIDI kit is brighter than an MT-32 rhythm
part. The SC-55 has eight kits selectable by program change on channel 10, and
that number had never been touched.

Six kits were swept. Standard, Room, Power, Electronic, Jazz and Brush moved the
spectrum by half a decibel. The program bytes were verified genuinely different
first, because by that point nothing was being taken on trust. Brush and Power
are about as different as two kits get. The drums were not the brightness.

That null result is what made the evening work. Guessing would have "fixed" the
drums.

Next, per part. Each instrument soloed on both machines and compared like for
like:

| part | 63   | 125  | 250  | 500  | 1k   | 2k   | 4k   | 8k   |
|------|------|------|------|------|------|------|------|------|
| BASS | -3.0 | -3.1 | -0.9 | +1.5 | +2.8 | +2.7 | +2.3 | -2.3 |
| LEAD | +1.7 | +0.3 | +0.7 | +1.6 | +0.7 | -1.2 | -1.2 | -2.3 |
| HRN1 | -0.5 | -1.8 | -0.9 | +0.6 | +1.8 | +2.4 | +1.8 | -3.5 |
| DRUM | -0.9 | -1.5 | -0.9 | +1.7 | +3.1 | +2.0 | -0.5 | -3.3 |

The bass had lost both bottom octaves and gained the mids. Six bass programs
were swept against the reference and every one of them was short at 63 and 125
Hz, including the one already in use, which scored best of the six.

A deficit that survives six unrelated instruments is not caused by the
instrument. So it was the pitch.

```
BASS transpose = 12, written notes 28..49  ->  MIDI 40..61
fundamentals: 82.4 Hz .. 277.2 Hz
an octave lower:  41.2 Hz .. 138.6 Hz
```

The bass was playing in a baritone register. `mkvce` had faithfully copied the
composer's transpose of 12, which is correct on an MT-32 and wrong on a sampler,
for exactly the reason the engine note was wrong: an MT-32 timbre carries coarse
tuning inside the patch, so Syn Bass 3 sounds an octave below the note it is
handed. The same failure mode, found months of work apart, in a part nobody
suspected.

Correcting the octave and the program together took the mid-range excess from
+2.8 and +2.7 dB down to -0.1 and -0.6. Synth Bass 2 wins only at the right
octave; at the wrong one it scored worse than Synth Bass 1, which is why the
first program sweep looked like a dead end.

Only four instruments in the bank carry `transpose=12`: the bass and the three
horns. Exactly the timbres an MT-32 tunes down internally.


## 5. The actual root cause

Two smaller faults surfaced next, and the second one was the real story.

`BASS` was capped. It is 127 in the MT-32 bank, the maximum, so a 0.7 multiplier
cut it to 89 with nowhere to compensate. The volume column cannot exceed 127, so
the scale factor was a ceiling that only the loudest part ever hit. Levels were
restated per part so the bass could keep its full 127.

Then a description that could not be explained by any of it: at the start of the
menu music something percussive is dominant, the horns come in later and are
weak, and on the MT-32 the horns are far more powerful.

That is a statement about time, not timbre, and it sent the search somewhere it
had not been all evening: the song's own event stream.

```
track 0  DRUM Kick/Snare   controller 07 6F   -> CC 7 = 111
track 1  DRUM Hats         (none)
track 2  BASS              controller 07 7F   -> CC 7 = 127
track 3  LEAD              controller 7B ..   -> not CC 7
track 4  HRN1 "Harm"       (none)
track 5  STRT              controller 7B ..   -> not CC 7
```

The songs set their own channel volumes. The drum and bass tracks send CC 7
immediately after selecting their instrument, which overrides whatever the voice
bank asked for. The lead, harmony and Strat tracks send nothing, so those keep
the bank value.

Which means `volumescale 0.7` never did the job it was added for. It could not
turn the score down, because the two loudest parts opted out. It turned down
only the melody and the harmony, by three decibels, and left the rhythm section
at full level. A percussive, dominant opening with weak horns behind it, from
the first build onward, caused by the one change made to be helpful.

Every level is now the composer's own, straight out of `MTSKIDMS.VCE`. The
result sits within 0.3 LU of the MT-32:

| | integrated loudness |
|-|---------------------|
| MT-32                  | -19.6 LUFS |
| SC15, before           | -22.3 LUFS |
| SC15, after            | -19.3 LUFS |


## 6. What this cost, and what it is worth

Three real bugs, all in the voice bank, none in the driver:

- the bass sitting an octave above where a sampler can carry it
- the bass capped at 89 by a multiplier it could not exceed
- the melody and harmony attenuated by a factor the rhythm section ignored

Every one was found by measurement. None was found by listening and adjusting,
and several hours were spent listening and adjusting first.

The first of those was recorded here as a transpose only an MT-32 could
interpret, on the reasoning that an LA timbre carries its own coarse tuning and
Syn Bass 3 therefore sounds an octave below the note it is handed. Section 7 is
the measurement that took that apart. The change was right; the reason was not.

The mistakes made along the way are part of the record. `core = dynamic` was
added unprompted to DOSBox and is a bad fit for a game that assembles its own
executable in memory. Keystrokes were injected into the wrong window. A spec
file was truncated to zero bytes by a shell redirect and recovered only because
it was already committed. A whole series of per part measurements was invalid
because muting by setting volume to 1 does not mute a track that sends its own
CC 7, and the numbers quoted from it, including one about the horns, should not
be trusted.

That last one is worth sitting with. The failed isolation was itself the proof
of the override. The measurement that was wrong is what pointed at the answer.

The method that worked, and it is not complicated:

- define the reference precisely, and verify it is what you think it is
- change one variable
- verify the change actually took effect in the artefact, not in the source
- let null results eliminate suspects rather than treating them as failures
- do not trust a metric past the point where it stops modelling the thing you
  care about, and say so when you reach that point

Two of the five sweeps returned nothing. Those two did most of the work.


## 7. The octave that was not an error

Four instruments carry a transpose of 12 in `MTSKIDMS.VCE`: `BASS` and the three
horns. `BASS` had it cancelled and the horns did not, on an untested theory
about MT-32 patches.

`SKIDVICT` is where it shows, playing two horn parts of 91 notes each and taking
`HRN1` to MIDI 99. It only plays after a race is won, so it was reached by
copying it over `SKIDTITL.KMS` and renaming the chunk at offset 6 from `vict` to
`titl`. Every song names the same bank, so nothing else moves.

Comparing two full mixes against the MT-32 by octave band energy answered
nothing: 2.059 with the horn transpose against 2.106 without, the two banks
verified different beforehand at one byte, offset 211. One part in six moving an
octave does not shift the shape of a mix, and the gap between an SC-55 and an
MT-32 swamps what it does shift.

Isolating the horn does answer it, though not by lowering the other volumes,
which tracks defeat by sending their own CC 7. Ending the tracks works: `00 D9`,
a delta of zero and `track_end`, written over the first event of each unwanted
track, with track 0 cut after its `set_tempo` so the tempo survives. `sc15 kms`
then reports 0 notes on five tracks and 91 on `HRN1`.

The reading is direct, at semitone resolution on a sustained note:

| | SC-55 at transpose 0 | SC-55 at transpose 12 | MT-32 |
|-|----------------------|-----------------------|-------|
| horn | A#3, 233 Hz | A#4, 466 Hz | A#4, 466 Hz |
| bass | A#1, 58 Hz  | A#2, 116 Hz | A#2, 116 Hz |

The MT-32 sits with the transposed column in both rows, 32.6 dB below the
untransposed horn across the bands where that version plays its lowest notes.
`Syn Bass 3` and `Fr Horn 1` sound the notes they are given, so the composer
wrote these parts high and an MT-32 plays them high.

That makes it a question of taste rather than fidelity. All three builds were
made and listened to, on the eval theme where the horn goes highest:

| build | BASS | horns | recording |
|-------|------|-------|-----------|
| the MT-32's own octaves | 12 | 12 | `10-SC15-SKIDVICT-MT32-octaves.mp3` |
| what ships               | 0  | 12 | `08-SC15-SKIDVICT-shipped.mp3` |
| all four dropped         | 0  | 0  | `09-SC15-SKIDVICT-horns-down.mp3` |

The faithful build is unusable: a sampled bass cannot carry that register.
Dropping all four costs the top of the harmony and the arrangement closes up.
What ships is the asymmetric one, the bass an octave below the MT-32 and the
horns where the MT-32 has them, which is two different answers to the same
question, one per part.

At MIDI 99 an SC-55 does stretch a French Horn about two octaves past where one
stops, and it does arrive thin. That was the argument for dropping the horns,
and it lost to the notes underneath it.
`12-SC15-horn-solo-shipped.mp3` against `13-SC15-horn-solo-down.mp3` is that
trade with nothing else playing.


## 8. The recordings

In `audio/`, all captured through the same emulator at the same settings.

The first six are 45 seconds of the title theme, and they chart section 5:

| file | what |
|------|------|
| `01-MT32-reference.mp3`     | the MT-32, the benchmark |
| `02-SC15-bass-OLD.mp3`      | before any of this |
| `03-SC15-bass-NEW.mp3`      | bass octave dropped |
| `04-SC15-bass-FULL.mp3`     | bass released from the volume cap |
| `05-SC15-horn-clear.mp3`    | the reconstructed guitar part lowered |
| `06-SC15-MT32-balance.mp3`  | every level back to the composer's own |

Play 1, then 2, then 6.

The rest are section 7, and they are the eval theme rather than the title theme,
because that is the song the horns are audible in. Four full mixes, 45 seconds:

| file | what |
|------|------|
| `07-MT32-SKIDVICT.mp3`               | the MT-32, again the benchmark |
| `08-SC15-SKIDVICT-shipped.mp3`       | what ships, bass at 0 and horns at 12 |
| `09-SC15-SKIDVICT-horns-down.mp3`    | all four dropped, the consistent build |
| `10-SC15-SKIDVICT-MT32-octaves.mp3` | all four at 12, the faithful build |

Play 8, then 9, then 10.

Then the isolations. Every other track was ended in the song data, so what is
left is one part and silence:

| file | what |
|------|------|
| `11-MT32-horn-solo.mp3`          | the MT-32's horn, at A#4 |
| `12-SC15-horn-solo-shipped.mp3`  | the same notes on the SC-55, also A#4 |
| `13-SC15-horn-solo-down.mp3`     | an octave down, at A#3 |
| `14-MT32-bass-solo.mp3`          | the MT-32's bass, at A#2 |
| `15-SC15-bass-solo-up.mp3`       | the SC-55 at the MT-32's octave, A#2 |
| `16-SC15-bass-solo-shipped.mp3`  | what ships, an octave down at A#1 |

11 against 12 is section 7's measurement made audible, the same octave. 11
against 13 is the octave the horn would have moved to. 14 against 15 and 16 is
the same comparison for the bass, and the one that retired the old explanation
for the bass fix.

Last, the title theme's harmony alone, 34 seconds. It is the other song the
change would have touched:

| file | what |
|------|------|
| `17-SC15-titl-harmony-shipped.mp3` | 52 notes at MIDI 64..80 |
| `18-SC15-titl-harmony-down.mp3`    | the same 52 notes at 52..68 |

The title theme never reaches MIDI 99, so neither is stretched thin and the
choice is only which register the harmony sits in. 17 keeps its top; 18 puts it
into the same register as the reconstructed Strat part of section 3.2.
