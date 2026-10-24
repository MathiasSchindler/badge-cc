# clean-rx firmware documentation

`clean-rx` contains freestanding C firmware for the CYW43439 and an RP2350
host application. These documents describe the current interfaces and
implementation.

CYW43439 denotes the device family in these guides. The
[datasheet](../../pimoroni/Infineon_CYW43439_DataSheet_v03_00_EN-3074791.pdf)
provides family-level information, not a badge-specific silicon-version or
revision identification. Register maps and radio/PHY names describe the
paths used by the firmware implementation.

- [Host integration](USAGE.md): building, loading, mailbox commands,
  receiving packets and transmitting frames.
- [Hardware and firmware architecture](HARDWARE_AND_FIRMWARE.md): memory
  layout, initialization, PSM, DMA, transport and MAC behavior.
- [HT20 interface](HT20.md): build profiles, frame descriptors and receive
  metadata.
- [Tool and profile ownership](../dev-doc/C07_TOOL_AND_PROFILE_OWNERSHIP.md):
  firmware-only builds, development-tool locations and campaign migration.

Development history, experiments, investigations, qualification results and
ongoing work are maintained separately in [dev-doc](../dev-doc/README.md).
