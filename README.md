# ch32-tapioca-probe

Two build-time USB debug-probe firmwares for CH32X035:

1. **JTAG + ARM SWD**: a homemade DirtyJTAG v2 & CMSIS-DAP v2 probe. Both share one firmware, and the first target command claims the shared wire (no reflash or rewiring).
2. **WCH-Link**: a WCH-LinkE-like probe for CH32 RISC-V, both single-wire **RVSWIO** and two-wire **RVSWD**, driven by stock `minichlink`. Auto-detects RVSWIO vs RVSWD per target.

It reuses the proven PIOC primitives from the sibling [**Tapioca**](https://github.com/pierrejay/ch32-tapioca) project.

Based on the [ch32fun](https://github.com/cnlohr/ch32fun) SDK.

A [reference design](hardware/README.md) of a tiny <€5 probe PCB (ARM SWD, RVSWIO, RVSWD) is also provided with EasyEDA & KiCad source files.

<img src="hardware/pcb_pic_probe.png" alt="CH32 Tapioca Probe reference board" width="300">

## Why?

- The CH32X035 costs around ~€0.25, has a package with minuscule footprint (`F8U6`: QFN 3x3) and needs almost no external parts: a few decoupling caps, optionally one pull-up for the USB bootloader. It's the ideal part to embed a USB debug/bridge probe **directly into a product**, and you could add a UART bridge, etc. just as easily.
- The PIOC (RISC8B coprocessor) is perfect for emulating single- or two-wire protocols deterministically.

## Tested

| Family | Interface | Chips | Host tool |
|---|---|---|---|
| ARM | JTAG | STM32H523, STM32G431 | openFPGALoader, OpenOCD |
| ARM | SWD (CMSIS-DAP) | STM32H523, STM32G431 | OpenOCD, probe-rs |
| CH32 | RVSWIO (1-wire) | CH32V003, CH32H417 | minichlink |
| CH32 | RVSWD (2-wire) | CH32X035, CH32V203, CH32V307 | minichlink |

The reference board's LED on `PA2` flickers while the probe is talking to a
target.

## Features

Both firmwares currently act as host-driven transports: the probe executes the timing-critical JTAG, SWD or WCH DMI wire transactions, while OpenOCD, probe-rs or minichlink owns the target-specific flash algorithm. This keeps the firmware small and deterministic and preserves compatibility with existing debugging tools.

The validated PIOC transports could also become building blocks for more autonomous tools. Example: load a binary into the target from an on-board memory, enabling a standalone field programmer without a PC.

### Product 1: JTAG + ARM SWD (USB `1209:C0CA`)

One firmware provides two USB interfaces, mutually exclusive on the shared wire:

- **DirtyJTAG v2** (interface 0): JTAG debug for openFPGALoader / urjtag / OpenOCD.
- **CMSIS-DAP v2** (interface 1): ARM SWD flash/debug, ST-Link-style, for OpenOCD / probe-rs.
- **Seamless switch**: `DAP_Connect` claims SWD and the first DirtyJTAG target packet claims JTAG; the probe arbitrates ownership of the shared PC18/PC19 wire. A stale owner (client exited) is auto-preempted after ~1 s idle, so switching JTAG ↔ SWD needs no reflash, rewire or manual release.

#### Pinout

JTAG and SWD share PC18/PC19 (SWJ-DP):

| Signal | Pin | Notes |
|---|---|---|
| JTAG `TCK` / SWD `SWCLK` | **PC18** (PIOC IO0) | |
| JTAG `TMS` / SWD `SWDIO` | **PC19** (PIOC IO1) | shared JTAG/SWD data |
| JTAG `TDI` | PA7 | JTAG only |
| JTAG `TDO` | PA6 | JTAG only |
| `nSRST` / SWD `nRESET` | PB0 | optional recovery |
| `nTRST` | PB1 | JTAG only |

#### Notes

- JTAG is bit-banged on the main CPU's GPIO, its four wires don't fit the 2-pin PIOC. Only ARM SWD w/ two wires runs on the PIOC.

- SWD runs a single fixed ~1 MHz profile (`DAP_SWJ_Clock` is accepted for host compatibility but doesn't change it). The ARM SWD path is validated for correctness, not tuned for speed; its throughput has not been qualified or benchmarked against a commercial probe (e.g. ST-Link).

- Full run-control debugging with SWD, not just flashing. RTT also works (host-side: `OpenOCD rtt` or `defmt`/`probe-rs`...): plain target-memory access over the debug link without trace pin.

- There is no SWO/ITM trace yet (no `DAP_SWO_*` commands), so `printf`-over-SWO and instruction trace aren't available.

- The CMSIS-DAP implementation is the SWD subset exercised by OpenOCD and
  probe-rs. Transfer value matching and the optional SWJ pin-wait behavior are
  not implemented; unsupported features are not advertised as probe capabilities.

- Example OpenOCD configs for driving a target through the probe are in [`openocd/`](openocd/) (e.g. `stm32g431-pioc-swd.cfg`).

### Product 2: WCH-Link, RVSWIO & RVSWD  (USB `1a86:8010`)

A WCH-LinkE-compatible probe for CH32 RISC-V, driven by **stock minichlink**.

The probe implements the WCH-Link *direct-DMI* passthrough only: the **host owns the flash algorithm**, the probe just runs the wire transactions. That is simple and enough for most flashing (which is why `minichlink` drives it), but it is not a full WCH-LinkE: tools that rely on the flash-programming logic built into a real probe are untested and likely won't work as-is.

- **RVSWIO**: single-wire, for the small parts (CH32V003…).
- **RVSWD**: two-wire, for the larger parts (CH32V307, CH32V203, CH32X035…).
- **Auto-detection**: one firmware probes the target and picks the transport itself, so the same binary flashes a CH32V003 and a CH32V307 with no reflash.

#### Pinout

One data line, plus a clock for the 2-wire parts:

| Signal | Pin | Notes |
|---|---|---|
| `SWCLK` | **PC18** (PIOC IO0) | 2-wire only |
| `SWDIO` / `SWIO` | **PC19** (PIOC IO1) | data (1- & 2-wire) |

#### Notes

- Some MCUs (e.g. CH32V003 1-wire and CH32X035 2-wire) drive SWDIO open-drain and need an external **~4.7–10 kΩ pull-up** to 3.3 V. Others (like CH32V203/V307) work without one; a default pull-up on the probe's PC19 makes it universal.

- On the same direct-DMI `minichlink` it flashes ~15 % faster than a genuine WCH-LinkE R0-1v3 and reads ~30 % faster (CH32V203, 32 KB payload). Insignificant in practice, and likely not true against newer probes using the vendor toolchain, but a good sign that the PIOC approach holds up. It has only been exercised with `minichlink` (not other WCH-Link host tools), and there is plenty of headroom left to optimise on the USB link and flashing process.

- Some older `minichlink` builds don't drive direct-DMI. `make minichlink` builds
  the pinned version directly from the ch32fun submodule.

## Build & flash

Clone the SDK submodule and build both products with a WCH-capable GNU RISC-V
embedded toolchain supporting `rv32imacxw` (`riscv-none-embed`,
`riscv-wch-elf`, `riscv-none-elf`, or another compatible prefix):

```sh
git submodule update --init
make
```

The Makefile uses an explicit `RISCV_PREFIX` first, then a toolchain installed
in `sdk/toolchain/`, then a compatible toolchain from `PATH`. For example, the
PlatformIO-distributed GCC 8.2.0 used to validate this project can be installed
directly in the repository without installing PlatformIO itself:

```sh
# macOS
git clone --depth 1 --branch 8.2.0 \
  https://github.com/Community-PIO-CH32V/toolchain-riscv-mac.git sdk/toolchain

# Linux x86_64, including WSL2
git clone --depth 1 --branch 8.2.0 \
  https://github.com/Community-PIO-CH32V/toolchain-riscv-linux.git sdk/toolchain
```

If the compiler has a different prefix,
pass it explicitly, for example `make RISCV_PREFIX=riscv64-unknown-elf`.

Both products flash through the CH32X035 USB ISP bootloader with
[`wchisp`](https://github.com/ch32-rs/wchisp) (hold `BOOT` while plugging in):

```sh
make flash-jtagswd   # product 1: JTAG + ARM SWD
make flash-wchlink   # product 2: WCH-Link (auto RVSWIO/RVSWD)
```

On Linux, install the included udev rule once to access the CH32X035 ROM USB ISP
bootloader without `sudo` (the user must belong to the `plugdev` group). Its
VID/PID is independent of the identity used by the probe firmware:

```sh
sudo install -m 0644 udev/50-ch32-tapioca-probe.rules /etc/udev/rules.d/
sudo udevadm control --reload-rules
```

Unplug and reconnect the probe in bootloader mode after installing the rule.

Run the native unit tests with `make test`. See `make help` for the common
targets. PC18/PC19 are the CH32X035's own SDI debug pins, so the probe itself is
flashed over USB ISP, not SWD.

## USB identity: disclaimer

Both firmwares borrow **VID/PIDs assigned to other projects/vendors** (DirtyJTAG's `1209:C0CA` and WCH-LinkE's `1a86:8010`) so that host tools like openFPGALoader, OpenOCD, probe-rs, minichlink detect the probe out of the box, with zero configuration. That is a deliberate convenience for an experimental, non-commercial project.

⚠️ **These identities are not ours**: this is not an official DirtyJTAG, WCH, or Arm product. If you build on this, it is your responsibility to assign a real USB identity (e.g. a [pid.codes](https://pid.codes) allocation) before distributing anything, and to not pass the probe off as an existing vendor's product.

A `pid.codes` allocation is planned for the reference probe. Until it is assigned
and host-tool compatibility has been checked, the borrowed identities remain for
development use only.

## Credits

- [**DirtyJTAG**](https://github.com/dirtyjtag/DirtyJTAG) / Jean Thomas
- [**CMSIS-DAP**](https://github.com/ARM-software/CMSIS-DAP) / Arm
- [**ch32fun & minichlink**](https://github.com/cnlohr/ch32fun) / Charles Lohr

See [`THIRD_PARTY.md`](THIRD_PARTY.md) for detailed contributions.
