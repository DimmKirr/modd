# Felucca UI Navigation (Upstream)

Baseline reference for the original Felucca firmware UI.
Source: `firmware/upstream/felucca/firmware/src/` — `ui.c`, `ui_input.c`,
`ui_layer.c`, `params.c`, `panel.c`.

- **Solid arrows** (`-->`) = button tap navigation
- **Dashed arrows** (`-.->`) = button hold / layer open
- **Dotted bi-arrows** (`<-..->`) = pages within a family (SELECT PAGE or tap-cycling)

<!-- ui-nav-diagram:start -->
```mermaid

```
<!-- ui-nav-diagram:end -->

## Physical controls

14 buttons, 7 encoders, 24 piano keys.

| ID | Label | Matrix default |
|----|-------|----------------|
| B_FX | FX | 2 |
| B_SCL | SCL | 3 |
| B_ENV | ENV | 4 |
| B_LFO | LFO | 5 |
| B_EDIT | EDIT | 6 |
| B_GLO | GLO | 7 |
| B_HOME | HOME | 8 |
| B_SAVE | SAVE | 9 |
| B_ARP | ARP | 10 |
| B_SEQ | SEQ | 11 |
| B_PLAY | PLAY | 12 |
| B_REC | REC | 13 |
| B_OCTDN | OCT- | 0 |
| B_OCTUP | OCT+ | 1 |

| ID | Label | Matrix default |
|----|-------|----------------|
| EN_SELECT | SELECT | enc 0 |
| EN_ALGO | ALGORITHM | enc 1 |
| EN_PRESET | PRESETS | enc 6 |
| EN_K1 | KNOB 1 | enc 2 |
| EN_K2 | KNOB 2 | enc 3 |
| EN_K3 | KNOB 3 | enc 4 |
| EN_K4 | KNOB 4 | enc 5 |

The mapping is overridable by HARDWARE CALIBRATION (hold OCT- + OCT+ at boot).

<!-- ui-controls-table:start -->
## Global controls

Always active, handled by the shell or keyboard overlay:

| Control | Action |
|---------|--------|
| Presets (enc) | Cycle presets |
| Master (pot) | Master volume |
| OCT+ / OCT- | Octave shift |
| PLAY | Play/stop (page-specific on WIFI, WEATHER, BLE) |
| REC | Record toggle |
| Select (enc) | Cycle pages within current view (when no page claims it) |

## Controls per page

| Page | Select | Algo | Knob 1 | Knob 2 | Knob 3 | Knob 4 | Buttons |
|------|--------|------|--------|--------|--------|--------|---------|
| [ENV](../../../../../firmware/upstream/felucca/firmware/src/params.c#L458) | - | - | [ATK](../../../../../firmware/upstream/felucca/firmware/src/params.c#L458) | [DEC](../../../../../firmware/upstream/felucca/firmware/src/params.c#L458) | [SUS](../../../../../firmware/upstream/felucca/firmware/src/params.c#L458) | [REL](../../../../../firmware/upstream/felucca/firmware/src/params.c#L458) | - |
| [ENV DEST](../../../../../firmware/upstream/felucca/firmware/src/params.c#L459) | - | - | [ED_FLT](../../../../../firmware/upstream/felucca/firmware/src/params.c#L459) | [ED_PIT](../../../../../firmware/upstream/felucca/firmware/src/params.c#L459) | [ED_SHP](../../../../../firmware/upstream/felucca/firmware/src/params.c#L459) | [ESYNC](../../../../../firmware/upstream/felucca/firmware/src/params.c#L459) | - |
| [LFO](../../../../../firmware/upstream/felucca/firmware/src/params.c#L461) | - | - | [LRATE](../../../../../firmware/upstream/felucca/firmware/src/params.c#L461) | [LWAVE](../../../../../firmware/upstream/felucca/firmware/src/params.c#L461) | [LPHASE](../../../../../firmware/upstream/felucca/firmware/src/params.c#L461) | [LFADE](../../../../../firmware/upstream/felucca/firmware/src/params.c#L461) | - |
| [LFO 2](../../../../../firmware/upstream/felucca/firmware/src/params.c#L462) | - | - | [LSYNC](../../../../../firmware/upstream/felucca/firmware/src/params.c#L462) | [LTRIG](../../../../../firmware/upstream/felucca/firmware/src/params.c#L462) | [LPOL](../../../../../firmware/upstream/felucca/firmware/src/params.c#L462) | - | - |
| [LFO DEST](../../../../../firmware/upstream/felucca/firmware/src/params.c#L463) | - | - | [LD_PIT](../../../../../firmware/upstream/felucca/firmware/src/params.c#L463) | [LD_FLT](../../../../../firmware/upstream/felucca/firmware/src/params.c#L463) | [LD_SHP](../../../../../firmware/upstream/felucca/firmware/src/params.c#L463) | [LD_AMP](../../../../../firmware/upstream/felucca/firmware/src/params.c#L463) | - |
| [MOD](../../../../../firmware/upstream/felucca/firmware/src/params.c#L464) | - | - | - | [M1SRC](../../../../../firmware/upstream/felucca/firmware/src/params.c#L464) | [M1DST](../../../../../firmware/upstream/felucca/firmware/src/params.c#L464) | [M1AMT](../../../../../firmware/upstream/felucca/firmware/src/params.c#L464) | - |
| [FX](../../../../../firmware/upstream/felucca/firmware/src/params.c#L465) | - | - | [DIST](../../../../../firmware/upstream/felucca/firmware/src/params.c#L465) | [CHOR](../../../../../firmware/upstream/felucca/firmware/src/params.c#L465) | [DLY](../../../../../firmware/upstream/felucca/firmware/src/params.c#L465) | [REV](../../../../../firmware/upstream/felucca/firmware/src/params.c#L465) | - |
| [SLICER](../../../../../firmware/upstream/felucca/firmware/src/params.c#L466) | - | - | [SLCR](../../../../../firmware/upstream/felucca/firmware/src/params.c#L466) | [SLPAT](../../../../../firmware/upstream/felucca/firmware/src/params.c#L466) | [SLRATE](../../../../../firmware/upstream/felucca/firmware/src/params.c#L466) | [SLDEPTH](../../../../../firmware/upstream/felucca/firmware/src/params.c#L466) | - |
| [INSERT](../../../../../firmware/upstream/felucca/firmware/src/params.c#L467) | - | - | [ITYPE](../../../../../firmware/upstream/felucca/firmware/src/params.c#L467) | [IA](../../../../../firmware/upstream/felucca/firmware/src/params.c#L467) | [IB](../../../../../firmware/upstream/felucca/firmware/src/params.c#L467) | [IC](../../../../../firmware/upstream/felucca/firmware/src/params.c#L467) | - |
| [INSERT 2](../../../../../firmware/upstream/felucca/firmware/src/params.c#L468) | - | - | [IMIX](../../../../../firmware/upstream/felucca/firmware/src/params.c#L468) | - | - | - | - |
| [DLY](../../../../../firmware/upstream/felucca/firmware/src/params.c#L469) | - | - | [DTIME](../../../../../firmware/upstream/felucca/firmware/src/params.c#L469) | [DFDBK](../../../../../firmware/upstream/felucca/firmware/src/params.c#L469) | [DCOLOR](../../../../../firmware/upstream/felucca/firmware/src/params.c#L469) | [DMIX](../../../../../firmware/upstream/felucca/firmware/src/params.c#L469) | - |
| [REVERB](../../../../../firmware/upstream/felucca/firmware/src/params.c#L470) | - | - | [RTYPE](../../../../../firmware/upstream/felucca/firmware/src/params.c#L470) | [RSIZE](../../../../../firmware/upstream/felucca/firmware/src/params.c#L470) | [RDAMP](../../../../../firmware/upstream/felucca/firmware/src/params.c#L470) | - | - |
| [CHORUS](../../../../../firmware/upstream/felucca/firmware/src/params.c#L471) | - | - | [CRATE](../../../../../firmware/upstream/felucca/firmware/src/params.c#L471) | [CDEPTH](../../../../../firmware/upstream/felucca/firmware/src/params.c#L471) | - | - | - |
| [SCL](../../../../../firmware/upstream/felucca/firmware/src/params.c#L472) | - | - | [ROOT](../../../../../firmware/upstream/felucca/firmware/src/params.c#L472) | [SCALE](../../../../../firmware/upstream/felucca/firmware/src/params.c#L472) | [QUANT](../../../../../firmware/upstream/felucca/firmware/src/params.c#L472) | [TRANS](../../../../../firmware/upstream/felucca/firmware/src/params.c#L472) | - |
| [CHORD](../../../../../firmware/upstream/felucca/firmware/src/params.c#L473) | - | - | [CHRD](../../../../../firmware/upstream/felucca/firmware/src/params.c#L473) | [VOIC](../../../../../firmware/upstream/felucca/firmware/src/params.c#L473) | - | - | - |
| [EDIT 1](../../../../../firmware/upstream/felucca/firmware/src/params.c#L474) | - | - | [E0](../../../../../firmware/upstream/felucca/firmware/src/params.c#L474) | [E1](../../../../../firmware/upstream/felucca/firmware/src/params.c#L474) | [E2](../../../../../firmware/upstream/felucca/firmware/src/params.c#L474) | [E3](../../../../../firmware/upstream/felucca/firmware/src/params.c#L474) | - |
| [EDIT 2](../../../../../firmware/upstream/felucca/firmware/src/params.c#L475) | - | - | [E4](../../../../../firmware/upstream/felucca/firmware/src/params.c#L475) | [E5](../../../../../firmware/upstream/felucca/firmware/src/params.c#L475) | [E6](../../../../../firmware/upstream/felucca/firmware/src/params.c#L475) | [E7](../../../../../firmware/upstream/felucca/firmware/src/params.c#L475) | - |
| [FILTER](../../../../../firmware/upstream/felucca/firmware/src/params.c#L476) | - | - | [FTYPE](../../../../../firmware/upstream/felucca/firmware/src/params.c#L476) | [E4](../../../../../firmware/upstream/felucca/firmware/src/params.c#L476) | [E5](../../../../../firmware/upstream/felucca/firmware/src/params.c#L476) | [E7](../../../../../firmware/upstream/felucca/firmware/src/params.c#L476) | - |
| [LANES](../../../../../firmware/upstream/felucca/firmware/src/params.c#L477) | - | - | [LN0](../../../../../firmware/upstream/felucca/firmware/src/params.c#L477) | [LN1](../../../../../firmware/upstream/felucca/firmware/src/params.c#L477) | [LN2](../../../../../firmware/upstream/felucca/firmware/src/params.c#L477) | [LN3](../../../../../firmware/upstream/felucca/firmware/src/params.c#L477) | - |
| [LANES 2](../../../../../firmware/upstream/felucca/firmware/src/params.c#L478) | - | - | [LN4](../../../../../firmware/upstream/felucca/firmware/src/params.c#L478) | [LN5](../../../../../firmware/upstream/felucca/firmware/src/params.c#L478) | [LN6](../../../../../firmware/upstream/felucca/firmware/src/params.c#L478) | [LN7](../../../../../firmware/upstream/felucca/firmware/src/params.c#L478) | - |
| [SLICES](../../../../../firmware/upstream/felucca/firmware/src/params.c#L479) | - | - | - | - | - | - | - |
| [OPERATOR](../../../../../firmware/upstream/felucca/firmware/src/params.c#L480) | - | - | - | - | - | - | - |
| [OP ENV](../../../../../firmware/upstream/felucca/firmware/src/params.c#L481) | - | - | - | - | - | - | - |
| [OPERATOR 2](../../../../../firmware/upstream/felucca/firmware/src/params.c#L482) | - | - | - | - | - | - | - |
| [OP1 ENV](../../../../../firmware/upstream/felucca/firmware/src/params.c#L483) | - | - | [FM1_ATK](../../../../../firmware/upstream/felucca/firmware/src/params.c#L483) | [FM1_DEC](../../../../../firmware/upstream/felucca/firmware/src/params.c#L483) | [FM1_SUS](../../../../../firmware/upstream/felucca/firmware/src/params.c#L483) | [FM1_REL](../../../../../firmware/upstream/felucca/firmware/src/params.c#L483) | - |
| [OP2 ENV](../../../../../firmware/upstream/felucca/firmware/src/params.c#L484) | - | - | [FM2_ATK](../../../../../firmware/upstream/felucca/firmware/src/params.c#L484) | [FM2_DEC](../../../../../firmware/upstream/felucca/firmware/src/params.c#L484) | [FM2_SUS](../../../../../firmware/upstream/felucca/firmware/src/params.c#L484) | [FM2_REL](../../../../../firmware/upstream/felucca/firmware/src/params.c#L484) | - |
| [OP3 ENV](../../../../../firmware/upstream/felucca/firmware/src/params.c#L485) | - | - | [FM3_ATK](../../../../../firmware/upstream/felucca/firmware/src/params.c#L485) | [FM3_DEC](../../../../../firmware/upstream/felucca/firmware/src/params.c#L485) | [FM3_SUS](../../../../../firmware/upstream/felucca/firmware/src/params.c#L485) | [FM3_REL](../../../../../firmware/upstream/felucca/firmware/src/params.c#L485) | - |
| [OP4 ENV](../../../../../firmware/upstream/felucca/firmware/src/params.c#L486) | - | - | [FM4_ATK](../../../../../firmware/upstream/felucca/firmware/src/params.c#L486) | [FM4_DEC](../../../../../firmware/upstream/felucca/firmware/src/params.c#L486) | [FM4_SUS](../../../../../firmware/upstream/felucca/firmware/src/params.c#L486) | [FM4_REL](../../../../../firmware/upstream/felucca/firmware/src/params.c#L486) | - |
| [OP LEVEL](../../../../../firmware/upstream/felucca/firmware/src/params.c#L487) | - | - | [FM1_LEVEL](../../../../../firmware/upstream/felucca/firmware/src/params.c#L487) | [FM2_LEVEL](../../../../../firmware/upstream/felucca/firmware/src/params.c#L487) | [FM3_LEVEL](../../../../../firmware/upstream/felucca/firmware/src/params.c#L487) | [FM4_LEVEL](../../../../../firmware/upstream/felucca/firmware/src/params.c#L487) | - |
| [VOICE](../../../../../firmware/upstream/felucca/firmware/src/params.c#L488) | - | - | [VOICE](../../../../../firmware/upstream/felucca/firmware/src/params.c#L488) | [GLIDE](../../../../../firmware/upstream/felucca/firmware/src/params.c#L488) | [GLMODE](../../../../../firmware/upstream/felucca/firmware/src/params.c#L488) | [PRIO](../../../../../firmware/upstream/felucca/firmware/src/params.c#L488) | - |
| [VOICE 2](../../../../../firmware/upstream/felucca/firmware/src/params.c#L489) | - | - | [ALLOC](../../../../../firmware/upstream/felucca/firmware/src/params.c#L489) | [DETUNE](../../../../../firmware/upstream/felucca/firmware/src/params.c#L489) | [PAN](../../../../../firmware/upstream/felucca/firmware/src/params.c#L489) | [MUTE](../../../../../firmware/upstream/felucca/firmware/src/params.c#L489) | - |
| [VOICE 3](../../../../../firmware/upstream/felucca/firmware/src/params.c#L490) | - | - | [SPRD](../../../../../firmware/upstream/felucca/firmware/src/params.c#L490) | [ATSNS](../../../../../firmware/upstream/felucca/firmware/src/params.c#L490) | [ATDST](../../../../../firmware/upstream/felucca/firmware/src/params.c#L490) | [ATAMT](../../../../../firmware/upstream/felucca/firmware/src/params.c#L490) | - |
| [SONG](../../../../../firmware/upstream/felucca/firmware/src/params.c#L492) | - | - | - | - | - | - | - |
| [USER](../../../../../firmware/upstream/felucca/firmware/src/params.c#L495) | - | - | - | - | - | - | - |
| [PRESETS](../../../../../firmware/upstream/felucca/firmware/src/params.c#L496) | - | - | - | - | - | - | - |
| [PHRASES](../../../../../firmware/upstream/felucca/firmware/src/params.c#L497) | - | - | - | - | - | - | - |
| [PROJECT](../../../../../firmware/upstream/felucca/firmware/src/params.c#L499) | - | - | [SLOT](../../../../../firmware/upstream/felucca/firmware/src/params.c#L499) | - | [LOAD](../../../../../firmware/upstream/felucca/firmware/src/params.c#L499) | [SAVE](../../../../../firmware/upstream/felucca/firmware/src/params.c#L499) | - |
| [TOOLS](../../../../../firmware/upstream/felucca/firmware/src/params.c#L500) | - | - | [CLRSEQ](../../../../../firmware/upstream/felucca/firmware/src/params.c#L500) | [INITSND](../../../../../firmware/upstream/felucca/firmware/src/params.c#L500) | - | - | - |
| [ARP](../../../../../firmware/upstream/felucca/firmware/src/params.c#L501) | - | - | [AMODE](../../../../../firmware/upstream/felucca/firmware/src/params.c#L501) | [ARATE](../../../../../firmware/upstream/felucca/firmware/src/params.c#L501) | [AOCT](../../../../../firmware/upstream/felucca/firmware/src/params.c#L501) | [AGATE](../../../../../firmware/upstream/felucca/firmware/src/params.c#L501) | - |
| [ARP 2](../../../../../firmware/upstream/felucca/firmware/src/params.c#L502) | - | - | [ASWING](../../../../../firmware/upstream/felucca/firmware/src/params.c#L502) | [APROB](../../../../../firmware/upstream/felucca/firmware/src/params.c#L502) | [AHOLD](../../../../../firmware/upstream/felucca/firmware/src/params.c#L502) | [AORDER](../../../../../firmware/upstream/felucca/firmware/src/params.c#L502) | - |
| [STEP](../../../../../firmware/upstream/felucca/firmware/src/params.c#L503) | - | - | - | - | - | - | - |
| [DETAIL](../../../../../firmware/upstream/felucca/firmware/src/params.c#L504) | - | - | - | - | - | - | - |
| [AUTOMATION](../../../../../firmware/upstream/felucca/firmware/src/params.c#L506) | - | - | - | - | - | - | - |
| [MIXER](../../../../../firmware/upstream/felucca/firmware/src/params.c#L510) | - | - | - | - | - | - | - |
| [CLOCK](../../../../../firmware/upstream/felucca/firmware/src/params.c#L511) | - | - | [BPM](../../../../../firmware/upstream/felucca/firmware/src/params.c#L511) | [SWING](../../../../../firmware/upstream/felucca/firmware/src/params.c#L511) | [CLOCK](../../../../../firmware/upstream/felucca/firmware/src/params.c#L511) | - | - |
<!-- ui-controls-table:end -->
