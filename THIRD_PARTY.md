# Third-party provenance

The software is MIT-licensed and the reference hardware is CERN-OHL-P-2.0-licensed
(see [`LICENSE`](LICENSE)). The project builds on several upstream projects and
public protocol specifications.

## Redistributed / adapted code

### Tapioca (myself)
- Upstream: <https://github.com/pierrejay/ch32-tapioca> (sibling project).
- Used in: the PIOC bit-bang primitives and timing this project reuses —
  [`src/hal/time.hpp`](src/hal/time.hpp) / [`.cpp`](src/hal/time.cpp) (lifted from the Tapioca HAL), the SWD physical layer in
  [`src/swd/`](src/swd/) (built on Tapioca's PIOC timing), and the `tapioca_*.ASM` engines in [`pioc/`](pioc/).
- Same author; carried over under the software's MIT license.

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
- [`src/swd/`](src/swd/) implements the CMSIS-DAP v2 SWD subset used by OpenOCD
  and probe-rs. The code is written for this project; only the wire protocol is
  shared.

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
