# tests

Native host-side unit tests for the pure logic (codecs, protocol decoders). Each is a
standalone C++ program with its own `main()` and `assert`s — no framework, no runner.

Build and run the complete suite from the repository root:

```sh
make test
```

To build and run one manually:

```sh
c++ -std=c++17 -Wall -Wextra -Isrc -Itests tests/<name>.cpp -o /tmp/<name> && /tmp/<name>
```

Header-only tests (recipe above as-is):
- `wch_rvswd_frame_test.cpp` — RVSWD 52-bit frame codec vs golden captured frames
- `wch_link_fixtures_test.cpp` — WCH-Link USB request/reply fixtures
- `packet_order_test.cpp` — USB packet ordering
- `pioc_swd_protocol_test.cpp` — PIOC SWD transfer framing

These also need their module's `.cpp` on the command line:
- `cmsis_dap_test.cpp` + `src/swd/cmsis_dap.cpp`
- `protocol_test.cpp` + `src/dirtyjtag/protocol.cpp`
- `wch_link_protocol_test.cpp` + `src/wchlink/protocol.cpp`

## Release validation

Run the automated gate from a clean checkout before hardware testing:

```sh
git submodule update --init
make clean
make verify-pioc
make test
make clean
make test HOST_CXXFLAGS="-std=c++17 -Wall -Wextra -fsanitize=address,undefined -fno-omit-frame-pointer"
make all
make BOARD=weact jtagswd wchlink
make wchlink-rvswio wchlink-rvswio-emit wchlink-rvswd wchlink-rvswd-emit
make wchlink-diag
```

Acceptance criteria: no warning from project firmware or tests, all tests exit
zero, all three generated PIOC headers match their ASM, and every firmware fits
the linker-script FLASH/RAM regions. The macOS warning about reducing the
alignment of minichlink's `__DATA,__common` section is upstream and does not apply
to the probe firmware.

### WCH-Link hardware matrix

Flash `make flash-wchlink`, then exercise every transport with the pinned
`sdk/ch32fun/minichlink/minichlink`:

| Transport | Targets | Required checks |
|---|---|---|
| RVSWIO | CH32V003, CH32H417 | identify, program, read back, compare, reboot and re-identify |
| RVSWD | CH32X035, CH32V203, CH32V307 | identify, program, read back, compare, reboot and re-identify |

For each target, use a known-good image and preserve a readback of the original
contents when needed:

```sh
ML=sdk/ch32fun/minichlink/minichlink
IMAGE=/absolute/path/to/firmware.bin
SIZE=$(wc -c < "$IMAGE" | tr -d ' ')

"$ML" -C linke -i
"$ML" -C linke -w "$IMAGE" flash -b
"$ML" -C linke -r /tmp/tapioca-readback.bin flash "$SIZE"
cmp "$IMAGE" /tmp/tapioca-readback.bin
"$ML" -C linke -b
```

Repeat identification at least 20 times without reconnecting the probe, then run
ten program/readback cycles. No command may hang, report corrupted identity data,
or require a USB replug between sessions.

RVSWIO-specific failure checks:

1. With no target attached, connect `DIO` to ground through approximately 1 kOhm
   (never short a driven pin directly), and confirm that `DIO` is below 1 V.
   Identification must fail in bounded time.
   `make diagnose-wchlink` must report RVSWIO, `timeout`, raw status `2`, and a
   nonzero timeout counter. Remove the resistor before normal target testing.
2. Pull `CLK` weakly low through approximately 100 kOhm while flashing an RVSWIO
   target. A logic analyser must show no transition on `CLK`; normal RVSWIO
   identification and readback must still pass. This verifies that PC18 remains
   high-impedance in one-wire mode.
3. After either failure, remove the fault and identify/program the target without
   power-cycling the probe. Recovery must be automatic.

RVSWD-specific failure checks:

1. Disconnect `DIO`, then `CLK`, in separate attempts. Each identification must
   terminate without a USB stall and diagnostics must preserve the first error.
2. Restore the wire and identify/program without reconnecting USB.
3. On a responding target, repeat enough raw DMI traffic to cover transient
   `busy` replies; a sustained busy condition must terminate at the 5 ms transport
   bound rather than the host's 5 s USB timeout.

### CMSIS-DAP and DirtyJTAG hardware matrix

Flash `make flash-jtagswd` and validate:

| Interface | Targets | Host tools | Required checks |
|---|---|---|---|
| SWD | STM32G431, STM32H523 | probe-rs and OpenOCD | attach, halt, SRAM R/W, program, verify, reset/run, RTT |
| JTAG | STM32G431, STM32H523 | OpenOCD and openFPGALoader | chain scan/IDCODE, program, verify, reset/run |

Use the checked-in SWD configurations where applicable:

```sh
openocd -f openocd/stm32g431-pioc-swd.cfg
openocd -f openocd/stm32h523-pioc-swd.cfg
```

For both SWD targets, run 100 independent attach/read-DPIDR/disconnect cycles and
at least ten program/verify/reset cycles. Disconnect the target during an access:
the command must return an error rather than hang, and the next attach after
rewiring must succeed without reflashing the probe. A disconnected target still
lets the deterministic PIOC transaction complete, so this case should increment
the transfer-error count, not the mailbox-timeout count. The latter is reserved
for an internal PIOC failure. To exercise it on hardware, use a temporary
fault-injection build that disables the PIOC clock immediately before publishing
`GO`; the USB command must return an error after approximately 5 ms and increment
`mailboxTimeouts`. Do not keep that injection in a release binary.

Finally, switch JTAG → SWD → JTAG without reflashing. Explicit disconnect must
release ownership immediately; killing a client without disconnecting must allow
the other interface to acquire the wire after the documented ~1 s idle timeout.

### USB/session recovery

For each firmware personality:

1. Start a target operation and terminate the host process before it consumes the
   reply. After the probe's 1 s abandoned-reply timeout, the next session must work
   without a USB reset or replug.
2. Perform a host USB reset, then retry the target operation.
3. Suspend/resume the host or hub and retry.
4. Repeat with both USB interfaces of the JTAG/SWD composite device enumerated.

The release is qualified only when every applicable row passes on macOS and Linux.
WSL is outside the supported qualification matrix.
