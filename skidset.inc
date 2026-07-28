;============================================================================
; skidset driver block.  skidset scans every *.DRV in the game directory for
; the magic and builds its setup menu out of what it finds, so this driver
; describes itself instead of having to be known to skidset in advance, and
; its row appears only when this file is actually present.  Format:
; DRVBLOCK.md in the skidset repository.
;
; Its own source, and included by sc15.asm only when NO_SKIDSET is undefined,
; so a driver can be built without it:
;
;     make driver NOSKIDSET=1
;
; That build is byte identical to the driver as it was before the block
; existed, which is the point of keeping the two separable: the block is the
; only thing that changes, and it changes nothing else.
;
; Data, and never executed.  It is included after the last instruction of the
; last slot, which ends in retf, so nothing can fall through into it, and no
; entry in the jump table reaches it.
;
; Values are 20h to 7Eh, which refuses tab and DEL along with the rest of the
; control codes; comments are exempt.  The /ssc switch is deliberately absent:
; LOAD.EXE derives the driver filename from the switch, so skidset derives the
; switch from the filename.
;
; A line of the block is a line of the bytes emitted, not a line of this file,
; and both newlines around the terminator are mandatory.
;
; The LF before SKIDSETEND is the only thing that ends the help value, and
; without it the value swallows the terminator, the block has no SKIDSETEND,
; and skidset passes the driver over with nothing on screen naming it.  That is
; the failure to watch for here, because it is a comma:
;
;     db  'help ...', 'SKIDSETEND', LF      ; refused, one line
;     db  'help ...', LF, 'SKIDSETEND', LF  ; correct
;
; The LF after SKIDSETEND is required too.  skidset reads the block into a fixed
; buffer, so it cannot tell the end of the file from a NUL in the binary, nor
; from a terminator landing on the last byte it read; it asks for the byte
; rather than guess.
;
; Neither token may appear anywhere else in the block, in a comment no more than
; in a value, and a driver carries one block and only one.
;
; Which db carries a byte does not matter.  The help value below is split
; across two directives for the column limit and reads as one line, because
; neither of them puts an LF inside it.
;============================================================================
LF              equ     0Ah

skidset_block:
    db  'SKIDSETDRV01', LF
    db  '; SC15.DRV - Roland Sound Canvas driver for Stunts 1.1', LF
    db  '; Copyright (c) 2026 Vinicius Ferrao. MIT licence.', LF
    db  '; https://github.com/viniciusferrao/skidsc55', LF
    db  'sound', LF
    ; Named for the family rather than one model, because nothing here is
    ; specific to an SC-55: the effects come out of the GS variation banks,
    ; which every Sound Canvas up to the SC-88 Pro carries.
    ;
    ; The wording follows SETUP.EXE's own menu, whose rows read "Roland MT-32",
    ; "Sound Blaster card" and "Internal PC speaker", with the short forms
    ; "(MT-32)", "(Sound Blaster)" and "(PC speaker)". Manufacturer and model in
    ; the label, model alone in the brief, and "card" only where it is one.
    ; skidset adds the brackets.
    db  'label Roland Sound Canvas', LF
    db  'brief Sound Canvas', LF
    ; "or compatible" is the idiom the period used for exactly this, as in "IBM
    ; PC or compatible", and it carries both halves in one word: later Roland
    ; models, and GS modules from anyone else. Naming GS as well would be
    ; saying the same thing twice, since SC-55 compatibility is what GS means.
    ; One help key carrying the whole paragraph. It used to repeat and the
    ; values were joined with a space; two keys is a duplicate now. A line may
    ; be 448 characters and this is 99, so there is room if the wording moves.
    db  'help Select if you have a Roland Sound Canvas on the MPU-401 port. '
    db  'Requires an SC-55 or compatible.', LF
    db  'SKIDSETEND', LF
SKIDSET_BLOCK_LEN equ   $ - skidset_block
