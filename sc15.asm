; SC15.DRV - Roland Sound Canvas audio driver for Stunts 1.1 (Broderbund).
;
; Implements the same 23-slot jump-table ABI as the original MT15.DRV, so the
; unmodified game executable, the unmodified KMS songs and the unmodified music
; engine drive it.  Everything MT-32 specific has been replaced by plain
; General MIDI Level 1 messages on an MPU-401 UART interface.
;
; The music is General MIDI and plays correctly on any GM device.  The sound
; effects are not, and cannot be: General MIDI has no engine, no tyre screech
; and no car crash anywhere in its 128 programs.  Those come from the Roland GS
; variation banks, which is why the driver is named after the Sound Canvas and
; not after General MIDI.  On a GM-only device the music is still right and the
; car sounds fall back on their capital tones.
;
; See docs/driver.md for the full ABI documentation and for the
; reverse-engineering notes this file is based on.
;
; Build:  tasm32 /m9 /ml /zn sc15.asm, sc15.obj
;         add /dNO_SKIDSET to leave the skidset block out; see skidset.inc
;         wlink format dos com option quiet name sc15.drv file sc15.obj
;
; The module is loaded raw at offset 0 of its own segment by the game's
; mmgr_alloc_pages(), so all internal data is addressed as cs:[absolute].

    nosmart
    locals

;----------------------------------------------------------------------------
; Instrument record (one chunk of a .VCE voice bank, 93 bytes).
; Only the fields the driver itself reads are named here; the remaining fields
; belong to the music engine.
;----------------------------------------------------------------------------
I_BENDRANGE     equ 12h         ; pitch bend sensitivity in semitones
I_VELSENS       equ 15h         ; 0 = play every note at velocity 127
I_ENVDEST       equ 19h         ; 2 = envelope drives pitch bend, 4 = CC 1
I_LFODEST       equ 28h         ; 2 = LFO drives pitch bend, 4 = CC 1
I_PORTA         equ 35h         ; 1 = re-trigger note when the pitch offset moves
I_PROGRAM       equ 44h         ; MIDI program number
I_VOLUME        equ 45h         ; channel volume, 0 = do not send CC 7
I_PAN           equ 46h         ; channel pan for CC 10
I_BANK          equ 47h         ; SC15 extension: bank select MSB (CC 0)
I_RETRIG        equ 48h         ; SC15 extension: retrigger period in ticks,
                                ; 0 disables it

;----------------------------------------------------------------------------
; Voice record (0A2B6h in the game data segment, stride 2Eh).
;----------------------------------------------------------------------------
V_NOTEBASE      equ 03h         ; note as written in the song
V_VELOCITY      equ 04h         ; velocity used for the note on
V_CURNOTE       equ 06h         ; note currently sounding
V_ENVLEVEL      equ 14h         ; envelope output
V_LFOLEVEL      equ 1Ch         ; LFO output
V_PITCHOFS      equ 22h         ; pitch offset in semitones

;----------------------------------------------------------------------------
MPU_TIMEOUT     equ 8000h       ; status polls before a byte is dropped
IN_SIZE         equ 40h         ; MIDI input ring buffer, must be a power of 2
IN_MASK         equ IN_SIZE-1

MT32_VOLUME_HI  equ 10h         ; MT-32 system area address of MASTER VOLUME,
MT32_VOLUME_MID equ 00h         ; the only DT1 write the game ever performs
MT32_VOLUME_LO  equ 16h

DRV segment byte public 'CODE'
    assume cs:DRV, ds:nothing, es:nothing, ss:nothing
    org 0

;----------------------------------------------------------------------------
; Jump table.  23 slots of exactly three bytes; the game far-calls
; audiodriverbinary+slot*3 and never inspects anything else at the front of
; the module.  The macro is used instead of "jmp near ptr" so that the
; assembler cannot shrink an entry to a two byte short jump.
;----------------------------------------------------------------------------
ENTRY   macro   target
    db      0E9h
    dw      target - ($ + 2)
    endm

    ENTRY   gm_init                 ; 00  int  init(void)
    ENTRY   gm_shutdown             ; 03  void shutdown(void)
    ENTRY   gm_all_notes_off        ; 06  void all_notes_off(void)
    ENTRY   gm_note_on              ; 09  void note_on(ch,voice,chunk,note,vel,instr)
    ENTRY   gm_note_off             ; 0C  void note_off(ch,voice)
    ENTRY   gm_voice_free           ; 0F  void voice_free(ch,voice)
    ENTRY   gm_set_volume           ; 12  void set_volume(ch,-,vol)
    ENTRY   gm_set_controller       ; 15  void set_controller(ch,-,cc,val)
    ENTRY   gm_reset_controllers    ; 18  void reset_controllers(void)
    ENTRY   gm_pitch_bend           ; 1B  void pitch_bend(-,bend,ch)
    ENTRY   gm_channel_off          ; 1E  void channel_off(ch)
    ENTRY   gm_program_change       ; 21  void program_change(ch,-,chunk,instr)
    ENTRY   gm_pitch_set            ; 24  void pitch_set(ch,voice,val)
    ENTRY   gm_voice_update         ; 27  void voice_update(ch,voice,-,instr)
    ENTRY   gm_null                 ; 2A  unused by the engine
    ENTRY   gm_null                 ; 2D  unused by the engine
    ENTRY   gm_refresh              ; 30  void refresh(voice0)
    ENTRY   gm_null                 ; 33  unused by the engine
    ENTRY   gm_query                ; 36  int  query(void)
    ENTRY   gm_raw_midi             ; 39  void raw_midi(count,near ptr)
    ENTRY   gm_midi_in              ; 3C  int  midi_in(void)
    ENTRY   gm_master_volume        ; 3F  void sysex_dt1(count,far ptr)
    ENTRY   gm_load_patchlib        ; 42  void load_patchlib(far ptr)

;----------------------------------------------------------------------------
; Configuration block.  Fixed offsets so that it can be patched in a built
; SC15.DRV without rebuilding the driver.
;----------------------------------------------------------------------------
    org     45h
cfg_signature   db      'SC15CFG', 0    ; 45h  8 bytes, locates the block
cfg_mpu_base    dw      0330h           ; 4Dh  MPU-401 data port, status = +1
cfg_gm_reset    db      1               ; 4Fh  send GM System On at init
; GS is on by default because the reference device is a Roland SC-55 and the
; sound effects need its variation banks: General MIDI has no engine, no tyre
; screech and no car crash anywhere in its 128 programs.  A device that knows
; nothing about GS ignores an unrecognised exclusive, and ignores or falls back
; on a bank select, so the driver stays usable on plain General MIDI either way.
; Turn it off with "sc15 cfg SC15.DRV gs=0" if a device misbehaves.
cfg_gs_reset    db      1               ; 50h  send GS Reset at init (SC-55+)
cfg_master_vol  db      100             ; 51h  initial master volume, 0..100
cfg_reserved    db      0, 0, 0, 0, 0, 0; 52h

banner          db      'SC15.DRV General MIDI driver for Stunts 1.1', 0

;----------------------------------------------------------------------------
; Driver state.
;----------------------------------------------------------------------------
mpu_dead        db      0               ; 1 = no interface found, stay silent
in_head         dw      0
in_tail         dw      0
in_buf          db      IN_SIZE dup (0)
retrig_ctr      db      16 dup (0)      ; per channel countdown, see slot 27h

msg_gm_on       db      0F0h, 07Eh, 07Fh, 009h, 001h, 0F7h
MSG_GM_ON_LEN   equ     $ - msg_gm_on

msg_gs_on       db      0F0h, 041h, 010h, 042h, 012h
                db      040h, 000h, 07Fh, 000h, 041h, 0F7h
MSG_GS_ON_LEN   equ     $ - msg_gs_on

msg_volume      db      0F0h, 07Fh, 07Fh, 004h, 001h
vol_lsb         db      0
vol_msb         db      0
                db      0F7h
MSG_VOLUME_LEN  equ     $ - msg_volume

;============================================================================
; Internal helpers
;============================================================================

;----------------------------------------------------------------------------
; io_delay - burn CX bus cycles.  Reading port 61h costs one ISA cycle
; (about a microsecond) regardless of CPU speed, which is what the original
; drivers use for their inter-SysEx pauses.
;----------------------------------------------------------------------------
io_delay proc near
    push    ax
    jcxz    @@out
@@spin:
    in      al, 61h
    loop    @@spin
@@out:
    pop     ax
    ret
io_delay endp

;----------------------------------------------------------------------------
; midi_in_store - move one waiting byte from the interface into the ring
; buffer.  Called while waiting to transmit so that a synth sending active
; sensing can never block the output path.
;----------------------------------------------------------------------------
midi_in_store proc near
    push    ax
    push    bx
    push    dx
    mov     dx, cs:[cfg_mpu_base]
    in      al, dx
    mov     bx, cs:[in_head]
    mov     cs:in_buf[bx], al
    inc     bx
    and     bx, IN_MASK
    mov     cs:[in_head], bx
    pop     dx
    pop     bx
    pop     ax
    ret
midi_in_store endp

;----------------------------------------------------------------------------
; midi_out - transmit AL.  Preserves every register.
;----------------------------------------------------------------------------
midi_out proc near
    cmp     cs:[mpu_dead], 0
    jne     @@ret
    push    ax
    push    bx
    push    cx
    push    dx
    mov     bl, al
    mov     dx, cs:[cfg_mpu_base]
    inc     dx                      ; status port
    mov     cx, MPU_TIMEOUT
@@poll:
    in      al, dx
    test    al, 40h                 ; DRR clear: ready to accept a byte
    jz      @@send
    test    al, 80h                 ; DSR clear: a byte is waiting for us
    jnz     @@next
    call    midi_in_store
@@next:
    loop    @@poll
    ; The interface is wedged. Dropping only this byte left the next one to be
    ; sent on its own: a status byte lost this way is followed by its data
    ; bytes, which the receiver then folds into whatever message came before.
    ; Silence is a better failure than notes nobody asked for, so stay quiet
    ; until the next init.
    ;
    ; The timeout is not a busy interface. 8000h polls of an ISA port is about
    ; 33 ms, a hundred times what one byte takes at 31250 baud.
    mov     cs:[mpu_dead], 1
    jmp     short @@done
@@send:
    dec     dx                      ; data port
    mov     al, bl
    out     dx, al
@@done:
    pop     dx
    pop     cx
    pop     bx
    pop     ax
@@ret:
    ret
midi_out endp

;----------------------------------------------------------------------------
; mpu_command - send AL to the MPU-401 command port and read the 0FEh ack.
; Returns carry set when the interface did not answer.  Interrupts are held
; off across the exchange so that a timer tick cannot steal the ack.
;----------------------------------------------------------------------------
mpu_command proc near
    push    ax
    push    bx
    push    cx
    push    dx
    pushf
    cli
    mov     bl, al
    mov     dx, cs:[cfg_mpu_base]
    inc     dx
    mov     cx, MPU_TIMEOUT
@@wr:
    in      al, dx
    test    al, 40h
    jz      @@ready
    loop    @@wr
    jmp     short @@fail
@@ready:
    mov     al, bl
    out     dx, al
    mov     cx, MPU_TIMEOUT
@@rd:
    in      al, dx
    test    al, 80h
    jz      @@ack
    loop    @@rd
    jmp     short @@fail
@@ack:
    dec     dx
    in      al, dx
    cmp     al, 0FEh
    jne     @@fail
    popf
    clc
    jmp     short @@out
@@fail:
    popf
    stc
@@out:
    pop     dx
    pop     cx
    pop     bx
    pop     ax
    ret
mpu_command endp

;----------------------------------------------------------------------------
; send_cs - transmit CX bytes starting at cs:si.
;----------------------------------------------------------------------------
send_cs proc near
    push    ax
    push    cx
    push    si
    jcxz    @@out
@@next:
    mov     al, cs:[si]
    call    midi_out
    inc     si
    loop    @@next
@@out:
    pop     si
    pop     cx
    pop     ax
    ret
send_cs endp

;----------------------------------------------------------------------------
; set_master_volume - AL holds an MT-32 style 0..100 master volume, which is
; rescaled to the 0..127 of the Universal Real Time master volume message.
; That message is part of General MIDI Level 1; devices that ignore it simply
; keep playing at full level.
;----------------------------------------------------------------------------
set_master_volume proc near
    push    ax
    push    bx
    push    cx
    push    dx
    push    si
    cmp     al, 100
    jbe     @@scale
    mov     al, 100
@@scale:
    mov     ah, 0
    mov     bx, 127
    mul     bx                      ; at most 12700, high word is zero
    mov     bx, 100
    xor     dx, dx
    div     bx
    mov     cs:[vol_lsb], al
    mov     cs:[vol_msb], al
    mov     si, offset msg_volume
    mov     cx, MSG_VOLUME_LEN
    call    send_cs
    pop     si
    pop     dx
    pop     cx
    pop     bx
    pop     ax
    ret
set_master_volume endp

;----------------------------------------------------------------------------
; hush_all - All Notes Off plus Reset All Controllers on every channel.
; MT15.DRV walked channels 15 down to 1 and left channel 0 sounding; the songs
; do use channel 0, so this version covers 15 down to 0.
;----------------------------------------------------------------------------
hush_all proc near
    push    ax
    push    dx
    mov     dh, 0Fh
@@chan:
    mov     al, 0B0h
    or      al, dh
    call    midi_out
    mov     al, 07Bh                ; All Notes Off
    call    midi_out
    xor     al, al
    call    midi_out
    mov     al, 0B0h
    or      al, dh
    call    midi_out
    mov     al, 079h                ; Reset All Controllers
    call    midi_out
    xor     al, al
    call    midi_out
    dec     dh
    cmp     dh, 0FFh
    jne     @@chan
    pop     dx
    pop     ax
    ret
hush_all endp

;----------------------------------------------------------------------------
; cc_out - emit a control change.  AH = channel, BH = controller, BL = value.
;----------------------------------------------------------------------------
cc_out proc near
    push    ax
    mov     al, ah
    and     al, 0Fh
    or      al, 0B0h
    call    midi_out
    mov     al, bh
    call    midi_out
    mov     al, bl
    call    midi_out
    pop     ax
    ret
cc_out endp

;============================================================================
; Slot 00 - init
;   Returns AL = voice count.  0 or 0FFh means "driver unusable" and makes the
;   game abort, anything above 7Fh selects the 16 channel MIDI code paths in
;   the engine.  0FFF6h is what MT15.DRV returns and is kept for that reason.
;
;   This one value picks the driver class, and the engine branches on it in
;   seven places through byte_40634.  It is why slot 3Fh receives MT-32 SysEx
;   and why slot 30h can be empty.  Do not lower it without reading
;   docs/driver.md section 3.1 first: the two are a pair.
;============================================================================
gm_init:
    push    bp
    mov     bp, sp
    push    si
    push    di
    mov     cs:[mpu_dead], 0
    mov     cs:[in_head], 0
    mov     cs:[in_tail], 0

    ; An absent interface floats the bus high, so DRR never clears.
    mov     dx, cs:[cfg_mpu_base]
    inc     dx
    mov     cx, MPU_TIMEOUT
gi_probe:
    in      al, dx
    test    al, 40h
    jz      gi_present
    loop    gi_probe
    mov     cs:[mpu_dead], 1        ; play silently rather than kill the game
    jmp     gi_done

gi_present:
    mov     al, 0FFh                ; reset; clones do not always ack
    call    mpu_command
    mov     cx, 4000h
    call    io_delay
    mov     al, 3Fh                 ; enter UART mode
    call    mpu_command

    call    hush_all

    cmp     cs:[cfg_gm_reset], 0
    je      gi_no_gm
    mov     si, offset msg_gm_on
    mov     cx, MSG_GM_ON_LEN
    call    send_cs
    mov     cx, 0EA60h              ; a GM reset needs tens of milliseconds
    call    io_delay
    mov     cx, 0EA60h
    call    io_delay
gi_no_gm:
    cmp     cs:[cfg_gs_reset], 0
    je      gi_no_gs
    mov     si, offset msg_gs_on
    mov     cx, MSG_GS_ON_LEN
    call    send_cs
    mov     cx, 0EA60h
    call    io_delay
    mov     cx, 0EA60h
    call    io_delay
gi_no_gs:
    mov     al, cs:[cfg_master_vol]
    call    set_master_volume

gi_done:
    pop     di
    pop     si
    mov     ax, 0FFF6h
    pop     bp
    retf

;============================================================================
; Slot 03 - shutdown, and slot 06 - all notes off.
; The engine calls both back to back when the driver is unloaded.
;============================================================================
gm_shutdown:
gm_all_notes_off:
    call    hush_all
    retf

;============================================================================
; Slot 09 - note on
;   arg 6  channel        arg 0Ch note
;   arg 8  voice, near    arg 0Eh velocity
;   arg 0Ah chunk, near   arg 10h instrument, far
;============================================================================
gm_note_on:
    push    bp
    mov     bp, sp
    mov     bx, [bp+8]
    mov     al, [bp+0Ch]
    mov     [bx+V_NOTEBASE], al
    mov     [bx+V_CURNOTE], al
    les     bx, [bp+10h]
    cmp     byte ptr es:[bx+I_VELSENS], 0
    jne     gno_vel
    mov     al, 7Fh
    jmp     short gno_store
gno_vel:
    mov     al, [bp+0Eh]
gno_store:
    mov     bx, [bp+8]
    mov     [bx+V_VELOCITY], al
    push    ax

    ; Arm the retrigger countdown for this channel, see slot 27h.
    les     bx, [bp+10h]
    mov     ah, es:[bx+I_RETRIG]
    mov     bl, byte ptr [bp+6]
    and     bx, 0Fh
    mov     cs:retrig_ctr[bx], ah

    mov     al, byte ptr [bp+6]
    and     al, 0Fh
    or      al, 90h
    call    midi_out
    mov     al, [bp+0Ch]
    call    midi_out
    pop     ax
    call    midi_out
    pop     bp
    retf

;============================================================================
; Slot 0Ch - note off.  Sent as note on with velocity zero would break the
; engine's own bookkeeping, so a real 8n message is used, exactly as MT15 did.
;============================================================================
gm_note_off:
    push    bp
    mov     bp, sp
    mov     al, byte ptr [bp+6]
    and     al, 0Fh
    or      al, 80h
    call    midi_out
    mov     bx, [bp+8]
    mov     al, [bx+V_CURNOTE]
    call    midi_out
    xor     al, al
    call    midi_out
    pop     bp
    retf

;============================================================================
; Slot 0Fh - voice released.  Nothing to do for a MIDI device: the note off
; from slot 0Ch has already freed the synth's own voice.
;============================================================================
gm_voice_free:
    retf

;============================================================================
; Slot 12h - channel volume.  arg 6 channel, arg 8 unused, arg 0Ah value.
;============================================================================
gm_set_volume:
    push    bp
    mov     bp, sp
    mov     ah, byte ptr [bp+6]
    mov     bh, 7
    mov     bl, byte ptr [bp+0Ah]
    call    cc_out
    pop     bp
    retf

;============================================================================
; Slot 15h - arbitrary control change from a KMS controller event.
; arg 6 channel, arg 8 unused, arg 0Ah controller, arg 0Ch value.
;============================================================================
gm_set_controller:
    push    bp
    mov     bp, sp
    mov     ah, byte ptr [bp+6]
    mov     bh, byte ptr [bp+0Ah]
    mov     bl, byte ptr [bp+0Ch]
    call    cc_out
    pop     bp
    retf

;============================================================================
; Slot 18h - reset all controllers on every channel.
;============================================================================
gm_reset_controllers:
    push    ax
    push    bx
    push    dx
    mov     dh, 0Fh
grc_chan:
    mov     ah, dh
    mov     bh, 079h
    mov     bl, 0
    call    cc_out
    dec     dh
    cmp     dh, 0FFh
    jne     grc_chan
    pop     dx
    pop     bx
    pop     ax
    retf

;============================================================================
; Slot 1Bh - pitch bend.  arg 6 unused, arg 8 signed bend, arg 0Ah channel.
;============================================================================
gm_pitch_bend:
    push    bp
    mov     bp, sp
    mov     al, byte ptr [bp+0Ah]
    and     al, 0Fh
    or      al, 0E0h
    call    midi_out
    mov     ax, [bp+8]
    add     ax, 2000h
    push    ax
    and     al, 7Fh
    call    midi_out
    pop     ax
    shl     ax, 1
    mov     al, ah
    and     al, 7Fh
    call    midi_out
    pop     bp
    retf

;============================================================================
; Slot 1Eh - silence one channel.  arg 6 channel.
;============================================================================
gm_channel_off:
    push    bp
    mov     bp, sp
    mov     ah, byte ptr [bp+6]
    mov     bh, 07Bh                ; All Notes Off
    mov     bl, 0
    call    cc_out
    pop     bp
    retf

;============================================================================
; Slot 21h - program change and channel setup.
;   arg 6  channel          arg 0Ah chunk, near
;   arg 8  unused           arg 0Ch instrument, far
;
; The MT-32 build sent the program number straight through, where it selected
; one of the 128 entries of the synth's patch memory.  For General MIDI the
; number in the voice bank is a GM program instead; see the GM voice banks in
; src/sc15/voices.  Bank select is emitted whenever the driver is configured for
; GS, including bank 0, so a plain GM device never sees it and a GS device is
; never left on the wrong bank.
;
; Sending it for bank 0 as well is what makes the effects and the music able to
; share a channel.  The effects sit in variation banks 1 to 4 and the music is
; all bank 0, the engine derives a channel for any part the voice bank does not
; pin, and the two sets collide there.  Skipping the message for bank 0 left the
; channel on whatever variation the last effect selected, and the next song then
; played through it.  An SC-55 hides that: GS falls back to the capital tone when
; a variation does not exist at that program, so the music still sounds right.
; Roland's SOUND Canvas VA does not, and the menu music after a race is audibly
; wrong.
;============================================================================
gm_program_change:
    push    bp
    mov     bp, sp

    les     bx, [bp+0Ch]
    mov     ah, byte ptr [bp+6]

    cmp     cs:[cfg_gs_reset], 0
    je      gpc_program
    mov     bl, es:[bx+I_BANK]
    mov     bh, 0                   ; CC 0, bank select MSB
    call    cc_out
    mov     bh, 20h                 ; CC 32, bank select LSB
    mov     bl, 0
    call    cc_out

gpc_program:
    les     bx, [bp+0Ch]
    mov     al, ah
    and     al, 0Fh
    or      al, 0C0h
    call    midi_out
    mov     al, es:[bx+I_PROGRAM]
    call    midi_out

    ; Pitch bend sensitivity through RPN 0, then park the RPN on the null
    ; address so a later data entry controller in a song cannot disturb it.
    mov     bh, 65h                 ; CC 101, RPN MSB
    mov     bl, 0
    call    cc_out
    mov     bh, 64h                 ; CC 100, RPN LSB
    mov     bl, 0
    call    cc_out
    les     bx, [bp+0Ch]
    mov     bl, es:[bx+I_BENDRANGE]
    mov     bh, 6                   ; CC 6, data entry MSB
    call    cc_out
    mov     bh, 65h
    mov     bl, 7Fh
    call    cc_out
    mov     bh, 64h
    mov     bl, 7Fh
    call    cc_out

    les     bx, [bp+0Ch]
    mov     bl, es:[bx+I_VOLUME]
    or      bl, bl
    je      gpc_pan
    mov     bh, 7                   ; CC 7, channel volume
    call    cc_out
gpc_pan:
    les     bx, [bp+0Ch]
    mov     bl, es:[bx+I_PAN]
    mov     bh, 0Ah                 ; CC 10, pan
    call    cc_out

    pop     bp
    retf

;============================================================================
; Slot 24h - coarse pitch set.  arg 6 channel, arg 8 voice, arg 0Ah value.
; The value is scaled by 60 and used as a raw 14 bit bender position, with no
; centre bias.  Kept bit for bit as MT15 did it because the engine relies on
; the exact curve for the engine and skid effects.
;============================================================================
gm_pitch_set:
    push    bp
    mov     bp, sp
    mov     al, byte ptr [bp+6]
    and     al, 0Fh
    or      al, 0E0h
    call    midi_out
    mov     ax, [bp+0Ah]
    mov     bx, 3Ch
    mul     bx
    push    ax
    and     al, 7Fh
    call    midi_out
    pop     ax
    shl     ax, 1
    mov     al, ah
    and     al, 7Fh
    call    midi_out
    pop     bp
    retf

;============================================================================
; Slot 27h - per tick voice update, called for every sounding voice on every
; engine tick.  arg 6 channel, arg 8 voice near, arg 0Ah unused,
; arg 0Ch instrument far.
;
; Nothing is transmitted unless the instrument asks for it, which is why the
; music voice banks leave all three destinations at zero.
;============================================================================
gm_voice_update:
    push    bp
    mov     bp, sp

    ; Portamento / arpeggio: retrigger when the engine moved the pitch offset.
    les     bx, [bp+0Ch]
    cmp     byte ptr es:[bx+I_PORTA], 1
    je      gvu_porta
    jmp     gvu_retrig
gvu_porta:
    mov     bx, [bp+8]
    mov     al, [bx+V_PITCHOFS]
    add     al, [bx+V_NOTEBASE]
    cmp     al, [bx+V_CURNOTE]
    jne     gvu_restrike
    jmp     gvu_retrig
gvu_restrike:

    mov     dl, [bx+V_CURNOTE]
    mov     al, byte ptr [bp+6]
    and     al, 0Fh
    or      al, 80h
    call    midi_out
    mov     al, dl
    call    midi_out
    xor     al, al
    call    midi_out

    mov     bx, [bp+8]
    mov     al, [bx+V_PITCHOFS]
    add     al, [bx+V_NOTEBASE]
    mov     [bx+V_CURNOTE], al
    mov     dl, al
    mov     al, byte ptr [bp+6]
    and     al, 0Fh
    or      al, 90h
    call    midi_out
    mov     al, dl
    call    midi_out
    mov     bx, [bp+8]
    mov     al, [bx+V_VELOCITY]
    call    midi_out

    ; Optional retrigger.  The General MIDI sound effect programs are one shot
    ; samples: they run for about a second and stop however long the note is
    ; held.  The engine is a single note held indefinitely, so on a device whose
    ; engine sample does not loop it would fall silent a second after the car
    ; starts moving.  An instrument can ask to have its note restruck every N
    ; engine ticks, which are 100 to the second.  0 disables it, and every voice
    ; bank ships with 0, so this costs nothing unless a device needs it.
gvu_retrig:
    les     bx, [bp+0Ch]
    mov     ah, es:[bx+I_RETRIG]
    or      ah, ah
    jne     gvu_retrig_tick
    jmp     gvu_lfo
gvu_retrig_tick:
    mov     bl, byte ptr [bp+6]
    and     bx, 0Fh
    mov     al, cs:retrig_ctr[bx]
    dec     al
    jnz     gvu_retrig_wait
    mov     cs:retrig_ctr[bx], ah   ; reload the period and restrike
    mov     bx, [bp+8]
    mov     dl, [bx+V_CURNOTE]
    mov     dh, [bx+V_VELOCITY]
    mov     al, byte ptr [bp+6]
    and     al, 0Fh
    or      al, 80h
    call    midi_out
    mov     al, dl
    call    midi_out
    xor     al, al
    call    midi_out
    mov     al, byte ptr [bp+6]
    and     al, 0Fh
    or      al, 90h
    call    midi_out
    mov     al, dl
    call    midi_out
    mov     al, dh
    call    midi_out
    jmp     gvu_lfo
gvu_retrig_wait:
    mov     cs:retrig_ctr[bx], al

gvu_lfo:
    les     bx, [bp+0Ch]
    mov     al, es:[bx+I_LFODEST]
    mov     bx, [bp+8]
    mov     cx, [bx+V_LFOLEVEL]
    call    gvu_emit

    les     bx, [bp+0Ch]
    mov     al, es:[bx+I_ENVDEST]
    mov     bx, [bp+8]
    mov     cx, [bx+V_ENVLEVEL]
    call    gvu_emit

    pop     bp
    retf

; AL = destination selector, CX = value.  4 sends CC 1, 2 sends a coarse
; pitch bend, anything else sends nothing.
gvu_emit proc near
    cmp     al, 4
    jne     @@try_bend
    mov     ah, byte ptr [bp+6]
    mov     bh, 1                   ; CC 1, modulation
    mov     bl, cl
    call    cc_out
    ret
@@try_bend:
    cmp     al, 2
    jne     @@out
    mov     al, byte ptr [bp+6]
    and     al, 0Fh
    or      al, 0E0h
    call    midi_out
    xor     al, al
    call    midi_out
    mov     al, cl
    call    midi_out
@@out:
    ret
gvu_emit endp

;============================================================================
; Slots 2Ah, 2Dh, 33h - never reached by the engine.
; Slot 30h - "refresh every voice".  Empty, and only safely empty because init
; returns 0FFF6h.
;
; The engine reaches this slot from the FM class branch of audio_unk, where it
; rebuilds the driver's voice state: slot 27h once for each of the 16 VOICE
; records, then this slot with the base of the array.  A driver whose init
; returned 7Fh or less has to do real work here.  We report 0F6h, so the engine
; sets byte_40634, takes the MT-32 branch, sends the master volume through slot
; 3Fh instead, and never calls this at all.
;
; If the init return value is ever lowered to drop the MT-32 SysEx handling,
; this stub becomes a silent bug: state restoration would do nothing and there
; would be no error to see.  Slot 27h needs re-reading in that case too, since
; it would be called sixteen times in succession as a rebuild rather than once
; per tick, and it restrikes notes when the pitch offset moves.  See
; docs/driver.md section 3.1.
;============================================================================
gm_null:
gm_refresh:
    retf

;============================================================================
; Slot 36h - capability query, unused by the engine.  MT15 answers 0FFh.
;============================================================================
gm_query:
    mov     ax, 0FFh
    retf

;============================================================================
; Slot 39h - raw MIDI passthrough for KMS SysEx events.
; arg 6 byte count, arg 8 near pointer into the game data segment.
;============================================================================
gm_raw_midi:
    push    bp
    mov     bp, sp
    push    si
    mov     cx, [bp+6]
    mov     si, [bp+8]
    jcxz    grm_done
grm_next:
    mov     al, [si]
    call    midi_out
    inc     si
    loop    grm_next
grm_done:
    pop     si
    pop     bp
    retf

;============================================================================
; Slot 3Ch - fetch a received MIDI byte, or -1 when the queue is empty.
;============================================================================
gm_midi_in:
    push    si
    mov     ax, 0FFFFh
    mov     si, cs:[in_tail]
    cmp     si, cs:[in_head]
    je      gmi_done
    mov     ah, 0
    mov     al, cs:in_buf[si]
    inc     si
    and     si, IN_MASK
    mov     cs:[in_tail], si
gmi_done:
    pop     si
    retf

;============================================================================
; Slot 3Fh - MT-32 DT1 SysEx.  arg 6 byte count, arg 8 far pointer.
;
; The engine only ever uses this slot to write the MT-32 system area address
; 10 00 16, MASTER VOLUME, which it ramps from 100 down to 0 to fade a song
; out.  That single write is translated into the General MIDI universal real
; time master volume message; every other address is dropped so no MT-32
; specific bytes can reach a GM device.
;============================================================================
gm_master_volume:
    push    bp
    mov     bp, sp
    cmp     word ptr [bp+6], 4
    jb      gmv_done
    les     bx, [bp+8]
    cmp     byte ptr es:[bx], MT32_VOLUME_HI
    jne     gmv_done
    cmp     byte ptr es:[bx+1], MT32_VOLUME_MID
    jne     gmv_done
    cmp     byte ptr es:[bx+2], MT32_VOLUME_LO
    jne     gmv_done
    mov     al, es:[bx+3]
    call    set_master_volume
gmv_done:
    pop     bp
    retf

;============================================================================
; Slot 42h - patch library upload.  MT15 uploaded MT32.PLB into the synth's
; patch and timbre memory; a General MIDI device has no such memory, so the
; library is simply ignored.  The game still loads and frees MT32.PLB either
; way, which keeps its memory map identical to the MT-32 build.
;============================================================================
gm_load_patchlib:
    retf

; The skidset driver block lives in its own source so a build can leave it out.
; Define NO_SKIDSET to do that; see skidset.inc and docs/driver.md section 7.1.
IFNDEF NO_SKIDSET
    include skidset.inc
ENDIF

DRV ends
; No start address on purpose: with "format dos com" wlink would emit the image
; from the entry point onward and lose the jump table.  The resulting
; "no starting address found" warning is expected.
    end
