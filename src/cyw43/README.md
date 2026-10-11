# CYW43439 controller components

These original, freestanding C modules are the maintained source for the
CYW43439 Cortex-M3 firmware work. They are separate from the RP2350 host
driver in `src/radio/`. `make -C src/cyw43 verify` checks freestanding M3/M33
links without a target runtime. The M33 link is a portability check; the
CYW hardware binding in `rx-dma/cyw.c` is M3-only.

This is a component library, **not** a bootable or receive-capable radio image.
No default badge firmware target links these components. The pinned vendor
firmware remains the default for the working RP2350 networking targets.
Source-built on-chip startup and receive experiments are opt-in, separately
identified builds; neither generated PSM bytes nor a successful component
link proves a cold source-only received frame or RF safety.

The experimental R3 paths for these files are relative compatibility symlinks.
They preserve existing test, image-builder and evidence paths during migration;
edit the canonical files here. The PSM image array is generated from
experimental source-program descriptions; its generated C output is canonical
here, and regeneration through the existing builder updates it through the
compatibility path. Historical run records and frozen recovery assets remain
in the experiments. Tests and hardware gates still belong to their existing
owners until their build/verification paths can be migrated without changing
physical qualification identities.
