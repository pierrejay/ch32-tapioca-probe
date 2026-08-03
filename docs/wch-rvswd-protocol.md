# WCH RVSWD (two-wire) protocol + PIOC engine

Companion to [`wch-rvswio-protocol.md`](wch-rvswio-protocol.md) (one-wire). RVSWD is
WCH's proprietary **two-wire** clock+data DMI transport used by the larger RISC-V
parts (CH32V203/V303/V307, CH32X035…). It carries the same DMI semantics as RVSWIO;
only the wire serialization differs. This is the transport `Ch32PiocRvswd` and
`pioc/tapioca_rvswd.ASM` implement.

The framing below was reverse-engineered from a real WCH-LinkE→CH32V307 capture
(407 transactions, decoded with `perigoso/sigrok-rvswd`) and cross-checked against
`fxsheep/openocd_wchlink-rv`. Every field was validated bit-for-bit against that
capture; the capture-derived tuples are frozen as fixtures in
[`tests/wch_rvswd_frame_test.cpp`](../tests/wch_rvswd_frame_test.cpp), which checks
the codec off-target.

## Wires and electrical framing (I2C-like)

- `SWCLK` = probe **IO0 / PC18** (output, bit-banged). `SWDIO` = probe **IO1 / PC19**
  (bidirectional). Both idle **HIGH**. Common ground; target powered.
- **START**: `SWDIO` falls (1→0) while `SWCLK` is HIGH.
- **STOP** : `SWDIO` rises (0→1) while `SWCLK` is HIGH.
- Within a transaction, **`SWDIO` changes only while `SWCLK` is LOW**; each bit is
  **sampled during the `SWCLK` HIGH phase** (1 = high, 0 = low).
- **MSB first**, all fields.

Per-bit cadence is *fall-first*: with `SWCLK` idling high — drop `SWCLK`, set/settle
`SWDIO`, raise `SWCLK` (bit is valid/sampled here), repeat.

## DMI frame — 52-bit "short" frame

One 52-bit frame does **both** read and write of any DM register — this is all the
direct-DMI USB path needs. Bit-exact layout (every field validated against the
capture):

```
bit    field          value (MSB first)
0..6   addr           a6..a0  (DM register)
7      op             0 = READ, 1 = WRITE                <-- direction
8      parity_host    XOR(addr[0:7], op)                 (even parity)
9      park           1
10..13 padding        0b0101 (0x5)                       (writes: target don't-care)
14..45 data           d31..d0   WRITE: host-driven / READ: target-driven
46     parity_data    XOR(data[0:32])
47     park           1
48..49 status         target: 1 = ok, 3 = busy (retry), 2 = fail
50..51 padding-target 2   (don't-care)
```

- **READ**  (op=0): host drives bits[0:14), then **releases at bit 14** (turnaround);
  target drives `data` + `parity_data` + `park` + `status` + pad. → 14 host / 38 targ.
- **WRITE** (op=1): host drives bits[0:48) (through `data` and its parity), then
  **releases at bit 48**; target drives only `status` + pad. → 48 host / 4 targ.
- The two "parity" fields are `[XOR-parity, park=1]`. The park bit and the write-side
  padding are not pinned by the WCH-LinkE and the **target ignores them**; the
  firmware always drives the deterministic read-clean pattern (park=1, padding=0x5).
- `status`: **1 = ok** (every successful access), **3 = busy → retry**, 2 = fail.

The register map matches the standard RISC-V Debug Module (shared with the RVSWIO
path): `0x04/0x05` data0/1, `0x10` DMCONTROL (`0x80000001` halt / `0x40000000`
resume), `0x11` DMSTATUS, `0x16` abstractcs, `0x17` command, `0x20-0x22` progbuf,
`0x7d/0x7e` DMCFGR/DMSHDWCFGR (read back `0x5aa50400`). All the DMI/unlock/USB C++ is
shared with RVSWIO; only the wire transport differs.

### The 84-bit "long" frame — observed, not used

The capture also carried 202 **84-bit** frames, *all* `addr-host=0x11`, `op` mostly 1,
host-data ≈ 0, returning `data-target=0xffffffff status=3 (busy)`. These are the
WCH-LinkE's internal DTM `dmi`(0x11) poll/keepalive (line floats high → all-ones,
busy), interleaved with the real short-frame work. The firmware answers minichlink's
transport-agnostic USB DMI command (`81 08 06 <reg> <data> <op>`) with **one short
frame per access**, so the long frame is not part of the direct-DMI path. Layout,
kept for reference: `addr-host[0:7] · data-host[7:39] · op[39:41] · parity[41:42] ·
addr-target[42:49] · data-target[49:81] · status[81:83] · parity[83:84]`.

## PIOC engine — parameterized shift register

The blob (`pioc/tapioca_rvswd.ASM`) clocks PC18/OUT0 and drives or samples PC19
with `BS`/`BC`, `RCL` and `BCTC`. All framing, parity and packing live in C++
(`rvswd_frame.hpp` / `Ch32PiocRvswd`); the blob only:

1. Wait for `CTRL` GO (bit7). Mark `STATUS` busy.
2. Enable `SWCLK`/`SWDIO` (latch high before setting DIR to avoid a startup glitch).
3. Emit **START** (drop `SWDIO` while `SWCLK` high).
4. Clock out **`HOSTBITS`** bits from the host mailbox bytes, MSB first (fall-first
   cadence). `HOSTBITS` is a mailbox byte, not hardcoded.
5. **Turnaround**: release `SWDIO` (DIR1→input), advance one clock.
6. Clock in **`TARGBITS`** bits into the target mailbox bytes, sampling on `SWCLK`
   high.
7. Re-drive `SWDIO`, emit **STOP**, idle both lines high.
8. Publish target bytes, then `STATUS` = complete **last** (generation boundary).

Because `HOSTBITS`/`TARGBITS` come from the mailbox, one blob serves both directions.
The C++ side pins them to fixed, validated constants — no per-op tuning:

| Direction | HOSTBITS | TARGBITS | Turnaround |
|---|---|---|---|
| READ  | 14 | 38 | after host bit 13 (before `data`) |
| WRITE | 48 |  4 | after host bit 47 (before `status`) |

The frame geometry is constant, so `rvswd_frame.hpp` builds the host bytes directly
with shifts/masks (the per-op hot path — no bit-by-bit assembly) and unpacks the
target bytes the same way; both are byte-exact and covered by
`tests/wch_rvswd_frame_test.cpp`. `Ch32PiocRvswd` packs {addr, op, parity, data}
into the host bytes, unpacks {data, parity, status} from the target bytes, verifies
parity, and maps status → `DmiStatus`.

### Mailbox (mirrors the RVSWIO layout where possible)

| Reg | Name | Meaning |
|---|---|---|
| REG0  | CTRL     | bit7 GO, bit0 READ (0=write, 1=read; op packed by C++) |
| REG1  | STATUS   | 0 busy, 1 complete, 0x80 protocol fault (no target / bad turnaround) |
| REG2  | HOSTBITS | number of bits to clock out before turnaround |
| REG3  | TARGBITS | number of bits to sample after turnaround |
| REG4..REG10  | HOST0..6 | host payload, MSB of HOST0 shifted out first (52 bits → 7 bytes) |
| REG11..REG17 | TARG0..6 | target payload, first sampled bit → MSB of TARG0 |

## Timing

There is no minimum clock (like MDIO), so a slow `SWCLK` is always safe and well
under the 5 ms host timeout. The sample point is the `SWCLK` HIGH phase —
deterministic, with no pulse-width classification (the RVSWIO read hazard does not
exist here). Between frames the transport enforces a short inter-frame guard
(deferred so it costs no host-visible latency; see `Ch32PiocRvswd::runFrame`).

## References

- Sigrok RVSWD decoder (field widths): `perigoso/sigrok-rvswd/pd.py`
- RE notes (empirical): `fxsheep/openocd_wchlink-rv` wiki, WCH-RVSWD-protocol
- GPIO RVSWD impls: `cnlohr/rv003usb` rvswdio_programmer; ESP32-S2 WCH programmer
- PIOC primitive base: the [Tapioca](https://github.com/pierrejay/ch32-tapioca)
  MDIO-master engine, reused as `pioc/tapioca_rvswd.ASM`
