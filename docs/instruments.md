# SC15 instrument mapping

How the General MIDI voice banks were derived, and why each program number was
chosen. The banks themselves are generated from the text specifications in
`voices/` by `sc15 mkvce`.


## 1. How Stunts identifies an instrument

An instrument is a four character ASCII resource name, nothing more.

`audio_map_song_instruments()` in seg027 reads the instrument name table out of
the song's `hdr1` chunk, looks each name up in the voice bank that was loaded
for the current driver, and overwrites the name in place with a far pointer to
the record it found. A name that is not in the bank is replaced with a null
pointer and that part of the song is silent.

So the driver never receives a name, an index or an enumeration. It receives a
pointer to a 93 byte record from the bank, and reads a program number, a volume,
a pan, a pitch bend range and a MIDI channel out of it. Everything about how an
instrument sounds is data, and the data is per driver: `MTSKIDMS.VCE` for the
MT-32, `ADSKIDMS.VCE` for AdLib, and now `SCSKIDMS.VCE` for General MIDI.

Two banks are needed, because the resource prefix comes from the driver file
name:

- `SCSKIDMS.VCE`, the music voices for SKIDTITL, SKIDSLCT, SKIDVICT, SKIDOVER.
- `SCENG1.VCE`, the sound effect voices.

Without them the game falls back to `GE`-prefixed and unprefixed names, none of
which exist, and voice loading fails.


## 2. What the MT-32 numbers actually mean

The program numbers in `MTSKIDMS.VCE` are not MT-32 timbre numbers directly.
A program change on an MT-32 selects one of the 128 entries of its patch memory,
and each entry names a timbre group and number.

Stunts uploads `MT32.PLB` into that memory at startup. Decoding it the way
MT15.DRV slot 42h does shows it contains only five records:

| Patch | Group  | Timbre name  |
|-------|--------|--------------|
| 0     | Memory | Engine 1     |
| 1     | Memory | Squeel       |
| 2     | Memory | Damage       |
| 3     | Memory | MetalClank   |
| 4     | Memory | Engine 1     |

Patches 5 to 127 are left at the factory default, where patch n selects preset
timbre n. So for the music bank, whose programs are all above 4, the MT-32 sound
behind each part is a named factory preset and is therefore known exactly. For
the effects bank, whose programs are mostly 0 to 2, the only description of the
intended sound is the ten character timbre name in the table above.

That gives a clean derivation rule, which is what the tables below apply:

> take the MT-32 sound the composer selected, by name, and choose the nearest
> General MIDI program.


## 3. Music bank, SCSKIDMS.VCE

| Voice | MT-32 program | MT-32 preset | GM program | GM name                  | Why |
|-------|---------------|--------------|------------|--------------------------|-----|
| BASS  | 30            | Syn Bass 3   | 39         | Synth Bass 2             | chosen by measurement, not by name; see 2.1 |
| HRN1  | 92            | Fr Horn 1    | 60         | French Horn              | exact name match |
| HRN2  | 92            | Fr Horn 1    | 60         | French Horn              | same source preset |
| HRN9  | 92            | Fr Horn 1    | 60         | French Horn              | same source preset |
| LEAD  | 24            | Syn Brass 1  | 62         | Synth Brass 1            | exact name match |
| GUIT  | 62            | Elec Gtr 2   | 27         | Electric Guitar (clean)  | MT-32 Elec Gtr 2 is the clean, chorused electric of the pair |
| VOXX  | 34            | Chorale      | 52         | Choir Aahs               | Chorale is the MT-32 choir pad |
| KEYS  | 34            | Chorale      | 52         | Choir Aahs               | same source preset, and it shares MIDI channel 6 with VOXX, so it has to agree |
| SNTH  | 5             | Elec Piano 3 | 5          | Electric Piano 2         | Elec Piano 3 is the FM flavoured electric piano, which is what GM 5 is |
| HRM1  | 0             | Acou Piano 1 | 0          | Acoustic Grand Piano     | exact name match |
| DRUM  | 0             | (percussion) | 0          | Standard Kit             | on the percussion channel a program selects a kit, and the standard kit is what the note map below assumes |
| STRT  | (absent)      | (absent)     | 27         | Electric Guitar (clean)  | reconstruction, see 3.2 |

Channel assignments, volumes, pans, pitch bend ranges, note transposes,
envelopes and LFO settings are all copied unchanged from `MTSKIDMS.VCE`, with
two exceptions noted below. `mkvce` copies the whole 93 byte record and
overwrites only the fields the driver reads.

### 3.0 The bass, and why it was chosen by measurement

Every other row in the table above was derived from the MT-32 preset's name.
That method picked Synth Bass 1 for BASS, and it was wrong twice over. Both
faults were found by recording the same 45 seconds from an emulated MT-32 and
from SC15, and comparing octave band energy; neither was audible to the author
as anything more specific than "the menu music sounds a bit off".

The octave. MTSKIDMS.VCE transposes this part up 12, which puts the written
notes at MIDI 40..61, fundamentals 82 to 277 Hz. That is a baritone register.
`transpose=0` cancels it and puts the line at 41 to 139 Hz, which sounds far
better on a sampler and is what this bank ships.

The reason recorded here used to be that an LA timbre carries its own coarse
tuning, so Syn Bass 3 sounds an octave below the note it is handed while a
sampled program does not, and cancelling the transpose therefore restored the
composer's pitch. That is wrong. With every other track ended in the song data,
the MT-32 plays the bass at A#2, 116 Hz, where the SC-55 with the transpose
cancelled plays A#1, 58 Hz, and the MT-32 sits 18.2 dB below it across the bands
holding those lowest notes. Syn Bass 3 sounds the note it is given.

So `transpose=0` puts the bass an octave below the MT-32 rather than onto it,
and is kept because it sounds better, not because it is faithful.
`audio/14-MT32-bass-solo.mp3`, `audio/15-SC15-bass-solo-up.mp3` and
`audio/16-SC15-bass-solo-shipped.mp3`: 14 and 15 are the same octave, 16 is what
this bank plays.

The program. At 41 to 139 Hz, Synth Bass 2 measured closest to the MT-32; an
octave up it had measured *worse* than Synth Bass 1, which is why the first
program sweep looked like a dead end. That comparison was made at the octave the
paragraph above shows is not the MT-32's, so it says which program suits the
pitch this bank plays rather than which is nearest the MT-32. It has not been
rerun.

Two null results did most of the work. A sweep of six SC-55 drum kits moved the
spectrum by half a decibel, which cleared the percussion; a sweep of six bass
programs was short at 63 and 125 Hz in every case, which cleared the timbre and
left only the pitch. Both would have been invisible by ear.

### 3.0.1 The horns, where the same question gets the opposite answer

HRN1, HRN2 and HRN9 carry the same transpose of 12 and keep it, so the bank
cancels the transpose on one of the four instruments carrying it and leaves it
on three.

The MT-32 does sound them where the bank asks. On one sustained note of SKIDVICT
the SC-55 plays A#3, 233 Hz, without the transpose and A#4, 466 Hz, with it; the
MT-32 plays A#4, 466 Hz and sits 32.6 dB below the untransposed version across
the three third octave bands holding its lowest notes. Fr Horn 1 carries no
coarse tuning. story.md section 7 has the method, including why two full mixes
cannot answer this and how the horn was isolated.

MIDI 99 is about two octaves above where a French Horn stops, and an SC-55 gets
there by stretching a recording, which arrives thin. That is the argument for
dropping the horns the way the bass was dropped, and it was built and rejected:
dropping them spends the top of the harmony across the whole arrangement to save
the handful of notes that stretch. Raising the bass costs more again. So the
SC-55 wants the bass an octave below the MT-32 and the horns where the MT-32 has
them, which is neither the faithful build nor the consistent one.

`audio/08-SC15-SKIDVICT-shipped.mp3`, `audio/09-SC15-SKIDVICT-horns-down.mp3` and
`audio/10-SC15-SKIDVICT-MT32-octaves.mp3` are the three builds.
`audio/12-SC15-horn-solo-shipped.mp3` against
`audio/13-SC15-horn-solo-down.mp3` is the same trade with nothing else playing,
and `audio/17-SC15-titl-harmony-shipped.mp3` against
`audio/18-SC15-titl-harmony-down.mp3` is the title theme, the only other part
the change would have reached. SKIDSLCT and SKIDOVER name no horn.

To build the consistent variant, add `transpose=0` to the three HRN lines in
`voices/scskidms.txt` and rebuild. 3.2's Strat level wants revisiting with it,
because the harmony comes down into that part's register.

### 3.1 Percussion needs no remapping

The DRUM part is pinned to MIDI channel 9, which is channel 10 counting from
one, on both the MT-32 and General MIDI. Its record carries a note transpose of
+12, applied by the engine before the note reaches the driver.

Decoding every drum track of every song gives this complete set of note numbers:

| Song      | Track            | Notes written | Notes sent (+12) | GM percussion            |
|-----------|------------------|---------------|------------------|--------------------------|
| SKIDTITL  | Kick/Snare/Toms  | 24 26 29 33 36| 36 38 41 45 48   | Bass Drum 1, Acoustic Snare, Low Floor Tom, Low Tom, Hi-Mid Tom |
| SKIDTITL  | Hats             | 30 32 37      | 42 44 49         | Closed Hi Hat, Pedal Hi-Hat, Crash Cymbal 1 |
| SKIDSLCT  | HATS             | 30 32         | 42 44            | Closed Hi Hat, Pedal Hi-Hat |
| SKIDSLCT  | KICK/SNARE       | 24 26         | 36 38            | Bass Drum 1, Acoustic Snare |
| SKIDSLCT  | CYMBALS          | 37 39         | 49 51            | Crash Cymbal 1, Ride Cymbal 1 |
| SKIDVICT  | Hats/Crash       | 30 32 37      | 42 44 49         | Closed Hi Hat, Pedal Hi-Hat, Crash Cymbal 1 |
| SKIDVICT  | Kick/Snare/Toms  | 24 26 29 31 33 36 | 36 38 41 43 45 48 | Bass Drum 1, Acoustic Snare, Low Floor Tom, High Floor Tom, Low Tom, Hi-Mid Tom |
| SKIDOVER  | Kick/Snare       | 24 26         | 36 38            | Bass Drum 1, Acoustic Snare |
| SKIDOVER  | Hats             | 30 32         | 42 44            | Closed Hi Hat, Pedal Hi-Hat |
| SKIDOVER  | Cymbals          | 37            | 49               | Crash Cymbal 1 |

Every one of them lands on the drum General MIDI names it, and every track name
the composer left in the song agrees. That is not luck: the General Standard
percussion map was derived from the MT-32 rhythm part, and the two agree over
exactly this range. No transpose change and no per-note remapping is needed.

The one change to the DRUM record is its pan, which the MT-32 bank leaves at 0.
A General MIDI device obeys CC 10 on channel 10 and would pan the whole kit hard
to one side, so it is moved to centre, 64.

### 3.2 STRT, a part the MT-32 never played

Track 5 of SKIDTITL is named "Strat" and asks for an instrument called STRT.
`MTSKIDMS.VCE` does not contain STRT, so the lookup returns null and the track
is silent on an MT-32. `ADSKIDMS.VCE` and `TDSKIDMS.VCE` both contain it, so
AdLib and Tandy players do hear the part.

`SCSKIDMS.VCE` reconstructs it from the GUIT record, keeps GUIT's clean electric
guitar program, and pins it to MIDI channel 8, which SKIDTITL does not otherwise
use. This is the one place where the General MIDI build plays something the
MT-32 build does not. Deleting the `STRT` line from
`voices/scskidms.txt` and rebuilding restores the MT-32 behaviour
exactly.

### 3.3 Voices no song uses

HRM1, HRN9, VOXX and SNTH are in `MTSKIDMS.VCE` but no shipped song references
them. They are mapped anyway, at no cost, so that a song added later behaves.


## 4. Effects bank, SCENG1.VCE

This is the weakest part of the mapping, and it is weak for a structural reason
rather than through lack of care: General MIDI has no engine, no tyre squeal and
no crash, and the MT-32 originals are not presets that can be looked up by name.

The first attempt mapped them by name alone, to sawtooth and square leads. That
was wrong, and audibly so: the engine note is a single held note swept across a
24 semitone bend range, and any clean melodic program under that gesture is a
siren. Decoding the actual timbre parameters out of MT32.PLB, rather than just
the names, shows what the property to preserve really was.

| Timbre | Structure | Partials |
|--------|-----------|----------|
| Engine 1 | `P1>S2 ring` and `S1>P2 ring` | four sawtooth partials pitched 12 to 36 semitones down, PCM waves 43 and 53, pitch bender enabled |
| Squeel | `S1+S2` and `P1+P2` | two square partials at pulse width 100 and 88, which is a very thin nasal pulse, plus two PCM partials |
| Damage | `S1>P2 ring` twice | four partials, PCM waves 18, 19, 24 and 83, pitch bender disabled |
| MetalClank | `P1+P2` and `P1>P2 ring` | two PCM partials, 69 and 88 |

Every one is built from ring modulated pairs and ROM PCM samples. Ring
modulation produces inharmonic partials, and the PCM waves contribute noise. So
the property to match is noise content, not pitch, and the General MIDI effects
group is where that lives.

### 4.1 General MIDI has no car sounds, GS does

Bank 0 contains Helicopter, Applause and Gun Shot and nothing else remotely
vehicular, on any device. Weeks of picking a better program out of bank 0 would
have been wasted; the sounds simply are not there.

The Roland GS extension, which is what the Sound Canvas line added on top of
General MIDI in 1991, does have them, as variation banks selected with Bank
Select MSB, CC 0, under the SFX capital tones. The complete set:

| Capital tone | Variations |
|---|---|
| 120 Gt.FretNoise | 1 Gt.Cut Noise, 2 String Slap |
| 121 Breath Noise | 1 Fl.Key Click |
| 122 Seashore | 1 Rain, 2 Thunder, 3 Wind, 4 Stream, 5 Bubble |
| 123 Bird | 1 Dog, 2 Horse-Gallop, 3 Bird 2 |
| 124 Telephone 1 | 1 Telephone 2, 2 DoorCreaking, 3 Door, 4 Scratch, 5 Wind Chimes |
| 125 Helicopter | 1 Car-Engine, 2 Car-Stop, 3 Car-Pass, 4 Car-Crash, 5 Siren, 6 Train, 7 Jetplane, 8 Starship, 9 Burst Noise |
| 126 Applause | 1 Laughing, 2 Screaming, 3 Punch, 4 Heart Beat, 5 Footsteps |
| 127 Gun Shot | 1 Machine Gun, 2 Lasergun, 3 Explosion |

That table was read out of `C:\Windows\System32\drivers\gm.dls`, the Roland
licensed sound set behind the Microsoft GS Wavetable Synth, and cross checked
against the preset table of `SGM-V2.01.sf2`. The two agree on every variation
they both contain, so the numbering is not a guess.

Note that capital tone 120 stops at variation 2. Pick Scrape, which some
SoundFonts place at 120 bank 6, is an SC-88 addition and an SC-55 falls back to
the capital tone if asked for it.

### 4.2 The mapping

| Voice | MT-32 program | Source timbre | Program | Bank | SC-55 sound | Why |
|-------|---------------|---------------|---------|------|-------------|-----|
| ENGI  | 0             | Engine 1      | 125     | 1    | Car-Engine  | the sound Roland built for exactly this, and the nearest thing in existence to a bespoke engine timbre |
| STAR  | 0             | Engine 1      | 125     | 1    | Car-Engine  | same source timbre, the starter is the same engine cranking |
| STOP  | 0             | Engine 1      | 125     | 2    | Car-Stop    | the car coming to a halt |
| SKID  | 1             | Squeel        | 125     | 2    | Car-Stop    | Car-Stop is a tyre screech, which is what Squeel is |
| SKI2  | 1             | Squeel        | 125     | 2    | Car-Stop    | same source timbre, pitched a fifth up to distinguish it |
| CRAS  | 2             | Damage        | 125     | 4    | Car-Crash   | exact match |
| BLOW  | 2             | Damage        | 127     | 3    | Explosion   | a tyre bursting |
| SCRA  | 2             | Damage        | 124     | 4    | Scratch     | the closest the SC-55 has to metal dragging along a wall |
| BUMP  | 115           | Elec Perc 1   | 118     | 0    | Synth Drum  | the faithful derivation from the MT-32 timbre. 126 bank 3, Punch, is worth trying instead |

CRAS, BLOW and SCRA share one MT-32 timbre because the MT-32 build only had five
custom timbres to spend. GS has a dedicated effects family, so the three are
separated by what the game event actually is.

The driver only emits CC 0 when its GS flag is set, which it is by default. A
device that does not know GS ignores the bank select or falls back to the
capital tone, so the driver stays usable on plain General MIDI; the effects just
will not be right, because there is nothing right for them to be.

The candidates were chosen by auditioning them from a generated Standard MIDI
File that reproduced the real gesture, bank select included, since a program
judged on a held note tells you nothing about how it behaves under a pitch
sweep. The throwaway script that wrote those files is not part of this
repository.

The later work was measured rather than auditioned, and that turned out to be
the better instrument by some distance. See story.md.

### 4.3 The engine note, and the two parameters that had to change

The engine is unlike every other effect. The other eight are ordinary tracks in
`GEENG.SFX` that play MIDI note 60, to which the instrument transpose is added:
CRAS lands on 45, BLOW on 48, SCRA on 70, SKID on 88.

The engine has no track at all. `sub_39050` builds an event in memory with the
note byte set to `0FFh`, which `sub_38DE6` sign extends to -1, so the note is
`transpose - 1` and it is held indefinitely with a duration of `0FFFFFFE0h`
while slot 24h drives the pitch. With the MT-32 transpose of 12 that is MIDI
note 11, 49 semitones below the root key of any sampled program.

The MT-32 does not care: `Engine 1` is a synthesised ring modulated stack, and
an oscillator at note 11 is a low buzz, which is what an engine is. A sampled
program at note 11 is played at about one seventeenth speed and turns to mud.
No program choice rescues that, which is why several rounds of auditioning
produced nothing usable until the note itself was corrected.

ENGI therefore overrides two fields:

| Field | MT-32 | SC15 | Reason |
|---|---|---|---|
| `+10h` transpose | 12 | 61 | puts the held note on 60, near the root key of a sampled program |
| `+12h` bend range | 24 | 12 | 24 sweeps the note four octaves, which no sample survives |

The pitch mapping itself is untouched. `audio_op_unk` in seg007 computes
`value = rawRPM / instr[+0Eh] + instr[+0Fh] * 16`, and ENGI keeps the MT-32
divisor of 60 and offset of 0, so slot 24h's `value * 60` tracks the raw rev
counter across the whole 14 bit bender range as it always did. For comparison,
the AdLib bank divides by 11 rather than 60, because AD15.DRV programs an OPL2
oscillator frequency directly instead of sending a bender position.

SKID and SKI2 also drop their MT-32 transposes of +28 and +19, to 0 and +7.
Those values existed to place a synthesised squeal; Car-Stop is a purpose built
tyre screech and wants to be near its own root key.

Everything else about the effects is preserved exactly. In particular every
effect keeps a channel volume of 0, which is the flag that tells the driver not
to send CC 7 at program change time. The effect code writes the running volume
itself through slot 12h, and a CC 7 from the driver would overwrite it.

### 4.4 If the engine sample does not loop

The General MIDI effect programs on a software synthesiser are one shot samples:
they run for about a second and stop, however long the note is held. The engine
is one note held forever with no retrigger, so on such a device it falls silent
a second after the car starts moving.

Whether an SC-55 loops its Car-Engine is a question for the hardware. In case it
does not, the driver accepts a retrigger period at offset `+48h` of the
instrument record, in engine ticks of which there are 100 per second, and
restrikes the note that often from slot 27h. Both banks ship with 0, which
disables it entirely, so it costs nothing unless a device turns out to need it:

```
ENGI  ENGI  125  =  64  12  =  1  transpose=61 retrigger=50
```

Pan is centred on all nine. The MT-32 bank already uses 64 for seven of them and
leaves STAR and STOP at 0, which reads as "do not care" given their volume is
also 0; on a General MIDI device 0 is hard left.


## 5. Volume and pan caveats

Channel volumes are copied from the MT-32 bank except for three, because MT-32
and General MIDI devices do not apply the same curve to CC 7 and do not have
comparable relative levels between a drum kit and a guitar patch. Listening to
the menu themes on an SC-55 SoundFont, the drums buried the guitars:

| Voice | MT-32 | SC15 | Reason |
|-------|-------|------|--------|
| DRUM  | 119   | 100  | a General MIDI standard kit is much hotter than the MT-32 rhythm part at the same CC 7 |
| GUIT  | 102   | 120  | the two guitar parts carry SKIDSLCT and were sitting under the kit |
| STRT  | 102   | 120  | same, for the reconstructed title theme guitar |

Unchanged: BASS 127, VOXX and KEYS 102, SNTH 99, HRN9 97, LEAD 88, HRN1 and
HRN2 78, HRM1 0. Each value is one number in
`voices/scskidms.txt`, and balance is worth revisiting per device.

Pan is also copied unchanged, and here there is a genuine open question. The
MT-32 is widely claimed to treat CC 10 in the opposite sense to General MIDI. If
that is true, the stereo image of the music is mirrored relative to what the
composer heard. The values were left alone because:

- they read naturally as General MIDI values already, and
- the one obviously deliberate pair, HRN1 at 34 and HRN2 at 90, is symmetric
  about centre, so mirroring only swaps which horn is on which side.

Anyone who wants the mirrored image can replace each pan value v with 127 - v in
the specification file. The question is recorded as open in
driver.md section 8.


## 6. Rebuilding a bank

```
sc15 mkvce voices/scskidms.txt build/SCSKIDMS.VCE
```

The specification file names a source bank, then one line per instrument giving
the resource name, the record in the source bank to copy, and the program,
volume, pan, bend range, channel and bank select values. Any numeric field may
be `=` to keep whatever the source record already had. The tool prints what it
changed and what it kept, so a diff of its output is a readable summary of the
mapping.
