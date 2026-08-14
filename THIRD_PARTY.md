# Third-party provenance

This project is MIT-licensed (see [`LICENSE`](LICENSE)) & builds on several upstream projects and public protocol specifications.

## Redistributed / adapted code

### DirtyJTAG (Jean Thomas and contributors)
- Upstream: <https://github.com/dirtyjtag/DirtyJTAG>
- Used in: [`src/dirtyjtag/`](src/dirtyjtag/) — the DirtyJTAG v2 USB command core is **adapted** from
  DirtyJTAG (see the header of [`src/dirtyjtag/protocol.cpp`](src/dirtyjtag/protocol.cpp)).
- License: MIT. Reflected by the second copyright line in [`LICENSE`](LICENSE)
  (`Copyright (c) 2017-2022 Jean THOMAS and DirtyJTAG contributors.`).

### Tapioca (myself)
- Upstream: <https://github.com/pierrejay/ch32-tapioca> (sibling project).
- Used in: the PIOC bit-bang primitives and timing this project reuses —
  [`src/hal/time.hpp`](src/hal/time.hpp) / [`.cpp`](src/hal/time.cpp) (lifted from the Tapioca HAL), the SWD physical layer in
  [`src/swd/`](src/swd/) (built on Tapioca's PIOC timing), and the `tapioca_*.ASM` engines in [`pioc/`](pioc/).
- Same author; carried over under this project's MIT license.

### ch32fun (Charles Lohr and contributors)

- Upstream: <https://github.com/cnlohr/ch32fun>.
- Used as the CH32X035 runtime and device SDK through the pinned
  [`sdk/ch32fun`](sdk/ch32fun) Git submodule; the host-side `minichlink` is built
  from that same checkout.
- License: MIT; the upstream license remains in the submodule.

## Protocols implemented from public specifications

The following are **protocol specifications**, not vendor code — this repository contains original implementations, so their upstream licenses do not attach to it.

### CMSIS-DAP v2 (Arm)
- Spec: <https://github.com/ARM-software/CMSIS-DAP>
- [`src/swd/`](src/swd/) implements the CMSIS-DAP v2 command set (the ARM ADIv5 SWD transport).
  The code is written for this project; only the wire protocol is shared.

### WCH-Link direct-DMI, RVSWIO & RVSWD

- Host tool / references: **minichlink** (Charles Lohr),
  <https://github.com/cnlohr/ch32fun>. The host executable built from the pinned
  submodule drives product 2. The USB command framing
  ([`src/wchlink/`](src/wchlink/), [`docs/wch-link-usb-protocol.md`](docs/wch-link-usb-protocol.md)) was derived from its published
  `pgm-wch-linke.c` driver.
- RVSWIO one-wire timing/recovery: reverse-engineered from `cnlohr/rv003usb`
  (`bitbang_rvswdio*`); see [`docs/wch-rvswio-protocol.md`](docs/wch-rvswio-protocol.md).
- RVSWD two-wire framing: reverse-engineered from a WCH-LinkE capture decoded with
  `perigoso/sigrok-rvswd`, cross-checked against `fxsheep/openocd_wchlink-rv`; see
  [`docs/wch-rvswd-protocol.md`](docs/wch-rvswd-protocol.md).
