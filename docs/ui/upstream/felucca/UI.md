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

## Global encoders

| Encoder | Default action | Modes | Source |
|---------|---------------|-------|--------|
| SELECT | BPM (adjust tempo 20-300) | SEL_BPM (default), SEL_PAGE, SEL_MODW, SEL_VEL — set in MENU | `ui_input.c:1805-1822` |
| ALGO | Track select (T1-T4, clamped) | — | `ui_input.c:1803-1804` |
| PRESET | Cycle all presets across engines | On STEP: cursor; on lists: K1 proxy | `ui_input.c:1801`, `ui.c:1664-1716` |
| MASTER (pot) | Master volume | — | hardware pot |

No page in upstream overrides SELECT or ALGO in `params.c`.

## Per-page knobs (K1-K4)

Every page has SELECT="-" and ALGO="-" (no page claims them).

### HOME family (HOME button cycles: HOME → MIXER → CLOCK → HOME)

| Page | K1 | K2 | K3 | K4 | Scope |
|------|----|----|----|----|----|
| HOME | engine knob 0 | engine knob 1 | engine knob 2 | engine knob 3 | TRACK |
| MIXER | P_LEVEL | P_PAN | P_REV | P_MUTE | SC_TRK (selected track) |
| CLOCK | G_BPM | G_SWING | G_CLOCK | - | GLOBAL |

### ENV family

| Page | K1 | K2 | K3 | K4 |
|------|----|----|----|----| 
| ENV | P_ATK | P_DEC | P_SUS | P_REL |
| ENV DEST | P_ED_FLT | P_ED_PIT | P_ED_SHP | P_ESYNC |

### LFO family

| Page | K1 | K2 | K3 | K4 |
|------|----|----|----|----| 
| LFO | P_LRATE | P_LWAVE | P_LPHASE | P_LFADE |
| LFO 2 | P_LSYNC | P_LTRIG | P_LPOL | - |
| LFO DEST | P_LD_PIT | P_LD_FLT | P_LD_SHP | P_LD_AMP |
| MOD | - (slot) | P_M1SRC | P_M1DST | P_M1AMT |

### FX family

| Page | K1 | K2 | K3 | K4 |
|------|----|----|----|----| 
| FX | P_DIST | P_CHOR | P_DLY | P_REV |
| SLICER | P_SLCR | P_SLPAT | P_SLRATE | P_SLDEPTH |
| INSERT | P_ITYPE | P_IA | P_IB | P_IC |
| INSERT 2 | P_IMIX | - | - | - |
| DLY | G_DTIME | G_DFDBK | G_DCOLOR | G_DMIX |
| REVERB | G_RTYPE | G_RSIZE | G_RDAMP | - |
| CHORUS | G_CRATE | G_CDEPTH | - | - |

### SCL family

| Page | K1 | K2 | K3 | K4 |
|------|----|----|----|----| 
| SCL | P_ROOT | P_SCALE | P_QUANT | P_TRANS |
| CHORD | P_CHRD | P_VOIC | - | - |

### EDIT family

| Page | K1 | K2 | K3 | K4 |
|------|----|----|----|----| 
| EDIT 1 | P_E0 | P_E1 | P_E2 | P_E3 |
| EDIT 2 | P_E4 | P_E5 | P_E6 | P_E7 |
| FILTER | P_FTYPE | P_E4 | P_E5 | P_E7 |
| VOICE | P_VOICE | P_GLIDE | P_GLMODE | P_PRIO |
| VOICE 2 | P_ALLOC | P_DETUNE | P_PAN | P_MUTE |
| VOICE 3 | P_SPRD | P_ATSNS | P_ATDST | P_ATAMT |
| LANES | P_LN0 | P_LN1 | P_LN2 | P_LN3 |
| LANES 2 | P_LN4 | P_LN5 | P_LN6 | P_LN7 |
| OP1 ENV | P_FM1_ATK | P_FM1_DEC | P_FM1_SUS | P_FM1_REL |
| OP2 ENV | P_FM2_ATK | P_FM2_DEC | P_FM2_SUS | P_FM2_REL |
| OP3 ENV | P_FM3_ATK | P_FM3_DEC | P_FM3_SUS | P_FM3_REL |
| OP4 ENV | P_FM4_ATK | P_FM4_DEC | P_FM4_SUS | P_FM4_REL |
| OP LEVEL | P_FM1_LVL | P_FM2_LVL | P_FM3_LVL | P_FM4_LVL |

### ARP family

| Page | K1 | K2 | K3 | K4 |
|------|----|----|----|----| 
| ARP | P_AMODE | P_ARATE | P_AOCT | P_AGATE |
| ARP 2 | P_ASWING | P_APROB | P_AHOLD | P_AORDER |

### SEQ family

| Page | K1 | K2 | K3 | K4 |
|------|----|----|----|----| 
| SEQ | - | - | - | - |
| STEP | - | - | - | - |
| DETAIL | - | - | - | - |
| AUTOMATION | - | - | - | - |

### SAVE family

| Page | K1 | K2 | K3 | K4 |
|------|----|----|----|----| 
| USER | - (browse) | - | - | - |
| PRESETS | - (browse) | - | - | - |
| PHRASES | - | - | - | - |
| PROJECT | P_SLOT | - | P_LOAD | P_SAVE |
| TOOLS | P_CLRSEQ | P_INITSND | - | - |

### GLO family

| Page | K1 | K2 | K3 | K4 |
|------|----|----|----|----| 
| SONG | - | - | - | - |

## Button map

### Tap

| Button | Action |
|--------|--------|
| HOME | Cycle HOME→MIXER→CLOCK→HOME; from other pages: go HOME |
| ENV | Open ENV family |
| LFO | Open LFO family |
| FX | Open FX family |
| SCL | Open SCL family |
| EDIT | Open EDIT family |
| GLO | Open GLO family (SONG page) |
| SAVE | Open SAVE family |
| ARP | Open ARP family |
| SEQ | Open SEQ family |
| PLAY | Play/stop transport |
| REC | Arm/disarm recording |
| OCT- | Octave down (min -3) |
| OCT+ | Octave up (max +3) |
| OCT- + OCT+ | Reset octave to 0 |

### Hold (layer overlays)

| Button held | K1-K4 become | Keys become | SELECT | ALGO |
|-------------|-------------|-------------|--------|------|
| FX | Perform macros | Effect assignment | — | consumed |
| GLO | T1-T4 LEVEL | — | BPM (always) | consumed |
| SCL | ROOT / SCALE / CHRD / VOIC | Root note selector | — | consumed |
| EDIT | Engine-specific | Engine selector | — | consumed |
| SEQ | P_SLEN / P_SDIV / P_SSWING / P_SGATE | Step tools | — | consumed |
| REC | Recording modes | Recording controls | — | consumed |
| HOME | Opens MENU (settings) | — | — | — |
| SAVE | UNDO (single undo_swap) | — | — | — |

## Matching status (MODD Felucca vs upstream)

### Matched

- SELECT = BPM globally
- ALGO = track select globally
- PRESET = preset cycle globally
- All per-page K1-K4 params (via PG_KNOB_CTL macro pages and hand-coded pages)
- MIXER: LEVEL/PAN/REV/MUTE for selected track
- No page claims SELECT or ALGO (except modd-only pages: SETTINGS, FILES, DEMO, DEBUG)
- Button taps match (family navigation)
- Button holds: HOME = MENU (Settings); SAVE = undo; GLO/SCL/SEQ/EDIT layer overlays remap K1-K4 while held or double-tap locked
- Layer overlays: GLO (T1-T4 LEVEL), SCL (ROOT/SCALE/CHRD/VOIC), SEQ (SLEN/SDIV/SSWING/SGATE), EDIT (engine/preset cycle)
- Double-tap a layer button to lock it open; tap again to unlock (300ms window)
- Key silencing while layer held; SCL keys set ROOT; GLO black keys toggle T1-T4 mute
- BPM LOCK setting (Settings > CONTROL): SELECT=BPM only works with GLO held
- HOME VIEW setting (Settings > CONTROL): KNOBS (engine knobs) or LEVELS (T1-T4 level)
- HOME K1-K4 = engine knobs (reads engine_t.knob[4]) or T1-T4 levels (HOME VIEW)
- SELECT mode: Settings > CONTROL > SELECT cycles BPM/PAGE/MODW/VEL (matches upstream MENU)
- FX layer K1-K4 adjust perf_k[0-3] (stub — no perform.c DSP yet)
- REC layer K1-K3 adjust click/count-in/level (stub — no click.c metronome yet)
- EDIT layer white keys select engine by position; GLO layer black keys toggle mute; SEQ key clears steps

### Intentional modd additions (no upstream equivalent)

| Item | MODD | Upstream | Reason |
|------|------|----------|--------|
| MOD K1 | SLOT selector | "-" (GR_MOD internal) | modd lacks GR_MOD subsystem |
| SONG K1 | G_BPM | "-" (GR_SONG list) | modd SONG is a different page concept |
| SETTINGS/FILES/DEMO/DEBUG | Claim SELECT/ALGO | (don't exist in upstream) | modd-only pages need these for navigation |

### Implemented via stubs (subsystem not fully ported)

| Feature | Upstream | MODD status |
|---------|----------|-------------|
| FX layer knobs | perf_k[0-3] real-time perform effects | Stubbed: knobs adjust perf_k[0-3] but no perform.c DSP behind them yet |
| REC layer knobs | CLICK/COUNT-IN/CLICK LEVEL | Stubbed: knobs adjust rec_click/countin/level but no click.c metronome behind them yet |
| FX layer keys | Effect assignment | Not implemented: needs perform.c port |
| REC layer keys | Recording controls | Not implemented: needs click.c/metronome port |

### Matched (additional)

- SELECT mode switching: Settings > CONTROL > SELECT = BPM / PAGE / MODW / VEL
- EDIT layer keys: white keys select engine by position
- GLO layer keys: black keys (54,56,58,59) toggle T1-T4 mute; F5 (65) unmute all
- SEQ layer keys: C3 (53) clears all steps on current track

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
