# Wi-Fi clean-room implementer's journal

What the implementer works from besides the fact sheet: §14 facts awaiting hardware
confirmation, §15 hardware tests, §16 unspecified steps, unknowns and housekeeping. Section
numbers continue the sheet's (§0-§13) and are stable; code cites them as
`CLEAN_ROOM_IMPLEMENTER_JOURNAL.md §14 row N` or `§15 Tn`. Facts are in [SPECSHEET.md](SPECSHEET.md); the
barrier rules are in [CLEAN_ROOM_METHODOLOGY.md](CLEAN_ROOM_METHODOLOGY.md).

**Barrier-clean.** The reader maintains this file from the implementer's observation files and
keeps it free of vendor identifiers, vendor function names and reader notes. The reader keeps a
separate dated record, which the implementer does not open.

---

## 14. Facts awaiting hardware confirmation (2026-11-04)

Each row is a vendor behaviour the GPL driver currently lacks or does differently (§4.2.1 GPL
column), stated as register, value, position in the sequence of §4.2.1 and the observable effect
expected if the behaviour matters. Rows were ordered by how likely each explained the state before 12l (no frame queued, nothing
demodulated). **Status 2026-11-04 (pass 2):** TX works since 12l and RX since 12o [M:hw]; each row
now starts with its state: collected (values read), confirmed (effect reproduced against a
baseline with one change), confirmed in combination (effect seen only together with other
changes), applied (implemented, not measured), open (not implemented), with the observation that
decided it. The original ranking was an inference [I]; the register facts carry their own tags. "Read on
both" means: read the register on the running vendor radio and on modd before changing
anything; none of these registers has been compared so far unless stated.

| # | Register | Value | Position | Expected observable effect | Tag |
|---|----------|-------|----------|----------------------------|-----|
| 1 | **Confirmed 2026-11-04 12l [M:hw]: frame queued and transmitted.** WDMA TX image: first transferred word | queue word (bit 0 = WIV, bits [2:1] = QSEL = 2; 5 for management, 4 for data), then the 16-byte TXWI, the unpadded frame and 6 more bytes; `WDMA_TX_ADR` = address of the queue word, `WDMA_TX_LEN` = MPDU + 26 | every TX (§4.2.1 C7-C8, §7.11.5) | `TXRXQ_PCNT` byte 2 rises during the transfer and falls after; `TX_STA_CNT0` success or failure counts move; `TX_STA_FIFO` (`0x31718`) valid bit set; the probe request appears on a sniffer. The GPL's first word today is the software length (bits [2:1] of 56 = queue 0) and its TXWI is shifted by 4 bytes | M:s wl_rf_common.a, M:hw (buffer samples), I |
| 2 | **Collected 12l [M:hw]: `0x3043C` = `0x8400A0FE` ([7:5] = 7), `0x30434` = `0x22020602` (bit 18 clear), queue-2 pages 0 before and 1 right after the kick; the gate passed first try.** `0x3043C` [7:5], `0x30434` bit 18, `0x30438` byte 2 | vendor requires [7:5] != 0, bit 18 = 0 and byte 2 below the queue-2 limit before every TX | read immediately before each TX (C6) | Cheapest discriminating read for TX: if `0x3043C` [7:5] reads 0 on modd the packet buffer is not ready and no framing change can queue a frame; if it is non-zero the blocker is downstream (row 1). Read on both | M:s wl_rf_common.a, I |
| 3 | **Confirmed [M:hw 12l-12o]: with rows 1, 3-7 applied the receiver decodes frames on every channel.** MAC+`0x1328` TX_PIN_CFG | `0x00050302` (RT2800 bits: PA_PE_G0_EN 1, LNA_PE_A0_EN 8, LNA_PE_G0_EN 9, RFTR_EN 16, TRSW_EN 18) | at every channel switch, after the channel code (C1) and TX_BAND_CFG (C2), before the first probe request; never written earlier | RX: PLCP/CRC error counts in `RX_STA_CNT0` become non-zero, beacons and probe responses arrive (bit 17 of `WDMA_CTL`, interrupt 52). TX: PA enabled on air. Read on both first (expected vendor `0x00050302`, modd the reset value) | M:s wl_wifi_sta.a, IP:RT2x00 for bit names, I |
| 4 | **Confirmed with row 3 [M:hw 12l-12o].** MAC+`0x132C` TX_BAND_CFG | read-modify-write: bits [2:1] cleared, bit 2 set (2.4 GHz band) | every channel switch, directly before TX_PIN_CFG (C2) | band select for TX and RX paths [IP:RT2x00]; same effects as row 3 if the reset value selects the other band. Read on both first | M:s wl_wifi_sta.a, IP:RT2x00, I |
| 5 | **Confirmed in combination [M:hw 12m-12o]: with the channel code as the only per-channel RF write, frames decode on channels 3-11; R106 stays 4; the MMIO PLL words stay at divider 121 / the init bank on both units.** `0x1405C` [7:0] | channel + 101 (one read-modify-write, no delay); with no BBP R94 write, no BBP R106 write (R106 stays 4), no `0x1193C`/`0x11934` write | every channel switch (C1), first after the scan starts | BBP R106 reads 4 throughout a scan (modd reads the channel number today); the RF die retunes from the LUT entry for the channel; demodulation on channels other than 6; the two `WF_CON6` codes change with the channel [I] | M:s wl_rf_common.a, M:hw (R106), I |
| 6 | **Confirmed in combination [M:hw 12n-12o]: a flat bank table (bank 15 everywhere, 12m) gave no reception; the measured table (12n) gave reception on every channel (12o); the necessity of the bank field alone was not isolated (§15 T3).** RF-die LUT word 1, entries 0-127 | `\|=` bank << 9, bank = lookup(word 1 [28:21]) in the 48-entry limit table built by the bank scan of §5.2 | LUT load (A20), after PHY init #2, CFR mode 1 and RAPP, before auto control | With auto control on, the synthesizer runs in the right VCO band for the selected channel; candidates for `WF_CON4` b23 / `WF_CON8` b23 (lock or ready) asserting and for the `WF_CON6` codes leaving `0xFF` [I] | M:s wl_rf_common.a, I |
| 7 | **Applied at HEAD (LUT load and auto control repeated after the MAC init, so no reset follows them); not separately measured (§15 T4).** End of the front-end init | order PHY init #2 → CFR mode 1 → RAPP → LUT load → auto control → power → channel, and **no** MAC/BBP reset (MAC_SYS_CTRL 3, MAC+`0x30C` pulse, R21 pulse) or PHY init after the LUT load and auto control | A17-A23 | Read the four indicators after each of A20, A21 and A23: if one asserts, the step that raised it is the missing condition. A reset after auto control may stop the RF-die sequencer [I] | M:s wl_rf_common.a, I |
| 8 | **Closed 2026-11-06:** the rewrite from §10.1-§10.2 retired the forced read-backs (§15 T6); calibration follows the sheet's procedure, with the quick path from VM 187. RF calibration settings | 24 MHz crystal type (`0x11930` [27:23] = `0x0B`), N = 101, fraction `0xC00000`, `0x11954` = `0x02822520` during calibration; or the quick path from VM 187 when its CRC-16 is valid | A16 | RX DC values in the LUT AGC words and TX IQ/DC values in window D taken under the board's real reference clock; RX sensitivity and AGC behaviour closer to the vendor (R32-R46 values) | M:s wl_rf_common.a, M:hw (crystal type), I |
| 9 | **Open (missing at HEAD; §15 T8).** MAC+`0x1800` + 8i (i = 0..253); MAC+`0x6800` + 4i (i = 0..255); MAC+`0x7000`..`0x700C`; MAC+`0x1114` | `0xFFFFFFFF`, `0x00FFFFFF`; 1; 0; `&= ~0x001F0000` | interface up (B3, B5, B6), before the first TX | The broadcast/unassociated WCID (`0xFF`) used by the probe request has a defined table entry; TX status for WCID `0xFF` frames [IP:RT2x00, I]; TSF/beacon sync off during the scan | M:s wl_wifi_sta.a, IP:RT2x00, I |
| 10 | **Implemented at HEAD 76da132d (12p): no write to `0x14000` on the boot path; the plain-boot scan result is the §15 T1 regression check, not yet in the observation record.** `0x14000` WL_CON0 | never written: no `0xFFFF`, no later 0; clock tree (A2-A3) built before the first MAC access | whole start | MAC/BBP readable from power-on with `WL_CON0` = 0; results identical to the released case of 12j/12k. Confirms that the v13 fault came from a missing clock tree, and removes the BT/WiFi arbitration side effects of `0xFFFF` | M:s all libraries, M:hw (vendor 0), I |
| 11 | **Open (the GPL writes 0 and 4; §15 T7).** MAC_SYS_CTRL (MAC+`0x1004`) | only 3 (with the `0x30C` pulse) and `0xC`; never 0 or 4 after the last PHY reset | A9, A10, A13, end of A15/A17; nothing in phase B | No transient TX/RX disable after init; effect unknown | M:s wl_rf_common.a, I |
| 12 | **Open (the GPL uses post-association values; §7.10 now gives the interface-up and association sets; §15 T9).** MAC configuration values | table of §7.10 (e.g. `0x31408` = `0x13F`, `0x31400` = `0x17F97` from B12, `0x31350` = `0x1020`), no write to MAC+`0x1200` or MAC+`0x137C` | B2, B12 | Unlikely to block a scan; RX filter differences (DROP_UC_NOME, DROP_BA, DROP_BAR) do not affect beacons [IP:RT2x00, I] | M:s wl_wifi_sta.a |
| 13 | **Open; §6.9 now gives both coefficient sets and the curve (the GPL writes a rising ramp and the thresholds at the wrong entries; §15 T10).** Window D CFR mode 1 and RAPP | §6.9 values (tap counts 15/15/0, per-rate gains, RAPP curve 511 → 271, thresholds 160/160/400, mode 2) | A18, A19 | TX spectral quality and power; not expected to change "never queued" [I] | M:s wl_rf_common.a |
| 14 | **Open (the GPL sends TXOP 0, density 5, PacketId 0; §15 T11).** Management TXWI W0 TXOP | 3 | every management TX | Unlikely to matter: the vendor's QoS-null and data TXWIs use TXOP 0 and are sent [M:hw] | M:s wl_wifi_sta.a, M:hw |
| 15 | **Open (the GPL makes one write of `0x00010E37`; §15 T12).** WDMA init | nine `WDMA_CTL` and nine `WDMA_CFG0` read-modify-writes before any MAC reset; `WDMA_CTL` bit 16 never written | A5-A7 | Unlikely to matter (final values equal; bit 16 is read-only) | M:s wl_rf_common.a, M:hw |
| 16 | **Done 12o [M:hw]: the handler masks `0x14050` bit 0 and touches no MAC register; a handler that re-armed MAC+`0x328` plus an unbounded drain stalled the CPU until the watchdog (12n).** Interrupt handler | clears `WF_CON4` bit 0 and wakes the receive task only; no access to MAC+`0x328` | after B13 | None until a frame arrives; avoids disturbing the receive-notify source | M:s wl_rf_common.a |
| 17 | **Implemented 2026-11-06 (§15 T20 is the check).** Station slot 1 and the BSSID registers before the authentication request | MAC+`0x1808`/`0x180C` = access point; `0x1010`/`0x1014` = BSSID | join (D4-D5) | none expected for the ACK itself [I]; required for the key install (slot 1 attribute and IV, D26) and for the hardware to decrypt unicast data | M:s wl_wifi_sta.a |
| 18 | **Open (diff at HEAD; §15 T21).** Unicast management TXWI and header | TXOP 3, density 0, PacketId 0, duration 304 µs; a deauthentication before the authentication | D6-D7 (§7.11.5) | if the access point then acknowledges (TX_STA_CNT1 [15:0] = 1), the frame format was the cause | M:s wl_wifi_sta.a |
| 19 | **Open (§15 T19).** Transmit decodability: no frame addressed to the GPL driver's own address has ever been received (no probe response, no ACK) [M:hw 2026-11-05]; the address the access point answered belongs to another station (§7.13.1) | TX back end (A18, A19, §6.9) and the §7.11.5 differences | before any Phase D work | a probe response to a directed probe request | M:hw, PUB (OUI registry), I |

**Not worth trying** (no vendor counterpart) [M:s]: writing the four indicator bits, writing
`0x14054`, `0x14058` or `0x14060`, any write to `0x40000`-`0x40FFF`, any write to BBP R32-R63 or
R94, an MCU command or firmware download, polling an indicator as a start condition.

---

## 15. Hardware tests for the implementer (2026-11-04)

Each test is a hardware experiment: prerequisites, register actions, what to record, and how to
read the result. The implementer designs the code and the logging; nothing here is a source
change recipe. Vendor-side values to compare against are in §4.2, §4.3, §6.9 and §11.2.9 [M:hw];
where a value is marked "vendor: not captured", the reader supplies it from the vendor unit (the
implementer never runs the vendor build). Record every run as an observation file with the
decision taken, the build id, and the values read; a result is "confirmed" only when the effect
reproduces against a baseline with one variable changed, "confirmed in combination" when several
changes were made at once, "collected" when only values were read.

Common to every test: sample the four indicators (`0x14050` bit 23, `0x14060` bit 23, `0x14058`
[23:16] and [7:0], `0x30F04` bit 16) at the existing init boundaries and at the boundary the test
names; scan with a known access point on channel 6 unless stated; the driver state referred to as
"HEAD" is commit 76da132d (12p).

**T0 (no flash): read the 12l-12o logs.** Already decided: `0x3043C` [7:5] = 7 and `0x30434`
bit 18 = 0 before TX (gate passes), queue 2 shows one page after the kick, `TX_STA_CNT1` counts a
frame, beacons decode on channels 3-11 [M:hw]. Remaining T0 decisions: none.

**T1 (regression, implemented at HEAD): plain boot never writes `0x14000`.** Boot with the clock
tree (§4.2.1 A1-A4) before the first MAC access and no write to `0x14000`; read `0x31000` right
after A3 (expect `0x00300100`, no fault) and `0x14000` at every boundary (expect 0); then the normal
init and a scan from the panel without any host probe. Expect the 12o readings. Decision: no
fault → record [M:hw] that the 2026-10-28 fault came from a MAC read before the clock tree and
keep this as the base of every later test; fault → report the boundary. The plain-boot scan result
is not in the observation record at the evidence cutoff: run this first.

**T2 (superseded by T3, implemented at HEAD).** Kept for the record: OR a constant bank `0x35`
into word 1 bits [15:9] of every FEQ entry and compare the indicators and `RX_STA_CNT0` during a
dwell. Not needed now that T3 is in.

**T3 (implemented at HEAD, result to record): bank-limit table.** Build the 48-entry table of §5.2
during the RF init in the 24 MHz PLL state with `0x11950` = 0 and `0x11938` bits 26/27 clear, patch
every FEQ entry, and log the top bank, the 48 limits and lookup(121). Recorded so far: top 47,
lookup(121) = 43 (vendor 53), limits 71..169 [M:hw 12n]; a scan with the 40 MHz words gives a flat
table (top 15, every limit at the ceiling) [M:hw 12m]. Open: explain the 10-bank difference
(§16 U11). A selective reversal (the 12n table against a flat one, everything else equal) would
turn "confirmed in combination" into "confirmed" for §14 row 6.

**T4 (§14 row 7): no reset after auto control.** Order the end of the init as §4.2.1 A17-A23 (PHY
init #2, TX filter mode 1, compression curve, LUT load, auto control, power, channel) with no
MAC_SYS_CTRL = 3, MAC+`0x30C` pulse, R21 pulse or PHY init after the LUT load. Sample the
indicators after A20, A21, A23 and after every later init stage. Expect an indicator to assert and
stay. Decision: asserts and stays → row 7 confirmed; asserts then drops at a later stage → that
stage is the disturbing step; never asserts → ordering is not the condition (and §16 U7 stays open).
HEAD approximates this by repeating LUT load + auto control after the MAC init; the vendor order is
the cleaner experiment.

**T5 (implemented at HEAD, per-switch reads to log): channel code only.** A channel switch is one
read-modify-write of `0x1405C` [7:0] = channel + 101, no delay, no BBP or PLL write. After each
switch read `0x1405C` (expect channel + 101), BBP R106 (expect 4), `0x1193C` and `0x11938` (expect
unchanged: 121 and the init bank), `0x14058`. Scan channels 1-13 with the access point on a channel
other than 6. Recorded: frames on channels 3-11 with no PLL write [M:hw 12o]. Decision: already
"confirmed in combination"; log the per-switch reads once to close §14 row 5 fully.

**T6 (§14 row 8): stored record and 24 MHz calibration.** Requires §10.2 (now present). Two parts
in one flash: (a) read the 68-byte record 187 from the store and log the validity check of §10.2
and the raw bytes; (b) if valid, apply the quick path of §10.2 (including D'[9..`0x0C`] and the
four RX DC pairs into the LUT AGC words) instead of a full trim; if invalid, run the full trim of
§10.1 with the 24 MHz crystal type `0x0B`, N = 101, fraction `0xC00000`, `0x11954` =
`0x02822520` (no 40 MHz words anywhere), and remove the forced vendor read-backs of the unit
results (§11.2.9 marks them). Read BBP R32-R46 and R4 after init. Expect R32-R46 near the vendor
(`0x0B 0x0E 0x1F 0x04 0x09 0x38 0x07 0xBA 0x0F 0xC4 0x01`), R4 = 0, `RX_STA_CNT0` non-zero during
a dwell, and the §10.1.6 "after trim" attenuation values when the full trim ran. Decision: valid
record → the quick path is the vendor path on this unit, keep it; invalid → the 24 MHz full trim
becomes the default; no change in reception either way → "no effect observed under these
conditions" (the forced read-backs were already this unit's results).

**T7 (§14 row 11): MAC_SYS_CTRL never 0 or 4.** Remove every write of 0 and 4; the last write is
`0xC` at the end of the PHY init. Read MAC_SYS_CTRL at every boundary (expect `0xC` from the PHY
init on). Decision: any change in `TXRXQ_PCNT` / `TX_STA_CNT1` behaviour → record; none → keep the
vendor behaviour.

**T8 (§14 row 9): WCID, attribute and key tables, beacon sync off.** At interface up write
MAC+`0x1800` + 8i = `0xFFFFFFFF` then `0x00FFFFFF` for i = 0..253, MAC+`0x6800` + 4i = 1 for i =
0..255, MAC+`0x7000`..`0x700C` = 0, MAC+`0x1114` `&= ~0x001F0000`. Read back `0x31800`/`0x31804`,
`0x31FE8`/`0x31FEC`, `0x36800`, `0x31114`. Note that the vendor never writes entries 254 and 255
and never uses WCID `0xFF` as a table index (§4.2.1 B3): keep both readings open (special "no
station" selector vs ordinary index) until measured. Decision: observe `TX_STA_FIFO` validity for
WCID `0xFF` frames and `TX_STA_CNT0`; no conclusion without a queued frame.

**T9 (§14 row 12): MAC table as the vendor writes it.** The 22 values of §4.2.1 B2, no write to
MAC+`0x1200` or MAC+`0x137C`, RX_FILTER_CFG `0x17F97`, EDCA as §7.10's interface-up set. Read back
`0x31200` (vendor `0x00020603` / `0x00030001`: hardware busy bits) and `0x31400`. Expect no change
in scan results. Decision: change → report; none → keep the vendor values.

**T10 (§14 row 13): TX filter mode 1 and the compression curve.** Requires §6.9 (now complete).
After PHY init #2: the 57 mode-1 writes, then the curve load and thresholds/powers of §6.9 with
mode 0 before and mode 2 after. Read back D[`0x4D`..`0x4F`] (expect 15, 15, 0), the odd entries
D[`0x0B`..`0x21`] (expect the mode-1 gains, or the per-rate defaults if record 108 is
applied) and D[`0x51`..`0x5E`] (expect `BE 64 2D 64 2D D6 3F 40 A0 A0 90 E0 7F 0F`). Expect a TX
power/spectrum change only (sniffer RSSI). The vendor unit's read-back already matches these
finals [M:hw 2026-11-03]; the test confirms the GPL load reproduces them.

**T11 (§14 row 14): management TXWI as the vendor builds it.** TXOP [9:8] = 3, MPDU density 0,
BA window 7, PacketId = MCS[3:0], NSEQ 0, WCID `0xFF`, ACK 0 for broadcast (§7.11.5). Expect no
change unless TX is queued but never completes. Also test the probe rate: OFDM 6 Mbps (PHYMODE 1,
MCS 0) against CCK 1 Mbps; both should be accepted (§16 U8).

**T12 (§14 row 15): WDMA init as nine and nine read-modify-writes** directly after the clock
tree, before any MAC reset; never write bit 16. Log `WDMA_CTL` after each step; expect final `0x37`,
bit 9 reads 0 after being set, bit 16 = 0 (status). Low yield; keep.

**T13 (implemented at HEAD 12o): interrupt handler.** The handler clears `0x14050` bit 0 and
flags; no MAC+`0x328` access; the receive path re-opens bit 0 after draining. Remaining check:
read MAC+`0x328` at each boundary (expect 2 after B13) and find out why interrupt 52 never fires
on modd although the line is registered and bit 0 is set (§16 U4): try registering the line
after MAC+`0x328` = 2 as the vendor does, and compare the interrupt controller's enable/priority
registers for line 52 against the header's semantics.

**T14 (mutating, piggyback on any flash): window C' and D' contents.** After init read all 72 C'
entries and D' entries 0..`0x1C` through their read-back registers (poll the port busy bit before
each read). Expect the §6.6 values with C'[`0x3C`] = 1 and D'[9..`0x0C`] = calibration results.
Then write one C' entry, pulse R21 bit 0, read it back (expect preserved). Mismatch → the window
write path or a reset clears windows; report entries.

**T15 (mutating, piggyback): acknowledge semantics.** After one TX: read `WDMA_CTL` bit 20
(expect 1), write bit 10, read again (expect 0); the same for bit 21 / bit 11 once a frame arrived.
Records [M:hw] whether bits 10/11 are write-1-to-clear for 20/21 (§7.11.3 infers it from the
vendor's poll order).

**T16 (piggyback): front-end register reset values.** Before the first channel switch read
MAC+`0x1328` and MAC+`0x132C` (vendor: not captured; the reset value of `0x132C` bit 0 is §16 U1);
after each switch expect `0x00050302` and bit 2 set, bits [2:1] = 10.

**T17 (settled by the reader, no flash needed): PBF_MAX_PCNT.** The running vendor reads
`0x1F3F7F9F` because the association path writes it (TX burst off); the hardware retains bit 15
(§7.10). Use byte 1 = `0x7F` as the queue-2 page limit after association and `0xBF` before.

**T18 (dropped).** The direct divider write no longer exists at HEAD.

**Phase D tests (added 2026-11-05; T17 and T18 were already taken, so these start at T19). Order:
T19 first (it decides whether any Phase D register matters yet), then T8, T20, T21, T22, T23, T24,
T25; T26 last.**

**T19 (first; no association needed): are the GPL driver's transmissions decodable?** On the
channel of a known access point, with RX_FILTER_CFG bit 2 clear, send directed probe requests
(addr1 = addr3 = the access point, its SSID in the element) once per second, alternately at CCK
1 Mbps and OFDM 6 Mbps (PHYMODE 1, MCS 0), and count, within 50 ms of each request, probe
responses whose addr1 equals the driver's own address; log beacons from the same access point as
the control, and TX_STA_CNT0/1 after each request (a directed probe request with the ACK bit set is
acknowledged by the access point). Decision: responses or an acknowledgement at either rate → the
transmissions are decodable, continue with T8/T20/T21; none at both rates with beacons arriving →
the transmit chain is the blocker: compare the window D state of the running build with §6.9
(T10) and the TX gain fields of §4.2.1 A18/A24 before any Phase D register work. Record the rate at
which responses appear (§16 U8, U21).

**T8** as given above (tables as B3/B5/B6), run before T20.

**T20 (§14 row 17): join-time writes.** At connect, before the authentication request, write slot 1
(MAC+`0x1808` = BSSID bytes 0-3, `0x180C` = bytes 4-5, [31:16] = 0) and the BSSID registers (D4-D5);
read back `0x31808`/`0x3180C` and `0x31010`/`0x31014`. Decision: no behavioural change is expected
for the ACK; the slot is needed by T24.

**T21 (§14 row 18): unicast management frames as the vendor builds them.** One variable per flash,
in this order: (a) TXWI TXOP [9:8] = 3 and density 0 for the authentication request; (b) header
duration = 304 µs for unicast management frames at CCK 1 Mbps (50 at OFDM 6 Mbps); (c) a
deauthentication (reason 3) to the access point immediately before the authentication request;
(d) authentication timeout 800 ms with one repeat, association timeout 300 ms. After each flash read
TX_STA_CNT0 [15:0] and TX_STA_CNT1 after the authentication request. Decision: TX_STA_CNT1 [15:0]
= 1 → that variable was the cause; record it in §14 row 18.

**T22 (§16 U15): TX status for packet id 0.** Send the authentication request with TXWI PacketId
[31:28] = 1 (then 0 as the baseline) and poll TX_STA_FIFO bit 0 within 20 ms. Decision: an entry
appears only with packet id 1 → the hardware suppresses reports for packet id 0 (and the vendor's
management frames are therefore never reported); entries in both cases → U15 closed.

**T23 (D11-D21): link-up set.** After the association response write D12-D21 (EDCA from the
beacon's WMM set, protection, basic rates with the 6/12/24 Mbps rule, fallback tables,
MAX_LEN_CFG from the A-MPDU factor, PBF_MAX_PCNT `0x1F3F7F9F`, BCN_TIME_CFG with the beacon
interval and `0x0B` in [20:16], RX_PARSER_CFG). Read back `0x31114`, `0x31408`, `0x31018`,
`0x3040C`, `0x31418`, `0x31354`-`0x31360`, `0x31300`-`0x3130C`; expect the §4.2.1 D values and
the running vendor's steady state (§7.10). Decision: TSF ticking (`0x3110C`/`0x31110` advance)
and beacon reception unchanged → set kept; any regression → report the register.

**T24 (D26-D27): hardware key install.** After message 4/4 is handed to the MAC: shared-key slot
(0, 0) with the TK (16 bytes, 8 + 8 MIC bytes as supplied), SHARED_KEY_MODE [3:0] = 4, IV/EIV of
slot 1 = `0x20000001`/0, attribute of slot 1 = `0x8`; the group key from the GTK element into slot
(0, id) with its nibble. Read back `0x36C00`-`0x36C1F` (expect the key bytes in little-endian words),
`0x37000`, `0x36008`/`0x3600C`, `0x36804`. Then send one unicast data frame with the TXWI of
§7.11.5 (data, WCID 1, WIV 0) and no software encryption: expect the access point to acknowledge
it and to answer a ping; expect received unicast data to arrive decrypted (the RX trailer's
decrypted/cipher-error bits of §7.11.6). Decision: both work → software CCMP retired; TX works, RX
does not → compare the attribute bits [9:7] (the Linux driver writes the cipher there, the vendor
does not); nothing → keep software CCMP and report.

**T25 (§7.13.1): the station address.** Drop the override and the random fallback; derive a
stable locally-administered address from the flash unique id (the SDK rule is citable). Ask
the router's client list which device carries `D8:C8:0C:xx:xx:xx`. Decision: a Tuya device → §14
row 19's address part is closed; nothing → report (the registry assignment stands either way).

**T26 (D31, optional): link-down writes.** On disconnect perform D31 and read back `0x31114`,
`0x31018`, `0x36804`, `0x37000`; keep the GPL's BSSID clearing and RX filter restore as its own
choice (the vendor does neither).

**Longer observations (not one flash):** (a) an indicator timeline over a vendor-order build
(T1 + T4 + T5 + T6) to find the first boundary where each indicator asserts (§16 U7); (b) a full
register sweep (`0x11900`-`0x119AF`, `0x14000`-`0x140FF`, `0x30000`-`0x31FFF`, BBP R0-R127 and all
four windows) of the GPL build against the vendor unit's sweep, which the reader captures; (c) the
elapsed time of the clock-tree, PLL-restart and loop-pulse delays measured with the cycle counter
(§16 U5).

---

## 16. Unspecified steps, unknowns and housekeeping (2026-11-04)

**Supported profile of this revision:** FM-1 board, 24 MHz crystal, WiFi-only cold start, station
mode: scan, authenticate, associate, data. Transitions outside it are not characterised and are
excluded until measured: warm restart of the WiFi block, repeated start/stop, sleep/wake (the
sleep registers are listed in §7.11.9 as not on the start path), and a Bluetooth start before or
alongside WiFi (U9).

**16.1 Steps not yet specified.** As of this revision every step of §4.2.1 A0-C8 and D1-D32, §11.2.1 1-16,
§10.1 and §10.2 has its registers, values (or generating rule) and order in this sheet. No step is
blocked. Rows are added here, with a stable id, if a later pass finds a gap; resolved rows stay
with their resolution.

**16.2 Unknowns.** Each has a stable id; "blocking" means a feature of the supported profile
cannot be claimed until it is resolved; "optional" means the profile works without it.

| Id | Unknown | Class | Cheapest route |
|----|---------|-------|----------------|
| U1 | Reset value of MAC+`0x132C` bit 0 (HT 40 MHz select); the vendor never writes it in 20 MHz operation | optional | T16 |
| U2 | Meaning of MAC+`0x434` bit (8q + 2) and MAC+`0x43C` [7:5]; values collected (`0x22020602`, `0x8400A0FE`) [M:hw 12l] | optional | longer observation (b) |
| U3 | Whether the compression curve RAM can be read back (the vendor never reads it; D[`0x5E`] returns the last data byte written [M:hw]) | optional | T10 variant: set D[`0x5D`] to an address and read D[`0x5E`] |
| U4 | Why interrupt 52 never fires on modd (line registered, `0x14050` bit 0 set; polling carries reception) [M:hw 12o] | optional (polling works) | T13 |
| U5 | Real durations of the vendor's NOP-loop delays (§7.11.9): the nominal 1/1/1/50, 5/5/100, 80 and 1000 µs are approximate on the vendor too | optional | longer observation (c) |
| U6 | Cache coherency of the WDMA buffers: **narrowed 2026-11-05** (§7.13.6): the vendor keeps both buffers in internal RAM and does no cache maintenance; the cache model gives SDRAM an uncached alias and internal RAM none. Closed for internal-RAM buffers (the GPL's case); an SDRAM-resident buffer would need line flush/invalidate [I] | closed for the supported profile | none |
| U7 | What asserts the four read-only indicators (§4.2) | optional | longer observation (a) |
| U8 | Whether the vendor's first probe request is sent at OFDM 6 Mbps or CCK 1 Mbps (profile-dependent, §7.11.5); the MAC accepts CCK 1 Mbps [M:hw] | optional | T11 |
| U9 | Coexistence arbiter programming (`0x14004` tables, `0x14000` bits 0-1) when BT runs too | blocking for BT + WiFi only | out of scope of this profile |
| U10 | Whether the TX IQ gain/phase fields accept the full ±423 / ±200 ranges: the vendor applies winners aliased to 8 bits (§10.1.9) | optional | T6 (full-trim run) with the winners logged |
| U11 | The GPL's bank table differs from the vendor's (43 vs 53 at divider 121) though reception works [M:hw 12n-12o] | optional | compare the 48 limits against the vendor unit's (reader capture) |
| U12 | Semantics of MAC+`0x308` = `0x9407`, MAC+`0x30C` [4:1], MAC+`0x324` = 2, MAC+`0x328` = 2 | optional | bit ablation after reception works |
| U13 | Whether the hardware drops bit 15 of `0x3040C`: no, software rewrites it (§7.10); closed 2026-11-04 | closed | |
| U14 | WCID `0xFF`: **narrowed 2026-11-05** (§7.13.2): a TXWI literal for frames sent without a station entry, including the vendor's acknowledged unicast management frames; slot 255 never written, attribute 255 = 1 with all others. Whether the hardware indexes the tables with `0xFF` is unmeasured and not needed for Phase D | optional | T8 |
| U15 | Whether TX_STA_FIFO reports frames queued with packet id 0 (the vendor's 1 Mbps management frames carry 0 and their entries, if any, are discarded; the Linux driver never uses 0) | optional | T22 |
| U16 | Meaning of MAC_STATUS_CFG `0x1200` bits [11:8] and [17:16] (the GPL reads `0x00030F00` after an erroneous write; no vendor access) | optional | none; stop writing it |
| U17 | RX_PARSER_CFG `0x1418`: reset value and effect; the vendor writes 1 only for an infrastructure WMM link with TKIP, 0 otherwise (D21), so a CCMP link gets 0 | optional | T23 read-back |
| U18 | Whether a queued EAPOL message 4/4 can still be encrypted by the MAC when the pairwise key is installed before it leaves (the vendor installs after the DMA hand-off without waiting for TX status) | optional | T24 ordering |
| U19 | Unit of TX_TIMEOUT_CFG [15:8] (RX ACK timeout 18; "slot" per the Linux field name) | optional | none needed |
| U20 | The stock FM-1 firmware's WiFi address source (a stored record, the SDK generator, or another rule); the two known sources are in §7.13.1 | optional | router client list; stock firmware record dump |
| U21 | Why no frame addressed to the GPL driver's own address has ever been received (no probe response over every scan, no ACK). **Root cause identified 2026-11-06** (§10.1.14): `0x11900` never leaves RX mode during a transmit, so the RF analog TX path (PA, DAC, TX mixer) stays off. Required order (SPECSHEET §4.2.1 A16, A20, A21): the RF mode write (A16) precedes the auto-control enable (A21); a mode write made after it clears the G4 enable bits in `0x11924` and `0x11938` that the auto-switch needs. `0x11950` = 0x57BFFFDC is correct. | **blocking** (association) | T27: with the A16-before-A21 order in place, confirm that `0x11900` leaves RX mode during a transmit |

**16.3 Housekeeping for the implementer.** Closed 2026-11-06. The stale `§` citations listed in
the 2026-11-04 revision were corrected; the identifiers that carried vendor vocabulary in
`wifi_rf_cal.c`, `wifi_rf.c`, `wifi_rf_lut_data.c` and `wifi_sdio.c` were replaced by names from
this sheet's vocabulary; the fm1-base references were removed; the verbatim AGC table was replaced
by the §6.7 rule; the forced unit read-backs were retired (T6); the tests that depended on reader
material were removed. The terms RAPP (TX soft compression) and CFR (TX filter) are defined in
§6.9 and may stay; the SDK register names (`WF_CONn`, `WL_CONn`, `WLA_CONn`) are public [PUB]. The
identifier list itself is reader-side (CLEAN_ROOM_METHODOLOGY.md, "Vendor identifiers").
