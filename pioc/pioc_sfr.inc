; PIOC SFR / instruction-operand definitions for the CH32X035 coprocessor.
;
; Hand-written for this project: only the registers and constants the Tapioca PIOC
; blobs (tapioca_swd.ASM, tapioca_rvswio.ASM, tapioca_rvswd.ASM) actually reference,
; transcribed from the CH32X035 PIOC register map. These are hardware facts (SFR
; indices and instruction operand selectors), not a redistribution of any vendor
; include file. Values must match the silicon; names follow the datasheet mnemonics.

; --- SFR indices (indirect register file) ---
SFR_STATUS_REG      EQU   0x03    ; status / flags register
SFR_PORT_DIR        EQU   0x0A    ; port direction (0 = input, 1 = output)
SFR_PORT_IO         EQU   0x0B    ; port data (drive out / read in)
SFR_DATA_EXCH       EQU   0x1F    ; CPU<->PIOC exchange / doorbell
SFR_DATA_REG0       EQU   0x20    ; mailbox data registers 0..15 (0x20..0x2F)
SFR_DATA_REG1       EQU   0x21
SFR_DATA_REG2       EQU   0x22
SFR_DATA_REG3       EQU   0x23
SFR_DATA_REG4       EQU   0x24
SFR_DATA_REG5       EQU   0x25
SFR_DATA_REG6       EQU   0x26
SFR_DATA_REG7       EQU   0x27
SFR_DATA_REG8       EQU   0x28
SFR_DATA_REG9       EQU   0x29
SFR_DATA_REG10      EQU   0x2A
SFR_DATA_REG11      EQU   0x2B
SFR_DATA_REG12      EQU   0x2C
SFR_DATA_REG13      EQU   0x2D
SFR_DATA_REG14      EQU   0x2E
SFR_DATA_REG15      EQU   0x2F

; --- bit positions ---
SB_FLAG_C           EQU   0       ; carry flag, bit 0 of SFR_STATUS_REG
SB_PORT_OUT0        EQU   0       ; SFR_PORT_IO: output level, IO0
SB_PORT_OUT1        EQU   1       ; SFR_PORT_IO: output level, IO1
SB_PORT_IN1         EQU   5       ; SFR_PORT_IO: input level, IO1

; --- instruction operand selectors ---
; BCTC / BG1F / BG2F (sample an input into the carry / branch)
BI_PORT_IN1         EQU   3       ; select IO1 input
; BP1F / BP2F (drive an output from the carry)
BO_PORT_OUT0        EQU   2       ; select IO0 output
BO_PORT_OUT1        EQU   3       ; select IO1 output
