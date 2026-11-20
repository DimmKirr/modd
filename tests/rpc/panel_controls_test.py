# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Dmitry Kireev (@DimmKirr)
"""The panel control names live in the firmware; the test client keeps copies. They must match."""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tests'))

from api_fixtures import CdpPanel, FirmwareApi  # noqa: E402

API_CTL = ROOT / 'firmware' / 'src' / 'rpc' / 'api_ctl.c'


def _c_names(array):
    src = API_CTL.read_text()
    m = re.search(r'\b' + array + r'\[\d*\]\s*=\s*\{(.*?)\}', src, re.S)
    assert m, f'{array} not found in {API_CTL}'
    return tuple(re.findall(r'"([^"]*)"', m.group(1)))


def test_firmware_api_buttons_match_firmware():
    assert FirmwareApi.BUTTONS == _c_names('API_BTN'), (
        'FirmwareApi.BUTTONS in tests/api_fixtures.py differs from API_BTN in '
        'firmware/src/rpc/api_ctl.c: update tests/api_fixtures.py')


def test_firmware_api_encoders_match_firmware():
    assert FirmwareApi.ENCODERS == _c_names('API_ENC'), (
        'FirmwareApi.ENCODERS in tests/api_fixtures.py differs from API_ENC in '
        'firmware/src/rpc/api_ctl.c: update tests/api_fixtures.py')


def test_cdp_panel_button_ids_match():
    ids = CdpPanel.BUTTON_IDS
    assert tuple(ids) == FirmwareApi.BUTTONS and list(ids.values()) == list(range(len(ids))), (
        'CdpPanel.BUTTON_IDS in tests/api_fixtures.py differs from FirmwareApi.BUTTONS '
        '(names, order or ids): update tests/api_fixtures.py')


def test_cdp_panel_encoder_ids_match():
    ids = CdpPanel.ENCODER_IDS
    assert tuple(ids) == FirmwareApi.ENCODERS and list(ids.values()) == list(range(len(ids))), (
        'CdpPanel.ENCODER_IDS in tests/api_fixtures.py differs from FirmwareApi.ENCODERS '
        '(names, order or ids): update tests/api_fixtures.py')
