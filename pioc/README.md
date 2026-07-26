# PIOC engines

This directory holds the PIOC coprocessor programs (hand-written assembler),
assembled to committed C headers by `assemble.py`. A PlatformIO pre-build hook
(`scripts/verify_pioc.py`) re-assembles each one and rejects a stale header.

- `tapioca_swd.ASM` — deterministic ARM SWD physical engine (documented below).
- `tapioca_rvswio.ASM` — WCH one-wire RVSWIO transport; see [`../docs/wch-rvswio-protocol.md`](../docs/wch-rvswio-protocol.md).
- `tapioca_rvswd.ASM` — WCH two-wire RVSWD transport; see [`../docs/wch-rvswd-protocol.md`](../docs/wch-rvswd-protocol.md).

All three `INCLUDE pioc_sfr.inc`, which holds the SFR indices and instruction-operand
selectors they use (hand-written from the CH32X035 PIOC register map).

## SWD engine (`tapioca_swd.ASM`)

`tapioca_swd.ASM` is the active deterministic SWD physical engine. It is a
small auditable implementation built from the same fixed-half-period primitives
and instruction subset already exercised by Tapioca's MDIO engine. SWD writes
establish data before the falling edge, as required by the CMSIS-DAP cadence:

- PIOC IO0 / PC18: SWCLK
- PIOC IO1 / PC19: bidirectional SWDIO
- fixed nominal 1 MHz timing profile (16 NOPs per half-period)
- request, ACK, data, parity, turnaround, idle and WAIT/FAULT data phases
- raw SWJ sequences and pin writes without returning the pads to the CPU
- atomic direction changes and physical SWDIO readback before driven clocks
- `turnaround + 33 clocks` recovery for invalid ACK values

The CPU writes a shared-register mailbox and observes `GO` being consumed
before accepting `STATUS=1`. `STATUS` is the final value published by every
PIOC command, giving each transaction an explicit generation boundary.

| Register | Direction | Meaning |
|---|---|---|
| DATA_REG0 | both | control: GO/data-phase/idle/pins/sequence/read |
| DATA_REG1 | PIOC -> CPU | status (`0` busy, `1` complete) |
| DATA_REG2 | CPU -> PIOC | encoded request, sequence bit or pin values |
| DATA_REG3 | PIOC -> CPU | raw three-bit ACK |
| DATA_REG4..7 | both | little-endian transfer data |
| DATA_REG8 | CPU -> PIOC | write parity |
| DATA_REG9 | PIOC -> CPU | read parity or sequence bit |
| DATA_REG10 | CPU -> PIOC | turnaround cycles (1..4) |
| DATA_REG11 | CPU -> PIOC | idle cycles |

`assemble.py` is aligned with Tapioca's assembler. Its extra instruction
encodings are checked against WCH sources, although this SWD engine uses the
smaller Tapioca-proven subset.

The 1 MHz profile is qualified on STM32H523 with repeated independent attach,
16 KiB SRAM write/read, flash program/readback/restoration, runtime recovery and
100,000 cumulative raw DP transfers without a failing ACK or mailbox timeout.
On STM32G431, an explicit SWD-to-JTAG transition produces an invalid ACK and the
engine recovers DPIDR immediately after the reverse selection sequence. A real
MEM-AP bus fault returns `FAULT`, is cleared through DP ABORT, and is followed by
a valid DPIDR without reconnecting or power-cycling. WAIT retry behavior is
covered by the host CMSIS-DAP tests. The optional WAIT/FAULT dummy data phase is
implemented to the Arm cadence but is not enabled by the STM32 target profiles.

PIOC program memory aliases the top 4 KiB of system SRAM. The PIOC PlatformIO
environment therefore uses `ldscript/Link_CH32X035_pioc.ld`, which limits the
main CPU to the lower 16 KiB and keeps its stack below `0x20004000`.
