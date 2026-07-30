# Block parity against skidset

`sc15 block` exists to tell you, before a driver ships, whether `skidset` will
draw a row for it. That promise is only as good as the two readers agreeing, and
they are separate implementations in separate repositories, so agreement has to
be measured rather than intended.

This is the harness that measures it. `refjudge.c` links `skidset`'s own
`src/drvblk.c` unmodified and feeds it a block the way `src/drvscan.c` does:
seek to the magic, read `DRV_BLK_MAX` bytes, NUL terminate, parse. `corpus.sh`
writes a set of driver-shaped files, one per rule worth arguing about, and
`diff.sh` runs both readers over them and prints every case where the verdicts
differ.

It needs a `skidset` checkout, which this repository does not vendor:

```
make parity SKIDSET_DIR=../skidset
```

Nothing here is built or run by CI. A runner has no `skidset` to check against,
and pinning a copy of somebody else's parser would defeat the point: the value
is in testing against whatever `skidset` actually is today.

## Why it exists

Three defects were found this way and none of them by reading the specification,
which all three readings had agreed with:

- a NUL inside a comment. Comments are exempt from the printable rule, so this
  looked legal. `skidset` walks the block as a C string, so the NUL is the end
  of the text and the terminator behind it is invisible: it refuses the block
  for having no `SKIDSETEND`.
- help whose words are separated by runs of spaces. `sc15 block` collapsed a run
  to one column and `skidset` charges for every one, so a paragraph reported as
  two rows needed sixteen and overflowed a fifteen-row window.
- `SKIDSETDRV01` followed by a bare CR and more text. A CR is dropped only when
  an LF follows, so that first line is not the magic, and a search that accepted
  either delimiter and a parse that trusted the search took it for a block.

Each was accepted here and refused there, which is the direction that costs a
driver its row with nothing on screen saying why.

## Adding a case

Add a line to `corpus.sh`. A case needs no expected answer: the point is not
what either reader says, it is that they say the same thing. If a new case
disagrees, one of the two is wrong and which one is a question for
`DRVBLOCK.md`.
