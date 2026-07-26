;==============================================================================
; tapioca_swd.ASM - deterministic SWD physical engine for CH32X035 PIOC.
;
; This engine is intentionally derived from Tapioca's silicon-validated
; mdio_master.ASM cadence, not lifted from an existing SWD blob:
;
;   write: drive data -> clock falls -> fixed HALF -> clock rises -> fixed HALF
;   read:  clock falls -> fixed HALF -> sample data -> clock rises -> fixed HALF
;
; IO0 / PC18 = SWCLK (output)
; IO1 / PC19 = SWDIO (bidirectional, pull-up while released)
;
; Direction changes publish the complete mode byte atomically. Driven bits are
; read back from the physical pad before SWCLK falls; write DATA/PARITY mailbox
; values therefore double as an electrical self-check for vendor diagnostics.
;
; The CPU only accesses the DATA_REG mailbox.  The PIOC owns both pads for the
; complete connected session, including SWJ sequences and pin writes.
;
; Mailbox (STATUS is always published last):
;   REG0  CTRL       bit7 GO, bit4 WAIT/FAULT data phase, bit3 idle-enabled,
;                    bit2 pins, bit1 sequence, bit0 read
;   REG1  STATUS     0 busy, 1 complete
;   REG2  FRAME      transfer: pre-encoded 8-bit SWD request, LSB first;
;                    sequence: bit0; pins: bit0 SWCLK, bit1 SWDIO
;   REG3  ACK        raw 3-bit SWD acknowledge
;   REG4..7 DATA     TX or RX word, little endian
;   REG8  TX_PARITY  bit0
;   REG9  RX_PARITY  bit0
;   REG10 TURN       turnaround cycles (1..4)
;   REG11 IDLE       idle cycles (used when CTRL.bit3 is set)
;==============================================================================

INCLUDE             pioc_sfr.inc

                    ORG   0X0000
                    DW    0X0000
                    JMP   MCU_START
                    DW    0X0FFF

CTRL                EQU   SFR_DATA_REG0
STATUS              EQU   SFR_DATA_REG1
FRAME               EQU   SFR_DATA_REG2
ACK                 EQU   SFR_DATA_REG3
DATA0               EQU   SFR_DATA_REG4
DATA1               EQU   SFR_DATA_REG5
DATA2               EQU   SFR_DATA_REG6
DATA3               EQU   SFR_DATA_REG7
TX_PARITY           EQU   SFR_DATA_REG8
RX_PARITY           EQU   SFR_DATA_REG9
TURN_COUNT          EQU   SFR_DATA_REG10
IDLE_COUNT          EQU   SFR_DATA_REG11

MCU_START:
                    CLR   SFR_PORT_DIR
                    BS    SFR_PORT_IO,SB_PORT_OUT0
                    BS    SFR_PORT_IO,SB_PORT_OUT1
                    MOVA1F 0X0F
                    CLR   STATUS

WAIT_GO:            BTSS  CTRL,7
                    JMP   WAIT_GO
                    BC    CTRL,7
                    CLR   STATUS

                    BTSC  CTRL,2
                    JMP   PIN_COMMAND
                    BTSC  CTRL,1
                    JMP   SEQUENCE_COMMAND
                    JMP   TRANSFER_COMMAND

; ---- raw pin command --------------------------------------------------------
; FRAME.bit0=SWCLK, FRAME.bit1=SWDIO. Latches are written before directions.
PIN_COMMAND:        MOV   FRAME,A
                    MOVA  SFR_DATA_EXCH
                    BP2F  BO_PORT_OUT0,0
                    BP2F  BO_PORT_OUT1,1
                    MOVA1F 0X0F
                    JMP   PUBLISH_DONE

; ---- single-bit SWJ sequence command ---------------------------------------
; Splitting uncommon SWJ sequences into one-bit mailbox commands keeps the
; PIOC program small while guaranteeing that the CPU never owns the pads.
SEQUENCE_COMMAND:   BTSC  CTRL,0
                    JMP   SEQUENCE_READ

SEQUENCE_WRITE:     MOV   FRAME,A
                    MOVA  SFR_DATA_EXCH
                    BP2F  BO_PORT_OUT1,0
                    MOVA1F 0X0F
                    CALL  DRIVE_BIT0
                    JMP   PUBLISH_DONE

SEQUENCE_READ:      MOVA1F 0X0D
                    CLR   SFR_DATA_EXCH
                    CALL  SAMPLE_BIT0
                    MOV   SFR_DATA_EXCH,A
                    MOVA  RX_PARITY
                    JMP   PUBLISH_DONE

; ---- complete SWD transfer --------------------------------------------------
TRANSFER_COMMAND:   BS    SFR_PORT_IO,SB_PORT_OUT0
                    BS    SFR_PORT_IO,SB_PORT_OUT1
                    MOVA1F 0X0F

                    MOV   FRAME,A
                    MOVA  SFR_DATA_EXCH
                    CALL  DRIVE8_LSB

                    ; Host-to-target turnaround, then ACK[0..2].
                    MOVA1F 0X0D
                    CALL  TURNAROUND
                    CLR   SFR_DATA_EXCH
                    CALL  SAMPLE_ACK3
                    MOV   SFR_DATA_EXCH,A
                    MOVA  ACK

                    ; ACK_OK is 1. ADDL sets Z when A-1 == 0.
                    ADDL  0XFF
                    JNZ   TRANSFER_NOT_OK

                    BTSC  CTRL,0
                    JMP   TRANSFER_READ

; ---- write data -------------------------------------------------------------
TRANSFER_WRITE:     CALL  TURNAROUND

                    ; Preload WDATA[0] before enabling the output driver.
                    MOV   DATA0,A
                    MOVA  SFR_DATA_EXCH
                    BP2F  BO_PORT_OUT1,0
                    MOVA1F 0X0F
                    CALL  DRIVE8_LSB
                    MOV   SFR_DATA_EXCH,A
                    MOVA  DATA0

                    MOV   DATA1,A
                    MOVA  SFR_DATA_EXCH
                    CALL  DRIVE8_LSB
                    MOV   SFR_DATA_EXCH,A
                    MOVA  DATA1
                    MOV   DATA2,A
                    MOVA  SFR_DATA_EXCH
                    CALL  DRIVE8_LSB
                    MOV   SFR_DATA_EXCH,A
                    MOVA  DATA2
                    MOV   DATA3,A
                    MOVA  SFR_DATA_EXCH
                    CALL  DRIVE8_LSB
                    MOV   SFR_DATA_EXCH,A
                    MOVA  DATA3

                    MOV   TX_PARITY,A
                    MOVA  SFR_DATA_EXCH
                    CALL  DRIVE_BIT0
                    MOV   SFR_DATA_EXCH,A
                    MOVA  TX_PARITY
                    JMP   TRANSFER_OK

; ---- read data --------------------------------------------------------------
TRANSFER_READ:      CLR   SFR_DATA_EXCH
                    CALL  SAMPLE8_LSB
                    MOV   SFR_DATA_EXCH,A
                    MOVA  DATA0
                    CLR   SFR_DATA_EXCH
                    CALL  SAMPLE8_LSB
                    MOV   SFR_DATA_EXCH,A
                    MOVA  DATA1
                    CLR   SFR_DATA_EXCH
                    CALL  SAMPLE8_LSB
                    MOV   SFR_DATA_EXCH,A
                    MOVA  DATA2
                    CLR   SFR_DATA_EXCH
                    CALL  SAMPLE8_LSB
                    MOV   SFR_DATA_EXCH,A
                    MOVA  DATA3

                    CLR   SFR_DATA_EXCH
                    CALL  SAMPLE_BIT0
                    MOV   SFR_DATA_EXCH,A
                    MOVA  RX_PARITY

                    CALL  TURNAROUND
                    BS    SFR_PORT_IO,SB_PORT_OUT1
                    MOVA1F 0X0F
                    JMP   TRANSFER_OK

; WAIT/FAULT can optionally carry a dummy data phase. Every other ACK value is
; a protocol error and requires the Arm-defined turnaround + 33-clock backoff.
TRANSFER_NOT_OK:    MOV   ACK,A
                    CMPL  0X02
                    JZ    TRANSFER_WAIT_FAULT
                    MOV   ACK,A
                    CMPL  0X04
                    JZ    TRANSFER_WAIT_FAULT
                    JMP   TRANSFER_PROTOCOL_ERROR

TRANSFER_WAIT_FAULT:
                    BTSC  CTRL,4
                    JMP   ERROR_DATA_PHASE
                    CALL  TURNAROUND
                    BS    SFR_PORT_IO,SB_PORT_OUT1
                    MOVA1F 0X0F
                    JMP   PUBLISH_DONE

TRANSFER_PROTOCOL_ERROR:
                    CALL  TURNAROUND
                    MOVL  33
                    MOVA  SFR_DATA_EXCH
PROTOCOL_BACKOFF:   BC    SFR_PORT_IO,SB_PORT_OUT0
                    CALL  HALF
                    BS    SFR_PORT_IO,SB_PORT_OUT0
                    CALL  HALF
                    MOV   SFR_DATA_EXCH,A
                    ADDL  0XFF
                    MOVA  SFR_DATA_EXCH
                    JNZ   PROTOCOL_BACKOFF
                    BS    SFR_PORT_IO,SB_PORT_OUT1
                    MOVA1F 0X0F
                    JMP   PUBLISH_DONE

ERROR_DATA_PHASE:   BTSC  CTRL,0
                    JMP   ERROR_READ_PHASE

ERROR_WRITE_PHASE:  CALL  TURNAROUND
                    CLR   SFR_DATA_EXCH
                    BP2F  BO_PORT_OUT1,0
                    MOVA1F 0X0F
                    CALL  DRIVE8_LSB
                    CALL  DRIVE8_LSB
                    CALL  DRIVE8_LSB
                    CALL  DRIVE8_LSB
                    CALL  DRIVE_BIT0
                    BS    SFR_PORT_IO,SB_PORT_OUT1
                    JMP   PUBLISH_DONE

ERROR_READ_PHASE:   CLR   SFR_DATA_EXCH
                    CALL  SAMPLE8_LSB
                    CLR   SFR_DATA_EXCH
                    CALL  SAMPLE8_LSB
                    CLR   SFR_DATA_EXCH
                    CALL  SAMPLE8_LSB
                    CLR   SFR_DATA_EXCH
                    CALL  SAMPLE8_LSB
                    CLR   SFR_DATA_EXCH
                    CALL  SAMPLE_BIT0
                    CALL  TURNAROUND
                    BS    SFR_PORT_IO,SB_PORT_OUT1
                    MOVA1F 0X0F
                    JMP   PUBLISH_DONE

TRANSFER_OK:        BTSC  CTRL,3
                    JMP   IDLE_CYCLES
                    BS    SFR_PORT_IO,SB_PORT_OUT1
                    JMP   PUBLISH_DONE

IDLE_CYCLES:        BC    SFR_PORT_IO,SB_PORT_OUT1
                    MOV   IDLE_COUNT,A
                    MOVA  SFR_DATA_EXCH
IDLE_LOOP:          BC    SFR_PORT_IO,SB_PORT_OUT0
                    CALL  HALF
                    BS    SFR_PORT_IO,SB_PORT_OUT0
                    CALL  HALF
                    MOV   SFR_DATA_EXCH,A
                    ADDL  0XFF
                    MOVA  SFR_DATA_EXCH
                    JNZ   IDLE_LOOP
                    BS    SFR_PORT_IO,SB_PORT_OUT1

PUBLISH_DONE:       MOVL  0X01
                    MOVA  STATUS
                    JMP   WAIT_GO

; ---- timing and bit primitives ---------------------------------------------
; TURN_COUNT is always clamped to 1..4 by the CPU.
TURNAROUND:         MOV   TURN_COUNT,A
                    MOVA  SFR_DATA_EXCH
TURN_LOOP:          BC    SFR_PORT_IO,SB_PORT_OUT0
                    CALL  HALF
                    BS    SFR_PORT_IO,SB_PORT_OUT0
                    CALL  HALF
                    MOV   SFR_DATA_EXCH,A
                    ADDL  0XFF
                    MOVA  SFR_DATA_EXCH
                    JNZ   TURN_LOOP
                    RET

DRIVE_BIT0:         BP2F  BO_PORT_OUT1,0
                    NOP
                    NOP
                    BG2F  BI_PORT_IN1,0
                    BC    SFR_PORT_IO,SB_PORT_OUT0
                    CALL  HALF
                    BS    SFR_PORT_IO,SB_PORT_OUT0
                    CALL  HALF
                    RET

SAMPLE_BIT0:        BC    SFR_PORT_IO,SB_PORT_OUT0
                    CALL  HALF
                    BG2F  BI_PORT_IN1,0
                    BS    SFR_PORT_IO,SB_PORT_OUT0
                    CALL  HALF
                    RET

SAMPLE_ACK3:        BC    SFR_PORT_IO,SB_PORT_OUT0
                    CALL  HALF
                    BG2F  BI_PORT_IN1,0
                    BS    SFR_PORT_IO,SB_PORT_OUT0
                    CALL  HALF
                    BC    SFR_PORT_IO,SB_PORT_OUT0
                    CALL  HALF
                    BG2F  BI_PORT_IN1,1
                    BS    SFR_PORT_IO,SB_PORT_OUT0
                    CALL  HALF
                    BC    SFR_PORT_IO,SB_PORT_OUT0
                    CALL  HALF
                    BG2F  BI_PORT_IN1,2
                    BS    SFR_PORT_IO,SB_PORT_OUT0
                    CALL  HALF
                    RET

; Drive SFR_DATA_EXCH bits 0..7, SWD wire order. Unlike MDIO, SWDIO is
; established before SWCLK falls, matching CMSIS-DAP cadence.
DRIVE8_LSB:         BP2F  BO_PORT_OUT1,0
                    NOP
                    NOP
                    BG2F  BI_PORT_IN1,0
                    BC    SFR_PORT_IO,SB_PORT_OUT0
                    CALL  HALF
                    BS    SFR_PORT_IO,SB_PORT_OUT0
                    CALL  HALF
                    BP2F  BO_PORT_OUT1,1
                    NOP
                    NOP
                    BG2F  BI_PORT_IN1,1
                    BC    SFR_PORT_IO,SB_PORT_OUT0
                    CALL  HALF
                    BS    SFR_PORT_IO,SB_PORT_OUT0
                    CALL  HALF
                    BP2F  BO_PORT_OUT1,2
                    NOP
                    NOP
                    BG2F  BI_PORT_IN1,2
                    BC    SFR_PORT_IO,SB_PORT_OUT0
                    CALL  HALF
                    BS    SFR_PORT_IO,SB_PORT_OUT0
                    CALL  HALF
                    BP2F  BO_PORT_OUT1,3
                    NOP
                    NOP
                    BG2F  BI_PORT_IN1,3
                    BC    SFR_PORT_IO,SB_PORT_OUT0
                    CALL  HALF
                    BS    SFR_PORT_IO,SB_PORT_OUT0
                    CALL  HALF
                    BP2F  BO_PORT_OUT1,4
                    NOP
                    NOP
                    BG2F  BI_PORT_IN1,4
                    BC    SFR_PORT_IO,SB_PORT_OUT0
                    CALL  HALF
                    BS    SFR_PORT_IO,SB_PORT_OUT0
                    CALL  HALF
                    BP2F  BO_PORT_OUT1,5
                    NOP
                    NOP
                    BG2F  BI_PORT_IN1,5
                    BC    SFR_PORT_IO,SB_PORT_OUT0
                    CALL  HALF
                    BS    SFR_PORT_IO,SB_PORT_OUT0
                    CALL  HALF
                    BP2F  BO_PORT_OUT1,6
                    NOP
                    NOP
                    BG2F  BI_PORT_IN1,6
                    BC    SFR_PORT_IO,SB_PORT_OUT0
                    CALL  HALF
                    BS    SFR_PORT_IO,SB_PORT_OUT0
                    CALL  HALF
                    BP2F  BO_PORT_OUT1,7
                    NOP
                    NOP
                    BG2F  BI_PORT_IN1,7
                    BC    SFR_PORT_IO,SB_PORT_OUT0
                    CALL  HALF
                    BS    SFR_PORT_IO,SB_PORT_OUT0
                    CALL  HALF
                    RET

; Sample SWDIO into SFR_DATA_EXCH bits 0..7 late in each low phase.
SAMPLE8_LSB:        BC    SFR_PORT_IO,SB_PORT_OUT0
                    CALL  HALF
                    BG2F  BI_PORT_IN1,0
                    BS    SFR_PORT_IO,SB_PORT_OUT0
                    CALL  HALF
                    BC    SFR_PORT_IO,SB_PORT_OUT0
                    CALL  HALF
                    BG2F  BI_PORT_IN1,1
                    BS    SFR_PORT_IO,SB_PORT_OUT0
                    CALL  HALF
                    BC    SFR_PORT_IO,SB_PORT_OUT0
                    CALL  HALF
                    BG2F  BI_PORT_IN1,2
                    BS    SFR_PORT_IO,SB_PORT_OUT0
                    CALL  HALF
                    BC    SFR_PORT_IO,SB_PORT_OUT0
                    CALL  HALF
                    BG2F  BI_PORT_IN1,3
                    BS    SFR_PORT_IO,SB_PORT_OUT0
                    CALL  HALF
                    BC    SFR_PORT_IO,SB_PORT_OUT0
                    CALL  HALF
                    BG2F  BI_PORT_IN1,4
                    BS    SFR_PORT_IO,SB_PORT_OUT0
                    CALL  HALF
                    BC    SFR_PORT_IO,SB_PORT_OUT0
                    CALL  HALF
                    BG2F  BI_PORT_IN1,5
                    BS    SFR_PORT_IO,SB_PORT_OUT0
                    CALL  HALF
                    BC    SFR_PORT_IO,SB_PORT_OUT0
                    CALL  HALF
                    BG2F  BI_PORT_IN1,6
                    BS    SFR_PORT_IO,SB_PORT_OUT0
                    CALL  HALF
                    BC    SFR_PORT_IO,SB_PORT_OUT0
                    CALL  HALF
                    BG2F  BI_PORT_IN1,7
                    BS    SFR_PORT_IO,SB_PORT_OUT0
                    CALL  HALF
                    RET

; Tapioca's measured MDIO profile uses the same 16-NOP half period. SWD write
; setup/readback adds a few high-phase cycles; the resulting profile is
; end-to-end qualified through CMSIS-DAP/OpenOCD at a requested 1 MHz.
HALF:               NOP
                    NOP
                    NOP
                    NOP
                    NOP
                    NOP
                    NOP
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
