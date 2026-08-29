;==============================================================================
; tapioca_rvswd.ASM - WCH RVSWD (two-wire) PIOC engine.
;
; One DMI transaction per mailbox round-trip. IO0/PC18 = SWCLK (always output,
; bit-banged), IO1/PC19 = SWDIO (bidirectional). Both idle HIGH. The host
; (minichlink) drives all DMI-level init/flash; this engine executes one 52-bit
; RVSWD frame per GO. Framing, parity and packing live in C++
; (src/wchlink/rvswd_frame.hpp); this blob only clocks a fixed geometry:
;
; 52-bit SHORT frame (I2C-like: START = SWDIO falls while SWCLK high; STOP =
; SWDIO rises while SWCLK high; SWDIO changes while SWCLK low, sampled while high;
; MSB first). Geometry decoded/validated 205/205 from a real WCH-LinkE->CH32V307
; capture - see docs/wch-rvswd-protocol.md:
;   READ  (op=0): host drives 14 bits [0:14), releases, target drives 38 [14:52)
;   WRITE (op=1): host drives 48 bits [0:48), releases, target drives  4 [48:52)
; Host bits are pre-packed by C++ (addr, op, parity=XOR(addr,op), park, padding,
; data, parity=XOR(data)); this engine is value-agnostic.
;
; BP2F/BG2F are avoided because they do not drive/sample SWDIO correctly on the
; tested CH32X035 revision. BS/BC, BCTC and RCL are used instead.
;
; Mailbox (STATUS published last; GO/STATUS generation boundary as in RVSWIO):
;   REG0  CTRL    bit7 GO, bit0 READ (0 = write, 1 = read)
;   REG1  STATUS  0 busy, 1 complete
;   REG4..REG9  HOST0..HOST5  host-driven bytes, HOST0 bit7 = frame bit 0 (first out)
;   REG10 BYTE    scratch: byte being shifted out / accumulated
;   REG11..REG15 TARG0..TARG4 target-sampled bytes, first sample -> TARG0 bit7
;==============================================================================

INCLUDE             pioc_sfr.inc

                    ORG   0X0000
                    DW    0X0000
                    JMP   MCU_START
                    DW    0X0FFF

CTRL                EQU   SFR_DATA_REG0
STATUS              EQU   SFR_DATA_REG1
HOST0               EQU   SFR_DATA_REG4
HOST1               EQU   SFR_DATA_REG5
HOST2               EQU   SFR_DATA_REG6
HOST3               EQU   SFR_DATA_REG7
HOST4               EQU   SFR_DATA_REG8
HOST5               EQU   SFR_DATA_REG9
BYTE                EQU   SFR_DATA_REG10
TARG0               EQU   SFR_DATA_REG11
TARG1               EQU   SFR_DATA_REG12
TARG2               EQU   SFR_DATA_REG13
TARG3               EQU   SFR_DATA_REG14
TARG4               EQU   SFR_DATA_REG15

MCU_START:
                    CLR   SFR_PORT_DIR
                    BS    SFR_PORT_IO,SB_PORT_OUT0   ; SWCLK latch high (idle)
                    BS    SFR_PORT_IO,SB_PORT_OUT1   ; SWDIO latch high (idle)
                    MOVA1F 0X0F                      ; IO0+IO1 push-pull output
                    CLR   STATUS

WAIT_GO:            BTSS  CTRL,7
                    JMP   WAIT_GO
                    BC    CTRL,7
                    CLR   STATUS

                    ; --- START: SWDIO 1->0 while SWCLK high ---
                    BS    SFR_PORT_IO,SB_PORT_OUT0   ; ensure SWCLK high
                    BS    SFR_PORT_IO,SB_PORT_OUT1   ; ensure SWDIO high
                    MOVA1F 0X0F                      ; both output (SWDIO drive)
                    CALL  HALF
                    BC    SFR_PORT_IO,SB_PORT_OUT1   ; SWDIO -> 0 = START
                    CALL  HALF

                    BTSC  CTRL,0                     ; skip if READ clear (= write)
                    JMP   DO_READ

; ---- WRITE: drive HOST0..5 (48 bits), release, sample 4 status bits -----------
DO_WRITE:           MOV   HOST0,A
                    MOVA  BYTE
                    CALL  DRIVE8
                    MOV   HOST1,A
                    MOVA  BYTE
                    CALL  DRIVE8
                    MOV   HOST2,A
                    MOVA  BYTE
                    CALL  DRIVE8
                    MOV   HOST3,A
                    MOVA  BYTE
                    CALL  DRIVE8
                    MOV   HOST4,A
                    MOVA  BYTE
                    CALL  DRIVE8
                    MOV   HOST5,A
                    MOVA  BYTE
                    CALL  DRIVE8
                    MOVA1F 0X0D                      ; release SWDIO (turnaround @ bit 48)
                    CALL  SAMPLE3                    ; status(48,49)+pad(50) -> BYTE[7:5]
                    MOV   BYTE,A                      ; (bit 51 is master-driven by STOP)
                    MOVA  TARG0
                    JMP   DO_STOP

; ---- READ: drive 14 bits (HOST0 + HOST1[7:2]), release, sample 38 bits --------
DO_READ:            MOV   HOST0,A
                    MOVA  BYTE
                    CALL  DRIVE8                     ; frame bits 0..7
                    MOV   HOST1,A
                    MOVA  BYTE
                    CALL  DRIVE6                     ; frame bits 8..13
                    MOVA1F 0X0D                      ; release SWDIO (turnaround @ bit 14)
                    CALL  SAMPLE8                    ; data 31..24
                    MOV   BYTE,A
                    MOVA  TARG0
                    CALL  SAMPLE8                    ; data 23..16
                    MOV   BYTE,A
                    MOVA  TARG1
                    CALL  SAMPLE8                    ; data 15..8
                    MOV   BYTE,A
                    MOVA  TARG2
                    CALL  SAMPLE8                    ; data 7..0
                    MOV   BYTE,A
                    MOVA  TARG3
                    CALL  SAMPLE5                    ; parity(46),park(47),status(48,49),pad(50)
                    MOV   BYTE,A                      ; (bit 51 is master-driven by STOP)
                    MOVA  TARG4

DO_STOP:            CALL  STOP
                    MOVL  0X01
                    MOVA  STATUS
                    JMP   WAIT_GO

; ---- STOP: drive the final (52nd) frame bit = 0, then a clean STOP ------------
; The decoder pushes a bit on every SWCLK rising edge and detects STOP as SWDIO
; rising while SWCLK high. Entering with SWCLK high and 51 bits clocked: re-acquire
; SWDIO, drop SWCLK (finalises bit 51), drive SWDIO low while SWCLK low (NOT a
; START), raise SWCLK (this is the 52nd bit = 0), then raise SWDIO while SWCLK high
; = STOP. Exactly 52 bits, no spurious START. Leaves both lines idle high.
STOP:               MOVA1F 0X0F                      ; re-acquire SWDIO as output
                    BC    SFR_PORT_IO,SB_PORT_OUT0   ; SWCLK low (finalises last bit)
                    BC    SFR_PORT_IO,SB_PORT_OUT1   ; SWDIO = 0 while SWCLK low
                    CALL  HALF
                    BS    SFR_PORT_IO,SB_PORT_OUT0   ; SWCLK high = 52nd bit (0)
                    CALL  HALF
                    BS    SFR_PORT_IO,SB_PORT_OUT1   ; SWDIO 0->1 while SWCLK high = STOP
                    CALL  HALF
                    RET

; ---- drive one bit: MSB of BYTE -> SWDIO, one SWCLK cycle ---------------------
; SWCLK low, RCL BYTE (MSB -> C), SWDIO = C, low-phase, SWCLK high, high-phase.
DBIT:               BC    SFR_PORT_IO,SB_PORT_OUT0   ; SWCLK low
                    RCL   BYTE,1                      ; C = old bit7; BYTE <<= 1
                    BTSS  SFR_STATUS_REG,SB_FLAG_C    ; skip next if C set
                    BC    SFR_PORT_IO,SB_PORT_OUT1    ; C=0 -> SWDIO low
                    BTSC  SFR_STATUS_REG,SB_FLAG_C    ; skip next if C clear
                    BS    SFR_PORT_IO,SB_PORT_OUT1    ; C=1 -> SWDIO high
                    CALL  HALF
                    BS    SFR_PORT_IO,SB_PORT_OUT0    ; SWCLK high (target samples)
                    CALL  HALF
                    RET

; drive the top 8 / top 6 bits of BYTE, MSB first (DRIVE8 = 2 bits then DRIVE6).
DRIVE8:             CALL  DBIT
                    CALL  DBIT
DRIVE6:             CALL  DBIT
                    CALL  DBIT
                    CALL  DBIT
                    CALL  DBIT
                    CALL  DBIT
                    CALL  DBIT
                    RET

; ---- sample one bit: SWDIO (during SWCLK high) -> C -> BYTE LSB ---------------
SBIT:               BC    SFR_PORT_IO,SB_PORT_OUT0   ; SWCLK low (target changes SWDIO)
                    CALL  HALF
                    BS    SFR_PORT_IO,SB_PORT_OUT0   ; SWCLK high
                    CALL  HALF
                    BCTC  BI_PORT_IN1                 ; C = SWDIO level (sampled while high)
                    RCL   BYTE,1                      ; C -> BYTE bit0
                    RET

; shift BYTE left one, feeding in 0 (left-justify a partial sample tail).
ZSHIFT:             BC    SFR_STATUS_REG,SB_FLAG_C
                    RCL   BYTE,1
                    RET

; sample 8 bits into BYTE, first sample -> bit7 (MSB first).
SAMPLE8:            CLR   BYTE
                    CALL  SBIT
                    CALL  SBIT
                    CALL  SBIT
                    CALL  SBIT
                    CALL  SBIT
                    CALL  SBIT
                    CALL  SBIT
                    CALL  SBIT
                    RET

; sample 5 bits, left-justified into BYTE[7:3] (read tail: parity,park,status,pad).
SAMPLE5:            CLR   BYTE
                    CALL  SBIT
                    CALL  SBIT
                    CALL  SBIT
                    CALL  SBIT
                    CALL  SBIT
                    CALL  ZSHIFT
                    CALL  ZSHIFT
                    CALL  ZSHIFT
                    RET

; sample 3 bits, left-justified into BYTE[7:5] (write tail: status + pad).
SAMPLE3:            CLR   BYTE
                    CALL  SBIT
                    CALL  SBIT
                    CALL  SBIT
                    CALL  ZSHIFT
                    CALL  ZSHIFT
                    CALL  ZSHIFT
                    CALL  ZSHIFT
                    CALL  ZSHIFT
                    RET

; ---- half SWCLK period: the single timing knob. This conservative value is
; HIL-validated on the tested targets and completes a frame below the 5 ms timeout.
HALF:               NOP
                    NOP
                    NOP
                    NOP
                    NOP
                    NOP
                    NOP
                    NOP
                    RET
