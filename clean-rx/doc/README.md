# clean-rx firmware documentation

This reference describes the source-built CYW43439 firmware, its RP2350
host interface and the chip resources used by the implementation.

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
