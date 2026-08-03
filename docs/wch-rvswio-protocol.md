# WCH RVSWIO (one-wire) protocol + PIOC engine

Framing and timing for the CH32V003-class one-wire debug transport (RVSWIO), and how
it maps onto the PIOC engine. This is the spec `pioc/tapioca_rvswio.ASM` and
`Ch32PiocRvswio` implement. It carries the same DMI semantics as the two-wire RVSWD
path ([`wch-rvswd-protocol.md`](wch-rvswd-protocol.md)); only the wire encoding differs.

Timing and framing are based on cnlohr's bit-bang implementations:
- `rv003usb/rvswdio_programmer/bitbang_rvswdio.h` — portable bit + DMI framing.
- `rv003usb/rvswdio_programmer/bitbang_rvswdio_ch32v003.h` — timer/PWM timing
  measured on real CH32V003 silicon (the numbers below).
- QingKe V2 Microprocessor Debug Manual (openwch/ch32v003).

## Wiring
- Probe `PC19` (PIOC IO1) ↔ target `PD1` (SWIO), single wire, bidirectional.
- Common GND + common 3.3 V. External pull-up 4.7–10 kΩ on the line (CH32V003 drives
  SWIO open-drain).
- Idle/disconnect: park PC19 as a floating input (never drive the target line at rest).

## Bit encoding (fixed period, PWM low-width)

The line idles high. Each bit is a falling edge, a low interval whose **width**
carries the value, then a rising edge, held high to fill a fixed bit period.

Measured on CH32V003 @ 48 MHz (`bitbang_rvswdio_ch32v003.h`, timer ticks ≈ 20.83 ns):

| Symbol | Low width | Notes |
|---|---|---|
| `1` | ~290 ns (10 ticks) | short low pulse |
| `0` | ~890 ns (36 ticks) | long low pulse (~3.6× the `1` low) |
| bit period | ~1.33 µs (`TPERIOD` 64 ticks) | ~750 kbit/s; same for `1` and `0` |

So `1` = short-low + long-high, `0` = long-low + short-high; the target classifies by
**low-pulse width**, not by total period. The ESP32 port states the same shape
symbolically: `1` = low T / high T, `0` = low 4T / high T (`bitbang_rvswdio.h`
`Send1BitSWIO`/`Send0BitSWIO`).

### Reading a bit (`ReadBitSWIO`)
1. Master drives the line low briefly (~T), then **releases** (tri-state, latch high).
2. Wait ~2T, sample the line: **high → 1**, **low → 0** (target still holding low).
3. Wait (bounded) for the line to return high before the next bit; timeout → error.

This is a pulse-width measurement: the master makes the falling edge, the target's
hold time decides the bit. The blob times the low phase with a level-duration counter
and thresholds it (~2.5T, between the `1` and `0` low widths).

## DMI transaction framing (opmode 1 = SWIO)

All bits MSB-first. Address is 7-bit. `rw` is the direction bit: `1` = write, `0` =
read. There is no parity field on SWIO.

### Write (`MCFWriteReg32`)
```
[start=1] [a6 a5 a4 a3 a2 a1 a0] [rw=1] [d31 … d0]      = 1 + 7 + 1 + 32 = 41 bits
```
then a short inter-transaction idle before the next transaction.

### Read (`MCFReadReg32`)
```
send:  [start=1] [a6 … a0] [rw=0]                        = 9 bits
read:  [d31 … d0]  (32 × ReadBitSWIO, MSB first)         timeout on any bit = error
```
then the same short idle.

## Mapping onto the PIOC engine

Deterministic PIOC wire work in the blob + C++ policy/mailbox on the CPU side.

### `pioc/tapioca_rvswio.ASM` (realtime only)
- **Emit** `1`/`0` by driving PC19 low for the short/long tick count, then high — the
  same fixed-NOP cadence primitive as the SWD engine, with two low-width constants.
- **Turnaround**: after the address+rw for a read, release PC19 to input without
  contention (no push-pull fight — rely on the external pull-up).
- **Classify reads** with the low-duration counter + threshold → bit 0/1, or a
  bounded-timeout status.
- Pack/unpack one full 41-bit (write) or 9+32-bit (read) DMI transaction through the
  CPU mailbox in a single round-trip (not bit-by-bit).
- Park PC19 floating on disconnect / USB reset.
- Every wait is bounded → a missing/stuck target never wedges the engine.

### `ch32_pioc_rvswio.{hpp,cpp}` (`Ch32PiocRvswio`, a `WchLink::IDmi`)
- Implements `connect / readDmi / writeDmi / disconnect`.
- Maps mailbox status → `DmiStatus` (Ok / Timeout / Parity / Busy / ProtocolFault).
- Uses the SWD engine's GO→STATUS generation-boundary mailbox pattern.
- Between transactions, a short guard is enforced deferred, so it adds no
  host-visible latency (see `Ch32PiocRvswio::runFrame`).

### Timing
The measured CH32V003 profile transposes directly to the PIOC, which is also 48 MHz:
low widths are expressed as PIOC loop iterations (T ≈ 208 ns low for `1`, ≈ 750 ns for
`0`, ~1.33 µs period). Validated by repeated flash / readback on a real CH32V003.
