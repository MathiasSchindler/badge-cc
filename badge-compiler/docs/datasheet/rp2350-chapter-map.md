# RP2350 research navigation

These are section destinations from the locked edition in [sources.md](sources.md).
PDF pages are physical 1-based indices; printed pages are independently
extracted footer labels. This map is navigation, not a verified timing ledger.

| Section | Research topic | PDF start | Printed start |
| --- | --- | ---: | ---: |
| 2 | System bus and contention | 23 | 22 |
| 2.2.2 | XIP address-map entry | 29 | 28 |
| 2.2.3 | SRAM address-map entry | 29 | 28 |
| 3.6 | Cortex-M33 coprocessors | 97 | 96 |
| 3.6.1 | GPIOC interface | 97 | 96 |
| 3.6.2 | DCP operation sequences | 100 | 99 |
| 3.6.3 | RCP security facilities | 108 | 107 |
| 3.6.4 | Single-precision FPU | 117 | 116 |
| 3.7 | Cortex-M33 core | 117 | 116 |
| 3.7.2.1.1 | RP2350 `UNCROSS_I_D` modification | 118 | 117 |
| 3.7.4.9 | Execution timing | 128 | 127 |
| 3.7.4.9.4 | Division timing | 130 | 129 |
| 3.7.4.9.5 | Register loads; Table 122 | 130 | 129 |
| 3.7.4.9.7 | Branch timing, continued on following pages | 130 | 129 |
| 4 | Memory | 312 | 311 |
| 4.2 | SRAM organization | 312 | 311 |
| 4.4 | External flash/PSRAM and XIP | 315 | 314 |
| 4.4.1 | XIP cache | 316 | 315 |
| D | RP2350 errata | 1352 | 1351 |
| E | Documentation release history | 1369 | 1368 |

The generated index covers all 19 top-level chapter/appendix bookmarks and
all 1,168 numbered section bookmarks, not just this shortlist. Every physical
page also has heading candidates, table-caption candidates and table mentions.
Use the lookup commands in the [index guide](README.md) to resolve sections,
tables or printed labels before reading an extracted page.

Section 3.7.4.9's candidate coverage is PDF pages 128-133. Shared end pages may
also start the next section; exact paragraph boundaries still require the
original page. Tables 122 and 123 resolve to physical PDF page 130. Table
references elsewhere in prose are not mistaken for their caption locations.

The cover (PDF page 1) and back cover (PDF page 1371) have no resolved printed
label. The intervening printed labels were individually checked as 1-1369;
future editions must not inherit this observed offset without re-extraction.

The Hazard3 chapter is retained in the complete source corpus so extraction
coverage stays honest, but it is not part of the Arm compiler optimization
scope. The table-footnote sample used during D00 validates formatting only;
its RISC-V instruction costs must not enter the M33 cost model.
