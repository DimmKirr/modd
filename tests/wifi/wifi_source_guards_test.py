#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Dmitry Kireev (@DimmKirr)
"""Source-level guards for WiFi bugs that were found on hardware.

The emulator has no WiFi MAC, so these read the C and check the shape that fixed a real
incident. Each test names what it protects. Anything that can run on the host belongs in a
compiled test instead (see wpa_handshake_test.py).
"""
import re
from pathlib import Path

import pytest

from kconfiglib import Kconfig, BOOL, expr_str

ROOT = Path(__file__).resolve().parents[2]
WIFI = ROOT / 'firmware' / 'src' / 'wifi'


def _fn_body(src, name):
    m = re.search(r'static\s+\w+\s+' + name + r'\s*\(.*?\)\s*\{(.*?)\n\}', src, re.DOTALL)
    assert m, f'{name}() not found'
    return m.group(1)


def test_assoc_sends_its_request_once_on_entry():
    """ASSOC must not resend on a `< 10` ms elapsed window every poll."""
    src = (WIFI / 'sta.c').read_text()
    fn_start = src.index('wifi_sta_poll(')
    fn_end = src.index('\n}', src.index('case WIFI_STATE_CONNECTED:', fn_start))
    fn = src[fn_start:fn_end]
    assoc = fn[fn.index('WIFI_STATE_ASSOC'):]
    assoc = assoc[:assoc.index('case ')]
    assert '< 10' not in assoc and '<10' not in assoc


def test_connect_derives_the_pmk_before_authentication():
    src = (WIFI / 'wifi.c').read_text()
    fn_start = src.index('wifi_connect(')
    fn = src[fn_start:src.index('\n}', fn_start) + 2]
    pmk = max(fn.find('wifi_wpa_init('), fn.find('wifi_wpa_derive_pmk('))
    auth = fn.find('wifi_sta_start_connect(')
    assert pmk >= 0 and auth >= 0
    assert pmk < auth, 'PMK must be derived before authentication starts'


# Known failure, kept visible: strict, so the test fails again the day it starts passing and
# this mark has to go. .scratch/tasks/todo/2026-11-19T21-00-wifi-unit-test-failures.md
@pytest.mark.xfail(strict=True, reason='wifi_sdio_tx() polls TX-done with interrupts off (up to ~10 ms against the 2.9 ms audio block); the fix waits for a hardware session')
def test_sdio_tx_keeps_irq_off_regions_short():
    """The audio DMA deadline is 2.9 ms: a claim/release region in wifi_sdio_tx must not hold
    both a loop and fm1_delay_us."""
    body = _fn_body((WIFI / 'hw' / 'sdio.c').read_text(), 'wifi_sdio_tx')
    regions = re.findall(r'wifi_sdio_claim\s*\(\s*\)(.*?)wifi_sdio_release\s*\(\s*\)', body, re.DOTALL)
    assert regions, 'no wifi_sdio_claim/release region found in wifi_sdio_tx'
    for i, region in enumerate(regions):
        assert not (re.search(r'\b(for|while)\s*\(', region) and re.search(r'fm1_delay_us\s*\(', region)), \
            f'region {i} holds a busy-wait loop and fm1_delay_us'


def test_rtos_shim_has_no_linux_kernel_names():
    """Clean-room guard: the RTOS shim uses project-original names only."""
    text = (WIFI / 'rtos.c').read_text()
    forbidden = ['sk_buff', 'alloc_skb', 'kfree_skb', 'skb_push', 'skb_pull', 'skb_put', 'skb_trim',
                 'skb_reserve', 'skb_clone', 'skb_copy', 'spinlock_t', 'spin_lock_irqsave',
                 'spin_unlock_irqrestore', 'tasklet_struct', 'tasklet_init', 'tasklet_hi_schedule',
                 'tasklet_kill', 'tasklet_fn_t', 'netif_carrier_on', 'netif_carrier_off',
                 'netif_carrier_ok', 'netif_start_queue', 'netif_stop_queue', 'netif_wake_queue',
                 'netif_rx']
    assert not [s for s in forbidden if s in text]


def test_autoscan_is_a_debug_only_option_and_off_by_default():
    sym = Kconfig(str(ROOT / 'Kconfig'), warn=False).syms['WIFI_DEBUG_AUTOSCAN']
    assert sym.type == BOOL
    deps = expr_str(sym.direct_dep)
    assert 'WIFI' in deps and 'DEBUG_LOG' in deps, deps
