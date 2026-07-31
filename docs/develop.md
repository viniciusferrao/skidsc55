# Silent music on the evaluation screen

A defect in the game, not in this driver. It is here because anyone running
`SC15.DRV` will eventually meet it and reasonably blame the new driver, and
because the trace below appears to be the first public diagnosis.

The results screen after a race sometimes has no music, and once it happens the
music stays gone until something else loads a song successfully. It is reported
on the stock MT-32 driver in a 1990 4D Sports Driving
release, on vanilla DOSBox with Munt, in
[a 2021 post](https://forum.stunts.hu/index.php?topic=2757.30) on the Stunts
forum, where replacing `MT15.DRV` with the Stunts 1.0 one did not help. No
public diagnosis followed.

It reproduces in about a minute:

1. Race Skid Vicious. Crash on the start banner, without driving.
2. The results screen plays.
3. Race opponent 1, 2 or 3. Crash on the banner again.
4. The results screen is silent.

Crashing rather than finishing means there is no finish time, so
`EndOfGameScores` takes the same branch both times and both screens ask for
`skidover`. The same song plays once and then does not, which puts the fault in
the sequence rather than in the song or the result.

Reading restunts gives a mechanism. `audiodriver_timer` in seg028 is the music
tick, and its first guard is `word_4063A`: non-zero and the tick returns having
done nothing. Nine functions in seg027 raise that flag around a critical section
and lower it afterwards. Eight are balanced on every path. `load_audio_finalize`
is not:

```asm
    mov  word_4063A, 1          ; music tick off
    call sub_3736A
    or   ax, word ptr [bp+arg_Mnote+2]
    jz   short loc_3720F        ; null resource   -> return, flag still raised
    cmp  byte ptr es:[bx+4], 0
    jnz  short loc_3720F        ; header mismatch -> return, flag still raised
    cmp  byte ptr es:[bx+5], 1
    jnz  short loc_3720F        ; header mismatch -> return, flag still raised
    ...
    mov  word_4063A, 0          ; lowered only here
loc_3720F:
    retf
```

`init_audio_resources` returns null when either of its two chunk lookups fails,
and `file_load_audiores` passes that straight in without checking. So a resource
that does not load leaves the music engine switched off, and only a later
successful load switches it back on. That matches the symptom, including its
coming back later and its being intermittent.

Capturing the MPU-401 stream through the reproduction above settles what the
driver is asked to do. Staging records it with `raw_midi_output = true` and the
`caprawmidi` binding, Ctrl+Alt+F6 by default, and the capture happens before the
synthesiser, so the bytes are the same whichever one is selected:

```
  sec | notes  cc  bend | program changes
   11 |     2  74    22 | ch2:p125 ch3:p125          race 1
   17 |     4 148     2 | ch10:p0 ch3:p39 ch7:p52 ch6:p62   results, skidover
   18 |    18   0     0 |
   ...                                                the song, 8 seconds of it
   26 |    17 102     0 | ch10:p0 ch3:p39 ch9:p27 ch6:p62   menu, skidslct
   32 |     2  70    20 | ch2:p125 ch3:p125          race 2
   34 |     1  12    40 | ch4:p125                   the crash
   35 |     0   3    26 |
   36 |     0   6    22 |                            results, and nothing
   37 |     0   9    16 |
```

The first results screen gets its program changes and its song. The second gets
neither: no program change, no note, nothing. Every program change after the
second race is program 125, the effects capital tone.

Pitch bend and controllers keep flowing to the end of the capture, so the
MPU-401 and the driver are alive throughout and still being called for effects.
Music is the only thing that stopped, which is what a raised flag would look
like. On a silent results screen this driver is never asked to play a note.

The four songs are not the variable. Every one carries `hdr1[4] = 0` and
`hdr1[5] = 0`, so all four take the success path on a clean load, and the voice
bank is mapped after the checks rather than before, which puts `mkvce` outside
the story. What remains is the null path, and the resource load failing under
memory pressure right after a race is the obvious candidate. That part is not
proven here.

A second capture, held on the silent screen and then taken back to the menu,
bounds it:

```
  sec | notes  cc  bend  sysex | program changes
   58 |     1  19    50     0 | ch4:p125                   the crash
   62 |     0  82     4     0 |                            the engine winding down
   63 |     0   0     0     0 |   --- silent
   ..                                                      27 seconds of it
   89 |     0   0     0     0 |   --- silent
   90 |     0   0     0    43 |
   91 |    13  86     0     8 | ch10:p0 ch3:p39 ch9:p27 ch6:p62   menu, skidslct
```

Twenty-seven seconds without a single byte, so it is not latency or a slow load.
Then the next screen recovers on its own, with its full complement of program
changes and a song. Nothing persists and nothing needs restarting: one load
fails, and only that screen is affected.

Which means the capture cannot tell a flag left raised from a sequence that was
never there. Both produce this exactly, because a raised flag does not block the
next load, only the tick, and the next load lowers it again. Separating them
needs the flag itself read at run time. `dosbox_with_debugger.exe` ships beside
the normal binary, though `LOAD.EXE` assembles the executable into conventional
memory at every launch, so `dseg` has no fixed base: `audiodriver_timer` has to
be found by its prologue first, and the `cmp` operand read back to recover the
offset.

Nothing in a driver can help either way. The engine stops calling the driver, so
`SC15.DRV` cannot see this, work around it, or report it.
