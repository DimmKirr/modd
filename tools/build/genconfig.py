#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Dmitry Kireev (@DimmKirr)
"""Generate build/<defconfig>/gen/config.h from .config using Kconfiglib.

  tools/genconfig.py                    # generate from .config
  tools/genconfig.py --defconfig NAME   # load configs/NAME_defconfig first
"""
import argparse
import json
import os
from pathlib import Path
from kconfiglib import Kconfig

ROOT = Path(__file__).resolve().parents[2]

# Symbols whose values are secret. config.h carries them (the firmware needs
# them); config.masked.h does not, and it is the only one debug dumps and
# logs may print. Add every new credential symbol here.
SECRET_SYMBOLS = ('WIFI_PASSWORD', 'OPENROUTER_API_KEY')
# Symbols an emulator build (CONFIG_EMU) takes from its defconfig, never from the environment.
EMU_KEEPS = ('WIFI_SSID', 'WIFI_PASSWORD')
SECRET_MASK = '***'


def mask_secrets(header_text):
    """config.h text with every secret symbol's value replaced by a mask.

    An empty value stays visible as "": whether a secret is set at all is a
    useful debug fact and gives nothing away.
    """
    out = []
    for line in header_text.splitlines():
        for name in SECRET_SYMBOLS:
            prefix = f'#define CONFIG_{name} '
            if line.startswith(prefix):
                empty = line[len(prefix):].strip() == '""'
                line = prefix + ('""' if empty else f'"{SECRET_MASK}"')
        out.append(line)
    return '\n'.join(out) + '\n'


COMPONENT_SYMBOLS = {
    'AUDIO': 'audio',
    'DUAL_CORE': 'dual-core',
    'AUDIO_CORE_FELUCCA': 'audio-core:felucca',
    'AUDIO_CORE_SLOOP': 'audio-core:sloop',
    'AUDIO_CORE_MELODEE': 'audio-core:melodee',
    'ENGINE_FM6': 'engine:fm6',
    'ENGINE_DRUM': 'engine:drum',
    'ENGINE_PROPHET': 'engine:prophet',
    'ENGINE_CZ1': 'engine:cz1',
    'ENGINE_SID': 'engine:sid',
    'ENGINE_ANALOG': 'engine:analog',
    'ENGINE_DIGITAL': 'engine:digital',
    'ENGINE_PHASE': 'engine:phase',
    'ENGINE_SAMPLE': 'engine:sample',
    'ENGINE_GRANULAR': 'engine:granular',
    'ENGINE_LOFI': 'engine:lofi',
    'ENGINE_TRIO': 'engine:trio',
    'ENGINE_TONEWHEEL': 'engine:tonewheel',
    'ENGINE_FORMANT': 'engine:formant',
    'ENGINE_PHYSICAL': 'engine:physical',
    'ENGINE_NOISE': 'engine:noise',
    'EFFECTS_FELUCCA': 'effects:felucca',
    'EFFECTS_SLOOP': 'effects:sloop',
    'EFFECTS_MELODEE': 'effects:melodee',
    'MODULATION': 'modulation',
    'SEQ_FELUCCA': 'seq:felucca',
    'SEQ_SLOOP': 'seq:sloop',
    'SEQ_MELODEE': 'seq:melodee',
    'SCALES_FELUCCA': 'scales:felucca',
    'SCALES_MELODEE': 'scales:melodee',
    'MIDI_UART': 'midi-uart',
    'USB': 'usb',
    'MIDI_FELUCCA': 'midi:felucca',
    'MIDI_SLOOP': 'midi:sloop',
    'MIDI_MELODEE': 'midi:melodee',
    'USB_HOST': 'usb-host',
    'UDISK': 'udisk',
    'LWIP': 'net:lwip',
    'USB_NET': 'usb-net',
    'USB_AUDIO': 'usb-audio',
    'INPUT': 'input',
    'UI_THEME_FELUCCA': 'ui:felucca',
    'UI_THEME_SLOOP': 'ui:sloop',
    'UI_THEME_MELODEE': 'ui:melodee',
    'UI_THEME_MODD': 'ui:modd',
    'FLASH': 'flash',
    'STORAGE_FELUCCA': 'storage:felucca',
    'STORAGE_SLOOP': 'storage:sloop',
    'STORAGE_MELODEE': 'storage:melodee',
    'BOOT_FELUCCA': 'boot:felucca',
    'BOOT_SLOOP': 'boot:sloop',
    'BOOT_MELODEE': 'boot:melodee',
    'OTA': 'ota',
    'BLE': 'ble',
    'BLE_MIDI': 'ble-midi',
    'BLE_CENTRAL': 'ble-central',
    'BLE_HID': 'ble-hid',
    'BLE_BOND': 'ble-bond',
    'BLE_FILE_TRANSFER': 'ble-ft',
    'MOD_MELODEE': 'mod:melodee',
    'MIX': 'mix',
    'EDITOR': 'editor',
    'CONSOLE': 'console',
    'DEBUG_LOG': 'debug-log',
    'CONTROLS_FELUCCA': 'controls:felucca',
    'CONTROLS_MELODEE': 'controls:melodee',
    'CONTROLS_SLOOP': 'controls:sloop',
}


def write_manifest(kconf, out_dir):
    enabled = []
    for sym_name, component_name in COMPONENT_SYMBOLS.items():
        sym = kconf.syms.get(sym_name)
        if sym and sym.str_value == 'y':
            enabled.append(component_name)
    manifest = {'components': sorted(enabled)}
    (out_dir / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')

    marker = '<<MODD>>'
    manifest_str = marker + ','.join(sorted(enabled)) + marker
    lines = [
        '#ifndef MODD_MANIFEST_H',
        '#define MODD_MANIFEST_H',
        f'__attribute__((used)) static const char modd_manifest[] = "{manifest_str}";',
        '#endif',
        '',
    ]
    (out_dir / 'manifest.h').write_text('\n'.join(lines))
    return enabled


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--defconfig', help='load configs/<NAME>_defconfig')
    ap.add_argument('--outdir', help='write config.h and manifest here (default: build/<defconfig>/gen)')
    args = ap.parse_args()

    os.environ['srctree'] = str(ROOT)
    kconf = Kconfig(str(ROOT / 'Kconfig'), warn_to_stderr=True)
    config = ROOT / '.config'

    if args.defconfig:
        dc = ROOT / 'configs' / f'{args.defconfig}_defconfig'
        if not dc.exists():
            raise SystemExit(f'defconfig not found: {dc}')
        kconf.load_config(str(dc))
    elif config.exists():
        kconf.load_config(str(config))

    if args.outdir:
        out = Path(args.outdir)
    else:
        dc = args.defconfig or 'modd'
        out = ROOT / 'build' / dc / 'gen'
    out.mkdir(parents=True, exist_ok=True)

    # Save .config BEFORE env overrides so secrets stay out of the repo.
    if args.outdir:
        kconf.write_config(str(out / '.config'))
    elif not args.defconfig:
        kconf.write_config(str(config))

    # Environment overrides for string Kconfig symbols (build output only).
    # Taskfile dotenv loads .env; WIFI_PASSWORD=x overrides CONFIG_WIFI_PASSWORD.
    ENV_OVERRIDES = ['WIFI_SSID', 'WIFI_PASSWORD', 'WIFI_MAC_OVERRIDE',
                      'OPENROUTER_API_KEY']
    # An emulator build (CONFIG_EMU) joins fm1-live's virtual AP with the credentials its
    # defconfig carries: the ones in the environment (.env, a real network) do not replace them.
    emu = 'EMU' in kconf.syms and kconf.syms['EMU'].str_value == 'y'
    for name in ENV_OVERRIDES:
        val = os.environ.get(name)
        if val is not None:
            if emu and name in EMU_KEEPS:
                print(f'genconfig: {name} from the environment ignored (emulator build)')
                continue
            sym = kconf.syms.get(name)
            if sym:
                sym.set_value(val)
                print(f'genconfig: {name} overridden from environment')
    kconf.write_autoconf(str(out / 'config.h'))
    (out / 'config.masked.h').write_text(mask_secrets((out / 'config.h').read_text()))
    components = write_manifest(kconf, out)
    print(f'genconfig: wrote {out / "config.h"}')
    print(f'genconfig: manifest ({len(components)} components): {", ".join(components)}')


if __name__ == '__main__':
    main()
