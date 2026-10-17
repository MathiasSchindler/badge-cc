# clean-rx firmware documentation

`clean-rx` contains freestanding C firmware for the CYW43439 and an RP2350
host application. These documents describe the current interfaces and
implementation.

- [Host integration](USAGE.md): building, loading, mailbox commands,
  receiving packets and transmitting frames.
- [Hardware and firmware architecture](HARDWARE_AND_FIRMWARE.md): memory
  layout, initialization, PSM, DMA, transport and MAC behavior.
- [HT20 interface](HT20.md): build profiles, frame descriptors and receive
  metadata.

Development history, experiments, investigations, qualification results and
ongoing work are maintained separately in [dev-doc](../dev-doc/README.md).
