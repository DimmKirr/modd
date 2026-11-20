# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Dmitry Kireev (@DimmKirr)
"""LCD screenshot checks shared by the UI walkthrough tests: OCR text and pixel colours.

Image is None without Pillow and _get_ocr is None without rapidocr_onnxruntime (the
`ui-test` dependency group); callers skip their checks then.
"""
try:
    from PIL import Image
except ImportError:
    Image = None

try:
    from rapidocr_onnxruntime import RapidOCR
    _ocr_engine = None

    def _get_ocr():
        global _ocr_engine
        if _ocr_engine is None:
            # one thread: onnxruntime sizes its pools from the visible cores, and under a
            # CPU quota (containers, CI) that throttles every process, the emulator too
            _ocr_engine = RapidOCR(intra_op_num_threads=1, inter_op_num_threads=1)
        return _ocr_engine
except ImportError:
    _get_ocr = None

OCR_SCALE = 3


def _ocr_text(img):
    """Run RapidOCR on a PIL Image, return all detected text joined.

    Upscales the 240x240 LCD image for better detection on small text.
    """
    if _get_ocr is None or img is None:
        return ''
    import numpy as np
    img_up = img.resize((img.width * OCR_SCALE, img.height * OCR_SCALE),
                        Image.LANCZOS)
    result, _ = _get_ocr()(np.array(img_up))
    texts = []
    if result:
        for line in result:
            texts.append(line[1])
    return ' '.join(texts)


def _region_dominant_color(img, x, y, w, h):
    """Return the most common (R, G, B) in a rectangular region."""
    if img is None:
        return (0, 0, 0)
    crop = img.crop((x, y, x + w, y + h))
    colors = crop.getcolors(maxcolors=crop.size[0] * crop.size[1])
    if not colors:
        return (0, 0, 0)
    colors.sort(key=lambda c: c[0], reverse=True)
    return colors[0][1][:3]


def _color_distance(c1, c2):
    """Euclidean distance between two RGB tuples."""
    return sum((a - b) ** 2 for a, b in zip(c1, c2)) ** 0.5
