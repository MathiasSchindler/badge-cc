# badge-cc

A C compiler and a collection of firmware, games and hardware tools for the
RP2350-based **GitHub Universe 2026 conference badge**.

The core firmware runs directly on the Cortex-M33, without a standard C
library or external target dependencies. The project includes its own C
toolchain, with LLVM used for independent testing and performance comparisons.
Optional SDK-based applications explore Bluetooth and Wi-Fi.

## What's inside

| Directory | Contents |
| --- | --- |
| [badge-compiler/](badge-compiler/) | C preprocessing, parsing, optimization, Thumb code generation, assembly and linking, with regression tests and benchmarks. |
| [src/](src/) | Badge firmware, shared utilities, hardware drivers, games, radio experiments and host tools. |
| [docs/](docs/) | Hardware specifications, application guides, protocols and physical test results. |
| [tests/](tests/) | Additional host-side firmware tests. |

## Highlights

- **[Tilt Maze](docs/tilt-maze.md)** — steer a marble through procedural mazes
  by tilting the badge, with increasingly challenging levels and hazards.
- **[Tilt Breakout](docs/tilt-breakout.md)** and
  **[two-badge Pong](docs/ble-tilt-pong.md)** — motion-controlled games,
  including multiplayer over Bluetooth.
- **[Hardware diagnostics](docs/diagnostics.md)** — interactive checks for
  the display, buttons, touch controls, motion sensors and other peripherals.
- **[USB feedback](docs/usb-live.md)** and **[Wi-Fi experiments](docs/wifi.md)**
  — live device reports, paired badge connections and measured UDP round trips.
- **[The compiler](badge-compiler/README.md)** — an end-to-end owned toolchain,
  tested against LLVM and on real badges.

## Getting started

Build the maze and run its host tests with a host C compiler and the LLVM
ARM tools:

```sh
make tilt-maze test-tilt-maze
```

Start with the [hardware specification](docs/spec.md), the
[maze guide](docs/tilt-maze.md), or the
[compiler overview](badge-compiler/README.md) for more detail.
