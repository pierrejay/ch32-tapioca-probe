;==============================================================================
; tapioca_rvswio.ASM - WCH RVSWIO (one-wire) PIOC engine.
;
; One contiguous DMI transaction per mailbox round-trip on IO1/PC19.
; Emission was HIL-validated on a logic analyser (9/9 frames bit-exact); the
; read path adds direction turnaround + fixed-delay level classification.
; See docs/wch-rvswio-protocol.md.
;
; IO1 / PC19 = SWIO (single bidirectional wire). IO0 / PC18 unused by RVSWIO.
; The host (minichlink) drives all DMI-level init; this engine only executes one
; read or write transaction per GO.
;
; Bit encoding (line idles high; value carried by LOW-pulse width):
;   1 : low 1*TUNIT, high 1*TUNIT      0 : low 4*TUNIT, high 1*TUNIT
; Read of one bit: drive low 1*TUNIT, release (input+pull-up), wait 2*TUNIT,
; sample the level (high=1 target released, low=0 target holding), then wait
; (bounded) for the line to return high before the next falling edge so a long
; '0' hold never collides with the next drive.
;
; DMI WRITE frame: [start=1][a6..a0][rw=1][d31..d0]         (send only)
; DMI READ  frame: send [start=1][a6..a0][rw=0], then read 32 bits MSB-first.
;
; Mailbox (STATUS published last; GO/STATUS generation boundary as in the SWD engine):
;   REG0  CTRL     bit7 GO, bit0 READ (0 = write, 1 = read)
;   REG1  STATUS   0 busy, 1 complete
;   REG2  HEAD     (address<<1)|rw, emitted MSB first (bit7=a6 .. bit1=a0, bit0=rw)
;   REG4  DATA0    d7..d0     REG5 DATA1 d15..d8    REG6 DATA2 d23..d16   REG7 DATA3 d31..d24
;   REG12 BYTE     scratch: byte being shifted out (write) or accumulated (read)
;   REG13 WHCNT    scratch: WAIT_HIGH bounded-timeout counter
;==============================================================================

INCLUDE             pioc_sfr.inc

                    ORG   0X0000
                    DW    0X0000
                    JMP   MCU_START
                    DW    0X0FFF

CTRL                EQU   SFR_DATA_REG0
STATUS              EQU   SFR_DATA_REG1
HEAD                EQU   SFR_DATA_REG2
DATA0               EQU   SFR_DATA_REG4
DATA1               EQU   SFR_DATA_REG5
DATA2               EQU   SFR_DATA_REG6
DATA3               EQU   SFR_DATA_REG7
BYTE                EQU   SFR_DATA_REG12
WHCNT               EQU   SFR_DATA_REG13

MCU_START:
                    CLR   SFR_PORT_DIR
                    BS    SFR_PORT_IO,SB_PORT_OUT1   ; IO1 latch high (idle)
                    MOVA1F 0X0F                      ; drive outputs (IO1 push-pull)
                    CLR   STATUS

WAIT_GO:            BTSS  CTRL,7
                    JMP   WAIT_GO
                    BC    CTRL,7
                    CLR   STATUS

                    ; start bit + address/rw are common to read and write
                    CALL  EMIT1                      ; start bit = 1
                    MOV   HEAD,A
                    MOVA  BYTE
                    CALL  EMIT_BYTE_MSB              ; a6..a0, rw

                    BTSC  CTRL,0
                    JMP   READ_FRAME

; ---- WRITE: emit 32 data bits, leave the line idle high ---------------------
WRITE_FRAME:        MOV   DATA3,A
                    MOVA  BYTE
                    CALL  EMIT_BYTE_MSB             ; d31..d24
                    MOV   DATA2,A
                    MOVA  BYTE
                    CALL  EMIT_BYTE_MSB             ; d23..d16
                    MOV   DATA1,A
                    MOVA  BYTE
                    CALL  EMIT_BYTE_MSB             ; d15..d8
                    MOV   DATA0,A
                    MOVA  BYTE
                    CALL  EMIT_BYTE_MSB             ; d7..d0
                    BS    SFR_PORT_IO,SB_PORT_OUT1  ; idle high
                    JMP   PUBLISH_DONE

; ---- READ: sample 32 bits MSB first into DATA3..DATA0 -----------------------
READ_FRAME:         CALL  READ_BYTE                 ; d31..d24
                    MOV   BYTE,A
                    MOVA  DATA3
                    CALL  READ_BYTE                 ; d23..d16
                    MOV   BYTE,A
                    MOVA  DATA2
                    CALL  READ_BYTE                 ; d15..d8
                    MOV   BYTE,A
                    MOVA  DATA1
                    CALL  READ_BYTE                 ; d7..d0
                    MOV   BYTE,A
                    MOVA  DATA0
                    MOVA1F 0X0F                     ; re-drive output
                    BS    SFR_PORT_IO,SB_PORT_OUT1  ; idle high
                    JMP   PUBLISH_DONE

PUBLISH_DONE:       MOVL  0X01
                    MOVA  STATUS
                    JMP   WAIT_GO

; ---- emit BYTE bits 7..0 as RVSWIO pulses -----------------------------------
; BTSC skips the following instruction when the tested bit is CLEAR: a clear bit
; falls through to EMIT0, a set bit takes the JMP to EMIT1.
EMIT_BYTE_MSB:      BTSC  BYTE,7
                    JMP   EB7_ONE
                    CALL  EMIT0
                    JMP   EB6
EB7_ONE:            CALL  EMIT1
EB6:                BTSC  BYTE,6
                    JMP   EB6_ONE
                    CALL  EMIT0
                    JMP   EB5
EB6_ONE:            CALL  EMIT1
EB5:                BTSC  BYTE,5
                    JMP   EB5_ONE
                    CALL  EMIT0
                    JMP   EB4
EB5_ONE:            CALL  EMIT1
EB4:                BTSC  BYTE,4
                    JMP   EB4_ONE
                    CALL  EMIT0
                    JMP   EB3
EB4_ONE:            CALL  EMIT1
EB3:                BTSC  BYTE,3
                    JMP   EB3_ONE
                    CALL  EMIT0
                    JMP   EB2
EB3_ONE:            CALL  EMIT1
EB2:                BTSC  BYTE,2
                    JMP   EB2_ONE
                    CALL  EMIT0
                    JMP   EB1
EB2_ONE:            CALL  EMIT1
EB1:                BTSC  BYTE,1
                    JMP   EB1_ONE
                    CALL  EMIT0
                    JMP   EB0
EB1_ONE:            CALL  EMIT1
EB0:                BTSC  BYTE,0
                    JMP   EB0_ONE
                    CALL  EMIT0
                    RET
EB0_ONE:            CALL  EMIT1
                    RET

; ---- single-bit pulse primitives --------------------------------------------
EMIT1:              BC    SFR_PORT_IO,SB_PORT_OUT1   ; low
                    CALL  TUNIT
                    BS    SFR_PORT_IO,SB_PORT_OUT1   ; high
                    CALL  TUNIT
                    RET

EMIT0:              BC    SFR_PORT_IO,SB_PORT_OUT1   ; low
                    CALL  TUNIT
                    CALL  TUNIT
                    CALL  TUNIT
                    CALL  TUNIT                      ; low for ~4*TUNIT
                    BS    SFR_PORT_IO,SB_PORT_OUT1   ; high
                    CALL  TUNIT
                    RET

; ---- read one byte, MSB first, accumulated in BYTE --------------------------
; Each bit: sample into carry, shift into BYTE immediately (C still valid), then
; wait for the line to return high before the next falling edge.
READ_BYTE:          CLR   BYTE
                    CALL  READ_BIT_SAMPLE
                    RCL   BYTE,1
                    CALL  WAIT_HIGH
                    CALL  READ_BIT_SAMPLE
                    RCL   BYTE,1
                    CALL  WAIT_HIGH
                    CALL  READ_BIT_SAMPLE
                    RCL   BYTE,1
                    CALL  WAIT_HIGH
                    CALL  READ_BIT_SAMPLE
                    RCL   BYTE,1
                    CALL  WAIT_HIGH
                    CALL  READ_BIT_SAMPLE
                    RCL   BYTE,1
                    CALL  WAIT_HIGH
                    CALL  READ_BIT_SAMPLE
                    RCL   BYTE,1
                    CALL  WAIT_HIGH
                    CALL  READ_BIT_SAMPLE
                    RCL   BYTE,1
                    CALL  WAIT_HIGH
                    CALL  READ_BIT_SAMPLE
                    RCL   BYTE,1
                    CALL  WAIT_HIGH
                    RET

; Drive the falling edge, release the wire, wait, sample the level into C.
; Critical: RELEASE to input directly - never drive the pad high before releasing,
; or a '0' bit (target holding low) is both a contention and forced high at the
; sample point, making every bit read as 1.
READ_BIT_SAMPLE:    MOVA1F 0X0F                      ; IO1 output
                    BC    SFR_PORT_IO,SB_PORT_OUT1   ; drive low (falling edge)
                    CALL  TUNIT                      ; low ~1*TUNIT
                    MOVA1F 0X0D                      ; release IO1 -> input + pull-up
                    CALL  TUNIT                      ; settle ~1*TUNIT -> sample ~2*TUNIT after the edge
                    BS    SFR_PORT_IO,SB_PORT_OUT0   ; DIAG: PC18 pulses high at the exact sample instant
                    BCTC  BI_PORT_IN1                ; C = line level (1 released, 0 held low)
                    BC    SFR_PORT_IO,SB_PORT_OUT0
                    RET

; Bounded wait until the target releases the line high; returns on timeout too so
; a stuck-low wire can never wedge the engine.
WAIT_HIGH:          MOVL  0X40
                    MOVA  WHCNT
WH_LOOP:            BTSS  SFR_PORT_IO,SB_PORT_IN1    ; skip if IN1 high
                    JMP   WH_CHECK
                    RET
WH_CHECK:           MOV   WHCNT,A
                    ADDL  0XFF
                    MOVA  WHCNT
                    JNZ   WH_LOOP
                    RET

; Nominal timing unit. HIL-validated at ~310 ns/unit (10 NOPs at 48 MHz) for
; emission; the read sample point is 3*TUNIT after each falling edge. Single knob.
TUNIT:              NOP
                    NOP
                    NOP
                    NOP
                    NOP
                    NOP
                    NOP
                    NOP
                    NOP
                    NOP
                    RET
