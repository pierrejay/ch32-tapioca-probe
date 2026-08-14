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
