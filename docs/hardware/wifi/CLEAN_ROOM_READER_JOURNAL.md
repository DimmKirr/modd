# Wi-Fi clean-room reader's journal

The reader's dated record for the AC791N WiFi analysis: the project timeline and the changelog
of the fact sheet (reader passes, errata, observations), with the evidence tags and the vendor
library names the reader worked from. The facts themselves are in [SPECSHEET.md](SPECSHEET.md);
the barrier rules are in [CLEAN_ROOM_METHODOLOGY.md](CLEAN_ROOM_METHODOLOGY.md).

**Reader-side material.** The implementer does not open this file (methodology, "Roles and the
barrier"). What the implementer works from besides the sheet is in
[CLEAN_ROOM_IMPLEMENTER_JOURNAL.md](CLEAN_ROOM_IMPLEMENTER_JOURNAL.md).

---

## Changelog

**Project timeline** (moved here from CLEAN_ROOM_METHODOLOGY.md on 2026-11-07, which now
carries no dated record):

- 2026-10-25: first fact sheet written from emulator traces and the first binary pass.
- 2026-10-26: a prototype driver was written directly from the intermediate representation
  (commit `6fa6dac7`; seven files, including register tables "extracted from decompiled IR").
  It was discarded the next day. Its blobs are in no file at HEAD and are not part of the
  published history, which was rebuilt on 2026-11-07 (below).
- 2026-10-27: the driver was restarted from the fact sheet (commit `313ca7ca`).
- 2026-10-28 to 2026-11-04: hardware probes and twelve bring-up iterations; reader passes on
  2026-10-31 and 2026-11-04.
- 2026-11-01: fm1-base's calibration file was used as a reference for `wifi_rf_cal.c` (the
  contamination the ruling above addresses).
- 2026-11-04: the driver written from the sheet transmits and receives (iteration 12o); the
  clean-room audit of commit 76da132d listed the open provenance items; reader pass 2 supplied
  the missing calibration facts, the tests (CLEAN_ROOM_IMPLEMENTER_JOURNAL.md §15) and the housekeeping list (§16).
- 2026-11-05: reader pass 3 added Phase D (join, authentication, association, link-up,
  keys, link-down) to SPECSHEET §4.2.1, §7.11.5, §7.13 and CLEAN_ROOM_IMPLEMENTER_JOURNAL.md §14-§16 from the implementer's
  2026-11-05 observations and the station, supplicant and transport libraries; two of the nine
  reader-side analysis packages were stopped by a safety classifier before writing and were
  redone by the reader by hand (private record §5).
- 2026-11-06: `wifi_rf_cal.c` rewritten from SPECSHEET §10.1-§10.2 with project-original names;
  the fm1-base citation, the forced unit read-backs (§15 T6) and the verbatim AGC table (now the
  §6.7 rule) removed; the CLEAN_ROOM_IMPLEMENTER_JOURNAL.md §16.3 housekeeping list closed. The RF calibration
  layer is clean-room from this date.
- 2026-11-07: the published history rebuilt as feature commits from a new root; the 2026-10-26
  prototype is not part of it. Commit ids cited above refer to the pre-rebuild history and
  resolve only in the maintainer's private record.

Update 2026-11-04 (reader pass): **[M:s wl_rf_common.a, wl_wifi_sta.a, cpu and BT controller
libraries; PUB WL82.h; M:hw 12a-12k]** A Tier 4 pass over the vendor WiFi libraries, every fact
re-checked in the binaries before it was recorded (source search record in §0). New: §4.2 rewritten
with bit semantics and the SDK register names; §4.2.1 is now the complete ordered bring-up
from clock tree to first transmit with a column comparing the GPL driver; §6.4-§6.9 (direct BBP
writes, window load order, AGC, per-channel behaviour, window D final state); §7.11.3-§7.11.6 and
the new §7.11.9 (queue selection, TX readiness gate, kick, status, RX notify, interrupt-driven
steps); new §14 "Facts awaiting hardware confirmation", ordered by how likely each explains the dead
TX/RX. **Errata** (old text kept and marked where it stood):
(1) `0x14000` is `WL_CON0`, a BT/WiFi radio arbitration and debug-mux control; the vendor never
writes it on a WiFi-only start and accesses the MAC with it at 0 (§4.2).
(2) `0x1405C` (`WF_CON7`, "WL_RST") is not a PLL divider/multiplier: bits [7:0] are the channel
code that indexes the RF-die LUT, [23:16] the TX power level (§4.2, old §4.2.1 step 3).
(3) `0x40000`-`0x40FFF` is the SoC high-speed bus (the flash controller sits at `0x40200`), not an
RT2800 WPDMA block; no WiFi code touches it (§7 header, §7.1, §7.3, §7.4, §7.6).
(4) The WDMA TX transfer starts at a 4-byte queue word in front of the TXWI, and its length is
MPDU length + 26; the measured length word is MPDU length + 22, not 16 + pad4 + 4 (§7.11.5).
(5) BBP R106 is a constant 4 written once per PHY init; the vendor writes no BBP register on a
channel change (§6.4, §6.8). BBP R94 (`0x5E`) is never written by WiFi code (§4.4).
(6) The FM-1 crystal is 24 MHz; the 40 MHz targets in §11.2.9 do not apply to this board.
(7) The five read-only indicators are never read, polled, set or cleared by any vendor code; they
are outputs of states the vendor reaches (§4.2). `0x1400C` is a low-power control/counter register,
not an ID register.
(8) The RT2800 names of several MAC table registers in §7.10 were wrong (`0x3042C`/`0x30430` are
BCN_OFFSET0/1, `0x3040C` PBF_MAX_PCNT, `0x30408` PBF_CFG, `0x31340` TXOP_CTRL_CFG, ...).
(9) All four BBP windows have a read-back register (§6.1, §6.3).
(10) `0x14080`-`0x14098` belong to an SD host controller, not to WiFi (§4.2).
Vendor function names that earlier revisions carried in §5.1, §6, §7.11, §10.1 and §11.2 were
replaced by behavioural descriptions; section numbers are unchanged.

Update 2026-11-05 (reader pass 3, Phase D; evidence cutoff: the implementer's observation file of
2026-11-05 06:50 with build dev-20261105T064012Z, its device ring and its register sweep of 06:53;
the uncommitted driver tree read on 2026-11-05 for the GPL column; verification: every row below was
re-read in the vendor libraries by the reader, and the join-step, link-up, link-down and key-table
claims were checked a second time by hand): **[M:s wl_wifi_sta.a, hostapd_and_wpasupplicant.a,
wl_rf_common.a; IP:RT2x00 (rt2800.h, rt2800lib.c); PUB assign_macaddr.c, wifi_conf.c, cpu.h,
cache.h, linker scripts, IEEE OUI registry; M:hw vendor captures 2026-11-02 and 2026-11-04, GPL
observations 2026-11-05]**. New: §4.2.1 Phase D (join, authentication, association, link-up,
EAPOL and key install, link-down: rows D1-D32 with the GPL column and the timers); §7.11.5 TXWI rows
for unicast management frames and station EAPOL frames; §7.13 (station slot table, key tables,
address handling, the acknowledgement path, the hand-off contract between the 802.11 state
machine, the supplicant and the key tables, WDMA buffer placement); §14 rows 17-19; §15 T19-T26;
§16 U6 and U14 narrowed, U15-U21 added. **Errata** (old text kept where it stood):
(22) The on-air transmitter address: nothing in the hardware or the libraries substitutes the
transmitter address of a frame; addr2 is the frame bytes (§7.13.1). The address the access point
answered on 2026-11-05 (`D8:C8:0C:xx:xx:xx`) is registered to Tuya Smart Inc.: a third station on
the same channel, whose responses the GPL driver accepted because its RX filter has bit 2 (drop
unicast not to me) clear and its parser did not check addr1.
(23) §4.2.1 B5 "1 each (shared-key mode; WCID attributes)": the attribute value 1 selects the
pairwise-key table with no cipher (bit 0 = key-table select); SHARED_KEY_MODE is the `0x7000`
block (§7.13.3).
(24) §7.10 "`|= 0x08` after association": the vendor never sets RX_FILTER_CFG bit 3 in station
mode; the running vendor radio reads `0x17F97` while associated [M:hw 2026-11-02].
(25) §7.11.5 "CCK 1 Mbps only after a rate update the initial scan does not run": that update is
the join step (D3); every management frame after the first join, including the authentication and
association requests, goes out at CCK 1 Mbps with a 304 µs duration field.
(26) The vendor's station slot 1 and the BSSID registers are written at join, **before** the
authentication request (D4-D5), not at link-up only; §4.2.1 B3's "WCID `0xFF` is never used as a
table index" stands, and the vendor's acknowledged unicast management frames all carry WCID `0xFF`.

Update 2026-11-04 (reader pass 2; evidence cutoff: the observation record through iteration 12p
and the driver at commit 76da132d; supported profile: FM-1, 24 MHz crystal, WiFi-only cold start,
station mode; verification: every claim of the private evidence record re-checked in the vendor
libraries, and the vendor unit's window D read-back of 2026-11-03 used as a cross-check): **[M:s
wl_rf_common.a, wl_wifi_sta.a, cpu and BT controller libraries; PUB SDK application sources; M:hw
12l-12o]**. The driver written from this sheet transmits and receives on the FM-1 since iteration
12o (2026-11-04 09:56): a 13-channel scan decodes beacons on channels 3-11 and lists 16 networks
[M:hw]. New: §10.1 rewritten so the full RF calibration can be implemented from this sheet alone
(RF mode table, gain-step rule, measurement engine, trim prologue, attenuation settings, RC tune,
TX LO/IQ and RX DC/IQ searches, bounds and failure contract); new §10.2 (record layout and
validity, quick path, when the record is written); §6.9 completed (both TX-filter coefficient
sets, the compression curve, the final window D state, confirmed against the vendor unit's
read-back); §11.2.9 completed (the 24 MHz constant analog block as an ordered field-write list,
the PLL field map, the crystal word, and the vendor unit's steady state split into constant and
unit-specific parts); §4.2.1 GPL column refreshed to commit 76da132d; §14 reconciled with the
observation record; new §15 (hardware tests for the implementer) and §16 (unspecified steps,
unknowns, housekeeping). **Errata** (old text kept where it stood):
(11) The vendor's receive path handles **one frame per wake** and never loops on `WDMA_CTL` bit 17
(§7.11.6); "re-check bit 17 for more frames" was wrong.
(12) In polling mode (a build option, not the default) the vendor writes neither MAC+`0x328` nor
`0x14050` bit 0 (§7.11.9); "register writes are the same" was wrong.
(13) The runtime AGC idle rule is "no target frame for 2 x 110 ms" (configurable), not 11 ticks
(§6.7).
(14) Record 187 stores its CRC-16 as a **32-bit word at bytes 64-67**, a stored 0 is invalid, and
the record is written only after a full trim that ran because the stored record was invalid (§10,
§10.2).
(15) The vendor's first-scan probe request goes out at **OFDM 6 Mbps**, not CCK 1 Mbps, unless a
profile step the sheet cannot resolve changes the management rate (§7.11.5); the MAC accepts the
GPL's CCK 1 Mbps probe [M:hw 12l].
(16) At interface up the EDCA registers receive the WMM default set (`0x64300`, `0xA4700`,
`0x43538`, `0x3212F`), not the constant set (`0xA4200` ...), which is written only at link-down or
at association (§4.2.1 B9, §7.10).
(17) `PBF_MAX_PCNT` is rewritten to `0x1F3F7F9F` at association (TX burst off); the table value
`0x1F3FBF9F` is replaced by software, not dropped by hardware (§7.10).
(18) The names `PLL_CON0..4` / `ANA_CON0..5` / `WL_CON0..11` that §11.2.9 used for
`0x11900`-`0x11964` clash with the names `WL_CONn` (`0x14000` block) and `PLL_CON0/1`
(`0x119A0`/`0x119A4`); §11.2.9 now uses the `WLA_CONn` names.
(19) The vendor identifier earlier text used for the feedback-divider code is replaced by
"divider code"; "PA_C_I" by "PA trim bytes".
(20) The bank-limit scan's degenerate case writes banks 15..0 then 127..97 (8-bit wrap, §5.2); the
GPL's bank table differs from the vendor's (43 vs 53 at divider 121 [M:hw 12n]) and works.
(21) `0x1193C` and `0x11938` do not track the channel on either unit: the MMIO PLL words stay at
divider 121 / the init bank while the RF die tunes from the LUT (§6.8, §11.2.9) [M:hw, M:s].

Update 2026-11-04 (iteration 12i, WL_CTL0 release): **[M:hw]** WL_CTL0 (`0x14000`) is an
assert/release control, not a bus gate: writing 0 after init keeps the MAC readable and starts the
baseband CCA detector (false CCA 201-240 per 100 ms, BBP R60 live) while TX and the five indicators
stay unchanged. Every init so far ran on a held block. §4.2 row updated; new §7.12 is the
stage-by-stage RX status with the open items.

Update 2026-11-03 (iteration 12h, SoC configuration probes): **[M:hw]** With every writable SoC-level
register at the vendor's steady-state value (SYS_CLK1, SYS_CLK2, WL_CLK0, WL_CLK3, `0x14054`, WL_RST,
`0x14080`-`0x14090`), five bits remain different and refuse writes: `0x1400C` b29, `0x14050` b23,
`0x14058` bits 22/21/7/1, `0x14060` b23, `WDMA_CTL` b16. They are read-only indicators of a running
MAC/BBP, not configuration (§4.2 rows and the paragraph before §4.2.1). SYS_CLK1 bit 12 is writable
and mirrors into `0x14090` bit 4 (§4.5). First direct BBP R0-R31 comparison recorded in §6.4.

Update 2026-10-26: added SDIO internal bus protocol (§7), RTOS shim API contract (§8), BBP programming
detail (§6), BLE/WiFi coexistence (§9), and evidence tags on all facts.

Update 2026-10-28: **ERRATA** §7 rewritten. The 0x50000 addresses are GPIO Port A, not SDIO. The
AC791N WiFi MAC uses direct memory-mapped registers, not an SDIO bus. Added RT2800 IP ancestry
evidence, DMA/interrupt register map (§7.1), MAC register addressing (§7.2). Added [IP:RT2x00] tag.

Update 2026-10-28 (hardware probes v15-v18): **[M:hw]** First real-hardware register probes on FM-1.
Key findings: (1) `0x50000` confirmed as GPIO, not SDIO [M:hw]. (2) DMA range `0x0200`-`0x0210`
is open bus on AC791N: write-readback returns zero [M:hw]. (3) **WL_CTL0 (`0x14000`) is the bus
gate** for the `0x30000`+ MAC register space: MAC reads fault when WL_CTL0=0, succeed when written
[M:hw]. (4) MAC_SYS_CTRL (`0x31004`) confirmed readable, returns `0x0C` (TX_EN + RX_EN) [M:hw].
(5) Full wireless control block scanned `0x14000`-`0x140A0` with 13 non-zero registers identified
[M:hw]. §4.2 and §7 updated accordingly.

Update 2026-10-28 (hardware probes v19-v22): **[M:hw]** MAC space mapping and DMA hunt.
Key findings: (1) MAC register space `0x30000`-`0x31800` confirmed alive with multiple non-zero
registers [M:hw v19-v20]. (2) `0x30200` returns `0x0BAD0BAD` sentinel (decoded but unimplemented)
[M:hw v19]. (3) Chip version at `0x31000` = `0x00300100` [M:hw v19]. (4) BBP port `0x3101C` =
`0x0008D000` confirmed on real hardware [M:hw v19]. (5) 18-address DMA hunt found two non-zero
ranges: `0x34200` (SRAM, high-entropy) and **`0x40200` (DMA candidate)** [M:hw v21-v22].
(6) `0x40000` block: base zeros, non-zero at RT2800 DMA offsets (`+0x200`=`0x809803B5`,
`+0x204`=`0x1`, `+0x208`=`0xF1FE`) [M:hw v22]. (7) Indirect access via `0x14090`/`0x14080`
disproven: `0x14080` always returns `0x800` regardless of `0x14090` value [M:hw v22].
§7.1, §7.3, §7.8 updated.

Update 2026-10-31 (RT2800 init gap analysis + write-only registers): **[IP:RT2x00, M:hw]**
(1) Missing init steps identified from Linux rt2800mmio driver: PBF_SYS_CTRL reset, MAC/BBP reset,
WPDMA_RST_IDX. PBF confirmed at 0x40408 (write succeeds [M:hw]). RST_IDX write still crashes (even
with 0x1003f). (2) WPDMA_GLO_CFG was 0xE1FF at boot: BIG_ENDIAN set, BUSY bits stuck. Writing 0x30
(Linux default) fixed it: reads back 0x30, BUSY clears instantly [M:hw]. (3) **TX_BASE_PTR0 is
write-only [IP:RT2x00, M:hw].** RT5350/RT3052 datasheets document write-only DMA registers (read
returns 0 by design). Linux rt2800 driver never reads TX_BASE_PTR back. readback=0 is not an error.
The DMA engine uses the written value internally. (4) GLO_CFG BIG_ENDIAN was the real bug: with
byte-swapped DMA accesses, descriptors and TXWI were corrupted. Now clean at 0x35.
§7.1, §7.3, §7.4, §7.9 updated.

Update 2026-10-30 (WiFi bisect on real hardware): **[M:hw]** WL_CTL8 (`0x14008`) GPIO hazard.
Key findings: (1) Writing `0xFFFF` to WL_CTL8 kills the input shift register (74HC595 chain on
PA1/PA3/PA4): encoders and buttons stop responding. All other subsystems (scheduler, UI drawing,
USB) continue normally. (2) WL_CTL0 (`0x14000`) alone is sufficient for MAC/DMA bus gating:
full WiFi init sequence (rtos, rf, sdio, mac, sta, lwip, weather) and `wifi_poll()` in a
cooperative scheduler task all work without writing WL_CTL8. (3) Bisect sequence verified each
init stage independently: rtos (PASS), rf (PASS), sdio with WL_CTL8 (FAIL: input dead), sdio
without WL_CTL8 (PASS), mac (PASS), sta (PASS), lwip+weather (PASS), poll in task (PASS).
§4.2, §7.4, §7.8 updated.

Update 2026-10-31 (WDMA discovery): **[M:s]** The MAC's DMA engine is not the RT2800 WPDMA ring
engine but a custom "WDMA" block at MAC offset `0xF00` (registers `0x30F00`-`0x30F1C`). The WPDMA
register offsets (`0x200`-`0x29C`) are never written by the vendor firmware; all TX and RX go
through WDMA. The registers at `0x30F00`/`0x30F04` previously
identified as "RF_CFG0"/"RF_CFG1" are WDMA_CFG0/WDMA_CTL. WDMA is synchronous: one packet at a
time, no ring buffers, no descriptors. The WPDMA registers at `0x40000` base are hardware-present
[M:hw] but vendor-unused. The 2026-10-31 "write-only registers" finding remains valid for direct
MMIO (our probes write the registers directly), but the vendor firmware never writes WPDMA.
§4.2, §4.3, §7.1, §7.3, §7.7, §7.11 updated.

Update 2026-10-31 (WDMA RX enable): **[M:s, M:hw]** Hardware probe confirmed WDMA TX works (bit 20
fires) but RX never triggers (bit 17 stuck low). Two writes the GPL driver lacked, seen in the
vendor firmware's write sequence: (1) MAC register `0x30328` (offset 808) must be written with
value `2` for the MAC to notify WDMA of received frames. (2) SoC register `0x14050` bit 0 must be
set to enable the WiFi interrupt path. IRQ 52 is the WiFi interrupt; the vendor firmware clears
`0x14050` bit 0 when the interrupt fires and sets it again once the received frame has been
processed. A polling driver can leave both registers set after init.
§7.11.4, §7.11.8 updated.

Update 2026-10-31 (WiFi clock tree): **[M:s]** Root cause of WDMA RX failure: the WiFi clock tree
was incomplete. The vendor firmware brings WL_CLK0 (`0x14040`) to `0x14F` (bits 0, 1, 2, 3, 6, 8)
one bit group at a time with delays between the writes (order in §4.2.1), while our init only set
bit 0 (`0x01`).
Missing bits 1,2,3,6,8 are PHY/BB/RF clock enables: without them the receiver has no clock tree.
WDMA TX worked because the WDMA engine has its own clock gate at `0x1404C` bit 24, but RF frames
cannot be received without the full clock tree. Additionally, WL_RST (`0x1405C`) must be configured
to `0x47000005` (PLL/divider config + deassert), not just `0x01` (simple deassert). The vendor firmware also
clears SYS_CLK2 (`0x10010`) bits [15:14] before clock init to route the radio clock to WiFi (those
bits control BT/WiFi clock domain selection). §4.2, §4.5, §7.4 updated.

Update 2026-10-31 (WiFi PHY/MAC hardware init): **[M:s]** Root cause of zero-AP scan identified:
after the shared BLE RF groups (1-14) the vendor firmware writes an extensive WiFi-specific PHY and
RF configuration. Without it the radio has correct clocks but the baseband demodulator is not
configured for 802.11 frame detection. Key findings: (1) the write order is PHY/MAC reset, SIFS
timing, the CCA registers, the RX front end, the RX 11n block, the TX back end, then the RF
transceiver (the ordered writes are in §4.2.1 and §11.2). (2) The BBP write protocol at `0x3101C`
is a two-phase sequence (bit 19 strobe + bit 17 kick), not the single write previously documented
in §6.2. (3) The CCA registers configure the Clear Channel Assessment engine (STF detection,
DSSS/OFDM demodulation, energy thresholds, AGC). (4) Ten CCA registers take calibration-dependent
values (STF thresholds, DSSS detection) that are not constants: they must be determined by hardware
probing. (5) The RF transceiver init writes MMIO `0x11930`/`0x11900`/`0x11904`/`0x11908` with
XOSC-dependent values. (6) The full write order is: reset, SIFS, reset, CFR calibration, PHY init,
RF init, PHY init again, CFR calibration, analog init, power/channel set.
§6.1, §6.2, §6.3, §11.2 updated.
