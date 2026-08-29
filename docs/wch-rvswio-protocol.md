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

## Bit encoding (low-pulse width)

The line idles high. Each bit is a falling edge followed by a low interval whose
**width** carries the value, then a high guard interval. The PIOC implementation
does not pad both symbols to a common period.

Measured on CH32V003 @ 48 MHz (`bitbang_rvswdio_ch32v003.h`, timer ticks ≈ 20.83 ns):

| Symbol | Low width | Notes |
|---|---|---|
| `1` | ~290 ns (10 ticks) | short low pulse |
| `0` | ~890 ns (36 ticks) | long low pulse (~3.6× the `1` low) |
| bit period | ~1.33 µs (`TPERIOD` 64 ticks) | ~750 kbit/s; same for `1` and `0` |

The reference CH32V003 implementation uses a fixed period. The PIOC engine keeps
the same low-width distinction but emits `1` as low T / high T and `0` as low 4T /
high T, so its symbol duration is variable. The ESP32 port states the same shape
symbolically: `1` = low T / high T, `0` = low 4T / high T (`bitbang_rvswdio.h`
`Send1BitSWIO`/`Send0BitSWIO`).

### Reading a bit (`ReadBitSWIO`)
1. Master drives the line low briefly (~T), then **releases** (tri-state, latch high).
2. Wait another T, then sample the line approximately 2T after the falling edge:
   **high → 1**, **low → 0** (target still holding low).
3. Wait (bounded) for the line to return high before the next bit; timeout before
   another bit must be emitted → error. The wait after the final data bit remains
   bounded but does not reject the 32 bits already sampled.

The master creates the falling edge and the target's hold time determines the level
at the fixed sample point. The blob does not count or threshold the low duration;
its counter only bounds the subsequent wait for line release.

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
read:  [d31 … d0]  (32 × ReadBitSWIO, MSB first)         inter-bit timeout = error
```
then the same short idle.

## Mapping onto the PIOC engine

Deterministic PIOC wire work in the blob + C++ policy/mailbox on the CPU side.

### `pioc/tapioca_rvswio.ASM` (realtime only)
- **Emit** `1`/`0` by driving PC19 low for the short/long tick count, then high,
  using a fixed-NOP timing unit.
- **Turnaround**: after the address+rw for a read, release PC19 to input without
  contention (no push-pull fight — rely on the external pull-up).
- **Classify reads** from one fixed-delay sample, then bound the wait for SWIO to
  return high. If the line cannot be released before another bit, publish mailbox
  status `2`.
- Pack/unpack one full 41-bit (write) or 9+32-bit (read) DMI transaction through the
  CPU mailbox in a single round-trip (not bit-by-bit).
- Park PC19 floating on disconnect / USB reset.
- Every wait is bounded; status `1` means complete and status `2` reports that
  SWIO did not return high in time to emit the following edge.

### `ch32_pioc_rvswio.{hpp,cpp}` (`Ch32PiocRvswio`, a `WchLink::IDmi`)
- Implements `connect / readDmi / writeDmi / disconnect`.
- Maps mailbox complete to `DmiStatus::Ok`, wire-release or overall engine expiry
  to `DmiStatus::Timeout`, and any unknown nonzero status to `ProtocolFault`.
- Uses the SWD engine's GO→STATUS generation-boundary mailbox pattern.
- Between transactions, a short guard is enforced deferred, so it adds no
  host-visible latency (see `Ch32PiocRvswio::runFrame`).

### Timing
The PIOC runs at 48 MHz. Its timing unit is ten NOPs plus call/return overhead and was
measured at approximately 310 ns during HIL validation. A `1` uses 2T total and a
`0` uses 5T total. The profile was validated by repeated flash/readback on a real
CH32V003; the exact waveform, rather than the reference implementation's nominal
fixed period, is authoritative for this firmware.
