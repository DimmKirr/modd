#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Dmitry Kireev (@DimmKirr)
"""Unit tests for gen_ui_map.py: preprocessor guards and hardware controls.

The modd shell registers pages under #ifndef / #else branches and a page can
sit in two views (Files is HOME or DEMO depending on HOME_PAGE_FILES). The
generator has to carry the negation, or the docs describe a build that
cannot exist.
"""
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[4]
sys.path.insert(0, str(ROOT / 'tools' / 'codegen'))

from gen_ui_map import (
    _guards_met, control_path, generate_mermaid, load_nav_map,
    parse_hw_controls, parse_pages,
)

PAGES_C = '''
static void shell_register_pages(void)
{
    /* VIEW_HOME */
#ifdef CONFIG_HOME_PAGE_FILES
    nav_register(&PAGE_FILES);
#else
    nav_register(&PAGE_HOME);
#endif

    /* VIEW_DEMO (SAVE hold) */
#ifdef CONFIG_UDISK
#ifndef CONFIG_HOME_PAGE_FILES
    nav_register(&PAGE_FILES);
#endif
    nav_register(&PAGE_DISK);
#endif
}
'''


def _pages(tmp_path):
    f = tmp_path / 'pages.c'
    f.write_text(PAGES_C)
    return [(p['symbol'], p['view'], p['guards']) for p in parse_pages(f)]


def test_else_and_ifndef_negate_the_guard(tmp_path):
    assert _pages(tmp_path) == [
        ('PAGE_FILES', 'VIEW_HOME', ['CONFIG_HOME_PAGE_FILES']),
        ('PAGE_HOME', 'VIEW_HOME', ['!CONFIG_HOME_PAGE_FILES']),
        ('PAGE_FILES', 'VIEW_DEMO', ['CONFIG_UDISK', '!CONFIG_HOME_PAGE_FILES']),
        ('PAGE_DISK', 'VIEW_DEMO', ['CONFIG_UDISK']),
    ]


def test_guards_met_honours_negation():
    config = {'CONFIG_UDISK'}
    assert _guards_met(['CONFIG_UDISK', '!CONFIG_HOME_PAGE_FILES'], config)
    assert not _guards_met(['CONFIG_HOME_PAGE_FILES'], config)
    assert not _guards_met(['!CONFIG_UDISK'], config)
    assert _guards_met(['!defined(CONFIG_DOCK)', 'defined(CONFIG_UDISK)'], config)


def test_hw_controls_skip_sentinels(tmp_path):
    f = tmp_path / 'controls.h'
    f.write_text('#define BTN_HOME 8\n#define BTN_COUNT 14\n'
                 '#define ENC_SELECT   0\n#define ENC_PRESET   6\n'
                 '#define ENC_COUNT    7\n#define ENC_NONE     0xFF\n')
    buttons, encoders = parse_hw_controls(f)
    assert buttons == [('HOME', 8)]
    assert encoders == [('SELECT', 0), ('PRESET', 6)]


def test_modd_default_walk_starts_on_home_not_files():
    nav = load_nav_map('modd')
    steps = control_path(nav, config={'CONFIG_UDISK', 'CONFIG_SYS_MENU'})
    assert steps[0]['view'] == 'VIEW_HOME' and steps[0]['page'] != 'Files'
    demo = [s['page'] for s in steps if s['view'] == 'VIEW_DEMO']
    assert len(demo) == 2, demo


def test_modd_diagram_uses_the_theme_button_map():
    mermaid = generate_mermaid('modd')
    assert 'BTN_FX_H[/"FX hold"/] -.-> sg_CONN' in mermaid
    assert 'BTN_SAVE_H[/"SAVE hold"/] -.-> sg_DEMO' in mermaid
    # Files and modd are alternatives for HOME: distinct nodes, no cycle edge
    assert 'P_FILES_HOME' in mermaid and 'P_FILES_DEMO' in mermaid
    assert 'P_FILES_HOME <-..-> P_HOME' not in mermaid
