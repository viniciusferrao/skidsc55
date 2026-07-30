#!/bin/sh
# Build the parity corpus. Most files carry one candidate block; the ones under
# "whole file" carry several, because the one-block rule and the choice of
# which candidate is the block are scanner behaviour rather than parser
# behaviour, and were not covered while every case held one.
#
# Every case is written to MANIFEST as well as to disk. diff.sh reads the
# manifest rather than globbing, so a corpus that half generated cannot quietly
# shrink into a run that passes on the cases that survived.
set -eu

d="$1"
rm -rf "$d"
mkdir -p "$d"
: >"$d/MANIFEST"

put() {
    name="$1"
    shift
    printf '%b' "$*" >"$d/$name.drv"
    echo "$name" >>"$d/MANIFEST"
}

HEAD='SKIDSETDRV01\n'
TAIL='SKIDSETEND\n'
BODY='sound\nlabel A\nbrief X\n'

put minimal "$HEAD$BODY$TAIL"
put comment-ok "${HEAD}; a plain comment\n$BODY$TAIL"
put help-single-spaces "$HEAD${BODY}help One two three.\n$TAIL"
put crlf-endings \
    'SKIDSETDRV01\r\nsound\r\nlabel A\r\nbrief X\r\nSKIDSETEND\r\n'
put tab-indented "$HEAD\tsound\n\tlabel A\n\tbrief X\n$TAIL"

# The three disputed single-block cases.
put nul-in-comment "${HEAD}; a\000b\n$BODY$TAIL"
put nul-in-value "${HEAD}sound\nlabel A\000B\nbrief X\n$TAIL"
put magic-cr-junk \
    'SKIDSETDRV01\rX\nsound\nlabel A\nbrief X\nSKIDSETEND\n'

# help of sixteen one-character words separated by twenty-five spaces each.
# 391 characters, under the 448 line limit, every word one character, so no
# other rule can mask a wrapping difference.
sp='                         '
h="a"
i=1
while [ $i -lt 16 ]; do
    h="$h$sp""a"
    i=$((i + 1))
done
put help-wide-spaces "$HEAD${BODY}help $h\n$TAIL"

# Everything the checker already claims to refuse.
put terminator-indented "$HEAD$BODY  SKIDSETEND\n"
put terminator-in-comment "${HEAD}; ends with SKIDSETEND\n$BODY$TAIL"
put terminator-in-value "${HEAD}sound\nlabel A SKIDSETEND\nbrief X\n$TAIL"
put magic-twice "${HEAD}sound\nSKIDSETDRV01\nlabel A\nbrief X\n$TAIL"
put cr-in-comment "${HEAD}; one\rtwo\n$BODY$TAIL"
put cr-in-value "${HEAD}sound\nlabel A\rB\nbrief X\n$TAIL"
put value-on-sound "${HEAD}sound yes\nlabel A\nbrief X\n$TAIL"
put no-newline-after-end "$HEAD${BODY}SKIDSETEND"
put no-newline-before-end \
    "${HEAD}sound\nlabel A\nbrief X\nhelp One.SKIDSETEND\n"
put no-terminator "$HEAD$BODY"
put neither-sound-nor-video "${HEAD}label A\nbrief X\n$TAIL"
put both-sound-and-video \
    "${HEAD}sound\nvideo\nlabel A\nbrief X\nmode S\n$TAIL"
put no-label "${HEAD}sound\nbrief X\n$TAIL"
put no-brief "${HEAD}sound\nlabel A\n$TAIL"
put label-32 \
    "${HEAD}sound\nlabel 01234567890123456789012345678901\nbrief X\n$TAIL"
put brief-22 \
    "${HEAD}sound\nlabel A\nbrief 0123456789012345678901\n$TAIL"
put label-twice "${HEAD}sound\nlabel A\nlabel B\nbrief X\n$TAIL"
put help-twice "$HEAD${BODY}help One.\nhelp Two.\n$TAIL"
put video-no-mode "${HEAD}video\nlabel A\nbrief X\n$TAIL"
put sound-with-mode "$HEAD${BODY}mode SVGA\n$TAIL"
put video-ok "${HEAD}video\nlabel A\nbrief X\nmode SVGA\n$TAIL"
put mode-nine-chars "${HEAD}video\nlabel A\nbrief X\nmode 123456789\n$TAIL"
put mode-backslash "${HEAD}video\nlabel A\nbrief X\nmode A\\\\B\n$TAIL"
put tab-in-value "${HEAD}sound\nlabel A\tB\nbrief X\n$TAIL"
put unknown-key "$HEAD${BODY}colour red\n$TAIL"
put misspelt-label "${HEAD}sound\nlabe1 A\nbrief X\n$TAIL"
put brief-with-bracket "${HEAD}sound\nlabel A\nbrief (X)\n$TAIL"
put unwrappable-word "$HEAD${BODY}help 012345678901234567890123456\n$TAIL"
put word-exactly-26 "$HEAD${BODY}help 01234567890123456789012345\n$TAIL"
put no-magic 'nothing here at all\n'

# --- whole file: how many blocks, and which candidate is the block ---------
#
# A driver carries one block and only one, because its switch is its filename
# and a second could only claim the same switch. What counts as a block is
# drv_blk_span's question rather than a search for the terminator's bytes, and
# these are the cases where the two answers differ.
put two-blocks-lf \
    "$HEAD$BODY$TAIL${HEAD}sound\nlabel B\nbrief Y\n$TAIL"

# The second terminated CRLF. A raw search for LF, SKIDSETEND, LF finds a CR in
# the byte it wanted to be an LF, and misses the block entirely.
put second-block-crlf \
    "$HEAD$BODY$TAIL"'SKIDSETDRV01\r\nsound\r\nlabel B\r\nSKIDSETEND\r\n'

# The second terminated at the end of the image with no newline at all. It
# still has a span, so it still counts, even though parsing it would refuse it
# for the missing byte.
put second-block-eof-no-lf \
    "$HEAD$BODY$TAIL${HEAD}sound\nlabel B\nbrief Y\nSKIDSETEND"

# A NUL before what looks like a second terminator. The window is a C string,
# so the terminator behind the NUL is not there and the candidate has no span.
put second-block-past-nul \
    "$HEAD$BODY$TAIL${HEAD}sound\nlabel B\n\000brief Y\n$TAIL"

# Twelve bytes that happened to line up, ahead of the real block. Passed over
# without a word, and the block behind it is still found.
put false-magic-then-block "codeSKIDSETDRV01code\n$HEAD$BODY$TAIL"

# A candidate with the magic on a line of its own and nothing terminating it,
# ahead of the real block. Named on screen, but it does not deny the block its
# row and it is not a second block.
put candidate-then-block \
    "${HEAD}sound\nno terminator here\n$HEAD$BODY$TAIL"
