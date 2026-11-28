# SPDX-License-Identifier: GPL-3.0-only
"""Host-side test for the OTA updater's LCD screen rendering.

Compiles a C harness that exercises the screen code with a mock LCD
(framebuffer capture), saves PPM screenshots, converts to PNG for the
test results directory, and verifies key properties: background colour,
progress bar growth, text presence, file browser highlight.
"""
import subprocess
import pytest
from pathlib import Path

import io_log

HARNESS_C = Path(__file__).parent / "boot_screen_harness.c"
OUT_DIR = Path(__file__).parent / "results"

SCREEN_NAMES = [
    "01_init", "02_waiting", "03_flash_begin", "04_progress_45",
    "05_progress_100", "06_done", "07_error", "08_browse", "09_browse_scroll",
    "10_progress_capped",
]


def _ppm_to_png(ppm_path, png_path):
    """Convert P6 PPM to PNG using Pillow."""
    from PIL import Image
    img = Image.open(ppm_path)
    img.save(png_path)


@pytest.fixture(scope="module")
def harness_output():
    OUT_DIR.mkdir(exist_ok=True)
    binary = OUT_DIR / "boot_screen_harness"
    r = subprocess.run(
        ["cc", "-Wall", "-Wextra", "-o", str(binary), str(HARNESS_C)],
        capture_output=True, text=True,
    )
    assert r.returncode == 0, f"Compile failed:\n{r.stderr}"
    r = subprocess.run(
        [str(binary), str(OUT_DIR)], capture_output=True, text=True,
    )
    assert r.returncode == 0, f"Harness failed:\n{r.stderr}"
    results = io_log.results_dir("test_boot_screen")
    results.mkdir(parents=True, exist_ok=True)
    for name in SCREEN_NAMES:
        ppm = OUT_DIR / f"{name}.ppm"
        if ppm.exists():
            _ppm_to_png(ppm, results / f"{name}.png")
    return OUT_DIR


def read_ppm(path):
    """Read P6 PPM, return (w, h, pixels) where pixels is bytes of RGB."""
    with open(path, "rb") as f:
        magic = f.readline().strip()
        assert magic == b"P6", f"Expected P6, got {magic}"
        dims = f.readline().strip().split()
        w, h = int(dims[0]), int(dims[1])
        maxval = f.readline().strip()
        assert maxval == b"255"
        pixels = f.read()
        assert len(pixels) == w * h * 3
    return w, h, pixels


def pixel_at(pixels, w, x, y):
    off = (y * w + x) * 3
    return pixels[off], pixels[off + 1], pixels[off + 2]


def count_matching(pixels, w, y_range, x_range, pred):
    return sum(
        1
        for y in y_range
        for x in x_range
        if pred(*pixel_at(pixels, w, x, y))
    )


def is_cream(r, g, b):
    return r > 240 and g > 230 and b > 220


def is_brown(r, g, b):
    return r < 100 and g < 80 and b < 60


def is_coral(r, g, b):
    return r > 160 and g < 100 and b < 80


class TestScreenInit:
    def test_dimensions(self, harness_output):
        w, h, _ = read_ppm(harness_output / "01_init.ppm")
        assert w == 240 and h == 240

    def test_background_is_cream(self, harness_output):
        _, _, px = read_ppm(harness_output / "01_init.ppm")
        r, g, b = pixel_at(px, 240, 120, 120)
        assert is_cream(r, g, b), f"Expected cream centre, got ({r},{g},{b})"

    def test_corners_are_cream(self, harness_output):
        _, _, px = read_ppm(harness_output / "01_init.ppm")
        for x, y in [(0, 0), (239, 0), (0, 239), (239, 239)]:
            r, g, b = pixel_at(px, 240, x, y)
            assert is_cream(r, g, b), f"Corner ({x},{y}) not cream: ({r},{g},{b})"


class TestWaitingScreen:
    def test_has_brown_text(self, harness_output):
        _, _, px = read_ppm(harness_output / "02_waiting.ppm")
        brown = count_matching(px, 240, range(55, 85), range(240), is_brown)
        assert brown > 50, f"Expected brown text in WAITING, found {brown} px"


class TestProgressBar:
    def test_flash_begin_has_track(self, harness_output):
        """The empty bar track should be visible."""
        _, _, px = read_ppm(harness_output / "03_flash_begin.ppm")
        # SCR_TRACK is #e8dcc8: r~232, g~220, b~200
        track = count_matching(
            px, 240, range(150, 162), range(30, 210),
            lambda r, g, b: 220 < r < 240 and 210 < g < 230 and 190 < b < 210,
        )
        assert track > 500, f"Expected track bar, found {track} track pixels"

    def test_bar_grows_at_45pct(self, harness_output):
        _, _, px0 = read_ppm(harness_output / "03_flash_begin.ppm")
        _, _, px45 = read_ppm(harness_output / "04_progress_45.ppm")
        bar_y = 156
        coral_0 = sum(1 for x in range(240) if is_coral(*pixel_at(px0, 240, x, bar_y)))
        coral_45 = sum(1 for x in range(240) if is_coral(*pixel_at(px45, 240, x, bar_y)))
        assert coral_45 > coral_0 + 50, f"Bar should grow: {coral_0} -> {coral_45}"

    def test_bar_full_at_100pct(self, harness_output):
        _, _, px = read_ppm(harness_output / "05_progress_100.ppm")
        bar_y = 156
        coral = sum(1 for x in range(30, 210) if is_coral(*pixel_at(px, 240, x, bar_y)))
        assert coral >= 170, f"Expected full bar (~180 coral px), got {coral}"

    def test_percent_text_at_45(self, harness_output):
        """The percentage text row (y~172-185) should contain brown pixels."""
        _, _, px = read_ppm(harness_output / "04_progress_45.ppm")
        brown = count_matching(px, 240, range(172, 186), range(240), is_brown)
        assert brown > 20, f"Expected percentage text, found {brown} brown px"


class TestDoneScreen:
    def test_done_text_present(self, harness_output):
        _, _, px = read_ppm(harness_output / "06_done.ppm")
        brown = count_matching(px, 240, range(60, 81), range(240), is_brown)
        assert brown > 30, f"Expected DONE text, found {brown} brown px"

    def test_full_bar_on_done(self, harness_output):
        _, _, px = read_ppm(harness_output / "06_done.ppm")
        bar_y = 156
        coral = sum(1 for x in range(30, 210) if is_coral(*pixel_at(px, 240, x, bar_y)))
        assert coral >= 170, f"Done should have full bar, got {coral}"


class TestErrorScreen:
    def test_error_text_present(self, harness_output):
        _, _, px = read_ppm(harness_output / "07_error.ppm")
        brown = count_matching(px, 240, range(60, 81), range(240), is_brown)
        assert brown > 30, f"Expected ERROR text, found {brown} brown px"

    def test_detail_text_present(self, harness_output):
        """'FLASH FAILED' at y=94, scale 2, height 14."""
        _, _, px = read_ppm(harness_output / "07_error.ppm")
        brown = count_matching(px, 240, range(94, 108), range(240), is_brown)
        assert brown > 20, f"Expected detail text, found {brown} brown px"


class TestFileBrowser:
    def test_highlight_on_first(self, harness_output):
        """First entry selected: coral highlight bar."""
        _, _, px = read_ppm(harness_output / "08_browse.ppm")
        coral = count_matching(px, 240, range(50, 70), range(10, 230), is_coral)
        assert coral > 200, f"Expected coral highlight, found {coral} px"

    def test_highlight_moves(self, harness_output):
        """Third entry selected: coral at a different y band."""
        _, _, px = read_ppm(harness_output / "09_browse_scroll.ppm")
        # Entry 2 at y = 50 + 2*20 = 90, band 90..110
        coral_band2 = count_matching(px, 240, range(90, 110), range(10, 230), is_coral)
        # Entry 0 band should NOT have coral
        coral_band0 = count_matching(px, 240, range(50, 70), range(10, 230), is_coral)
        assert coral_band2 > 200, f"Expected coral at entry 2, found {coral_band2}"
        assert coral_band0 < 50, f"Entry 0 should not be highlighted, found {coral_band0}"

    def test_has_title(self, harness_output):
        """'SELECT FILE' at y=10, scale 3."""
        _, _, px = read_ppm(harness_output / "08_browse.ppm")
        brown = count_matching(px, 240, range(10, 31), range(240), is_brown)
        assert brown > 50, f"Expected title text, found {brown} brown px"


class TestProgressCapping:
    """When the flash region is larger than the hardware limit (emulator),
    the progress bar should still reflect a meaningful percentage."""

    def test_capped_bar_matches_45pct(self, harness_output):
        """With 45% of the hardware region done on a 6 MB emu total,
        the bar should be ~45% full, not ~4%."""
        _, _, px = read_ppm(harness_output / "10_progress_capped.ppm")
        bar_y = 156
        coral = sum(1 for x in range(30, 210) if is_coral(*pixel_at(px, 240, x, bar_y)))
        # 45% of 180px bar width = ~81px.  Allow a range.
        assert 60 < coral < 120, f"Expected ~81 coral px (45%), got {coral}"

    def test_capped_percent_text(self, harness_output):
        """The percentage text should read around 45%."""
        _, _, px = read_ppm(harness_output / "10_progress_capped.ppm")
        brown = count_matching(px, 240, range(172, 186), range(240), is_brown)
        assert brown > 20, f"Expected percentage text, found {brown} brown px"
