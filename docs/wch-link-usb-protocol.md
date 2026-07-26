# WCH-Link USB protocol — the `minichlink -C linke` subset

Authoritative framing for the WCH-Link personality (product 2). Every fact here is
taken from the stock host driver, not guessed from old WCH utilities:

- `minichlink/pgm-wch-linke.c` (cnlohr/ch32fun, `master`) — line numbers below.
- Cross-ref: `minichlink/minichlink.c` for the DMI register algorithm layer.

The machine-usable request/reply byte arrays live in
[`tests/wch_link_fixtures.hpp`](../tests/wch_link_fixtures.hpp).

## Transport (verified)

- Device `1a86:8010`, **interface 0** claimed directly (`pgm-wch-linke.c:158,260`).
- Bulk **OUT `0x01`** for commands, bulk **IN `0x81`** for replies, `WCHTIMEOUT = 5000` ms
  (`wch_link_command`, `pgm-wch-linke.c:59,71,81`).
- **Every command triggers an IN read.** `wch_link_command` always issues the
  `0x81` bulk-IN after the OUT, even when the caller passes `reply = NULL`
  (`pgm-wch-linke.c:74–81`). **Design consequence:** the probe must answer *every*
  command with at least one byte on EP1 IN, or the host blocks for the full 5 s
  timeout per command. There is no "silent" command.
- `FORCE_EXTERNAL_CHIP_DETECTION=1` in current builds: chip detection and flash
  run host-side on raw DMI reads/writes, so the `81 0d 01 02` reply's chip-id
  fields are not parsed by the host (`#if !FORCE_EXTERNAL_CHIP_DETECTION`,
  `pgm-wch-linke.c:471`). The DMI command (`81 08 06 …`) is the load-bearing path.

## Reply-byte contract legend

- **[V]** host-verified: the host reads/branches on this byte — must be correct.
- **[I]** host-ignored under `FORCE_EXTERNAL_CHIP_DETECTION`: content free, but a
  reply of the right shape/length must still be sent. The firmware sends a
  correctly-shaped placeholder; the exact bytes a genuine WCH-LinkE would return are
  not reproduced (the host does not read them).

## Command table

### `81 0d 01 01` — reset probe state + report version/type
`LESetupInterface`, `pgm-wch-linke.c:384,386–408`. Reply (7 bytes):

```
82 0d 04 <major> <minor> <type> 00
         [I]      [I]     [V]   [I]
```
- `type` = `rbuff[5]` selects the printed name; **`18` (0x12) = WCH-LinkE** (`:402`).
- `major`=`rbuff[3]`, `minor`=`rbuff[4]` are printed only.
- Canonical fixture: `82 0d 04 02 08 12 00`.

### `81 0d 01 ff` — stop / exit programming, release session
`LEResetInterface`/`LESetupInterface`, `pgm-wch-linke.c:355,382,450`. Called with
`reply=NULL` → content **[I]**, but a non-empty reply is mandatory (see transport).

### `81 0d 01 02` — connect / detect target
`pgm-wch-linke.c:370,374,429,435`. Reply is a 9-byte-shaped packet; host checks:
- retry if `transferred == 4` **[V-length]**, or if reply starts `81 55 01` **[V]**
  (`:371,436`) → probe must **not** produce those on success.
- under FORCE_EXTERNAL detection, chip-id bytes `rbuff[3..5]` are **[I]**.
- Documented genuine shape (comment `:370`): `82 0d 05 09 00 30 05 00`. The firmware
  replies with this shape; chip-id bytes are **[I]** placeholders.

### `81 0c 02 <family> <speed>` — set target family + interface speed
`pgm-wch-linke.c:376,411`. `family`=`target_chip_type`, `speed`=`interface_speed`
(default `01 02` = "normal"/4 MHz at `:411`). Called `reply=NULL` → **[I]**, non-empty
reply mandatory.

### `81 08 06 <reg> <d3 d2 d1 d0> <op>` — one DMI operation
`LEWriteReg32`/`LEReadReg32`, `pgm-wch-linke.c:294–345`. 9-byte request.
- `reg` = 7-bit DMI address; `op` = `1` read, `2` write (`:298,321`).
- Data is **big-endian** in request bytes 4..7 (`:301–304`).
- Reply (9 bytes): host reads data big-endian from `rbuff[4..7]` and status `rbuff[8]`
  (`:329–330`).

```
82 08 06 <reg> <d3 d2 d1 d0> <status>
[I][I][I] [I]  [V big-endian] [V]
```
- **status [V]:** `0x02` or `0x03` = error; anything else = OK (`:310,330`).
  `rbuff[3]==reg` check exists but is commented out — echo `reg` anyway.
- `resplen`/`transferred` **must be 9** or the host errors (`:310,330`) → **[V-length]**.

### `81 0d 01 03` — hold / recovery
`pgm-wch-linke.c:455,582`. Reply **[I]**, shape `82 0d 05 09 00 30 05 00` (comment).

### `81 0d 01 13` — force reset line low (host fallback)
`wch_link_multicommands`, `pgm-wch-linke.c:449`. Reply **[I]**, non-empty mandatory.

### Seen in the reset dance (implement as no-op acks first)
- `81 0b 01 01` (`:462`), `81 0d 01 0f 09` (`:634`) — reply **[I]**, non-empty.

## Error / negative fixtures
- DMI error: reply `… <status=0x02>` → host takes the error branch (`:310,330`).
- Absent target: host expects the connect retry loop to terminate via bounded
  behavior, never a hang — the probe answers each command; it does not stall EP1.

## Note on the [I] placeholder bytes
Every **[I]** byte above (the exact `81 0d 01 ff`, `81 0c 02`, `81 0d 01 02` and
`81 0d 01 13` replies) is host-ignored under `FORCE_EXTERNAL_CHIP_DETECTION`, so the
firmware only needs to return a reply of the right shape and length — which it does,
and which `minichlink` drives correctly. Reproducing a genuine WCH-LinkE's exact
values (from a usbmon / Wireshark capture) would only matter for a host tool that
parses the chip-detection replies; `minichlink` on the direct-DMI path does not.
