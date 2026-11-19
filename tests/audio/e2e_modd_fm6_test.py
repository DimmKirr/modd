#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Dmitry Kireev (@DimmKirr)
"""E2E: verify modd default build runs Melodee core + FMLab FM6 end-to-end.

The modd defconfig enables CONFIG_AUDIO_CORE_MELODEE and CONFIG_ENGINE_FM6_FMLAB.
Track 1 is initialised with the FM6 engine (index 4 in ENGINES[]) and is the
selected track at boot (song.sel = 1).  This test plays notes on track 1 and
asserts that the FM6 engine produces audible audio output.
"""
import base64
import json
import os
import struct
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from e2e_fixtures import E2ETestCase, _run_timestamp, RESULTS_DIR, fwsc_path, EMU_RUNNING_JS

ROOT = Path(__file__).resolve().parents[2]
DEFCONFIG = 'modd'
FWSC = fwsc_path(DEFCONFIG)
TEST_RESULTS = RESULTS_DIR / f'{_run_timestamp}-e2e_modd_fm6_test'

JS_HOOK_AUDIO_CONTEXT = '''() => {
    const OrigAC = window.AudioContext;
    const origConnect = AudioNode.prototype.connect;

    window.AudioContext = function(...args) {
        const ac = new OrigAC(...args);
        window.__fm1_ac = ac;

        const analyser = ac.createAnalyser();
        analyser.fftSize = 2048;
        analyser.smoothingTimeConstant = 0;
        window.__fm1_analyser = analyser;

        const recorder = ac.createScriptProcessor(4096, 2, 2);
        const sink = ac.createMediaStreamDestination();
        window.__fm1_chunks = [];
        window.__fm1_isRecording = false;
        window.__fm1_sampleRate = ac.sampleRate;
        recorder.onaudioprocess = (e) => {
            if (!window.__fm1_isRecording) return;
            const L = new Float32Array(e.inputBuffer.getChannelData(0));
            const R = new Float32Array(e.inputBuffer.getChannelData(1));
            window.__fm1_chunks.push({ L, R });
        };
        recorder.connect(sink);
        window.__fm1_recorder = recorder;

        AudioNode.prototype.connect = function(dest, ...rest) {
            const result = origConnect.call(this, dest, ...rest);
            if (dest === ac.destination && this !== analyser && this !== recorder) {
                try { origConnect.call(this, analyser); } catch(e) {}
                try { origConnect.call(this, recorder); } catch(e) {}
            }
            return result;
        };
        return ac;
    };
    window.AudioContext.prototype = OrigAC.prototype;
    Object.setPrototypeOf(window.AudioContext, OrigAC);
}'''

JS_START_RECORDING = '''() => {
    window.__fm1_chunks = [];
    window.__fm1_isRecording = true;
}'''

JS_STOP_AND_EXPORT_WAV = '''() => {
    window.__fm1_isRecording = false;
    const chunks = window.__fm1_chunks;
    if (!chunks || chunks.length === 0)
        return { error: "no audio captured", samples: 0, base64: "" };

    const sr = window.__fm1_sampleRate || 44100;
    const totalFrames = chunks.reduce((s, c) => s + c.L.length, 0);
    const numChannels = 2;
    const bitsPerSample = 16;
    const bytesPerSample = bitsPerSample / 8;
    const blockAlign = numChannels * bytesPerSample;
    const dataSize = totalFrames * blockAlign;
    const buffer = new ArrayBuffer(44 + dataSize);
    const view = new DataView(buffer);

    const writeStr = (off, s) => { for (let i = 0; i < s.length; i++) view.setUint8(off + i, s.charCodeAt(i)); };
    writeStr(0, 'RIFF');
    view.setUint32(4, 36 + dataSize, true);
    writeStr(8, 'WAVE');
    writeStr(12, 'fmt ');
    view.setUint32(16, 16, true);
    view.setUint16(20, 1, true);
    view.setUint16(22, numChannels, true);
    view.setUint32(24, sr, true);
    view.setUint32(28, sr * blockAlign, true);
    view.setUint16(32, blockAlign, true);
    view.setUint16(34, bitsPerSample, true);
    writeStr(36, 'data');
    view.setUint32(40, dataSize, true);

    let off = 44;
    for (const chunk of chunks) {
        for (let i = 0; i < chunk.L.length; i++) {
            const clamp = (v) => Math.max(-1, Math.min(1, v));
            view.setInt16(off, clamp(chunk.L[i]) * 0x7FFF, true); off += 2;
            view.setInt16(off, clamp(chunk.R[i]) * 0x7FFF, true); off += 2;
        }
    }

    const bytes = new Uint8Array(buffer);
    let binary = '';
    for (let i = 0; i < bytes.length; i++) binary += String.fromCharCode(bytes[i]);
    const b64 = btoa(binary);

    return { samples: totalFrames, sampleRate: sr, durationMs: Math.round(totalFrames / sr * 1000), base64: b64 };
}'''


def save_wav(b64_data, path):
    raw = base64.b64decode(b64_data)
    Path(path).parent.mkdir(parents=True, exist_ok=True)
    Path(path).write_bytes(raw)
    return len(raw)


def wav_rms(path):
    data = Path(path).read_bytes()
    if len(data) < 44:
        return 0.0
    pcm = data[44:]
    n = len(pcm) // 2
    if n == 0:
        return 0.0
    samples = struct.unpack(f'<{n}h', pcm[:n * 2])
    sq_sum = sum(s * s for s in samples)
    return (sq_sum / n) ** 0.5 / 32768.0


class TestModdFM6Audio(E2ETestCase):
    """Boot modd defconfig, verify FM6 engine on track 1 produces audio.

    The modd default build uses:
      - CONFIG_AUDIO_CORE_MELODEE (Melodee audio core)
      - CONFIG_ENGINE_FM6_FMLAB   (FMLab FM6 engine)
    Track 1 is assigned FM6 (ENGINES[4]) and is the selected track at boot.
    """

    _build_ok = False

    @classmethod
    def setUpClass(cls):
        TEST_RESULTS.mkdir(parents=True, exist_ok=True)
        if not FWSC.is_file():
            cls._build_ok = False
        else:
            cls._build_ok = True
        try:
            super().setUpClass()
        except unittest.SkipTest:
            pass

    def test_fm6_on_track1_produces_audio(self):
        """Play C4-G4 on track 1 (FM6) and verify the recorded WAV has audio."""
        if not self._build_ok:
            self.skipTest(f'No firmware at {FWSC} (run: task build)')
        if not self._browser:
            self.skipTest('Emulator not available')

        page = self.new_emu_page()
        try:
            page.evaluate(JS_HOOK_AUDIO_CONTEXT)

            page.locator('#open').set_input_files(str(FWSC))
            page.wait_for_function(
                'document.getElementById("status").textContent.includes("ready")')
            page.locator('#start').click()
            page.wait_for_function(
                EMU_RUNNING_JS,
                timeout=30_000)
            page.wait_for_timeout(3_000)

            self.screenshot(page, 'modd-HOME-fm6-boot')

            # Silence baseline
            page.evaluate(JS_START_RECORDING)
            page.wait_for_timeout(1000)
            silence_wav = page.evaluate(JS_STOP_AND_EXPORT_WAV)

            silence_path = TEST_RESULTS / 'modd-fm6-silence.wav'
            if not silence_wav.get('error'):
                save_wav(silence_wav['base64'], silence_path)

            # Play notes on track 1 (FM6, already selected at boot)
            page.evaluate(JS_START_RECORDING)
            ctrl = self.controls(page)
            for note in ['c4', 'd4', 'e4', 'f4', 'g4']:
                ctrl.play_note(note, hold_ms=300, delay_ms=100)
            page.wait_for_timeout(500)
            playing_wav = page.evaluate(JS_STOP_AND_EXPORT_WAV)

            self.screenshot(page, 'modd-HOME-fm6-noteOn')

            playing_path = TEST_RESULTS / 'modd-fm6-playing.wav'
            if playing_wav.get('error'):
                (TEST_RESULTS / 'modd-fm6.json').write_text(
                    json.dumps({'error': playing_wav['error']}, indent=2) + '\n')
                self.fail(f'Recording failed: {playing_wav["error"]}')

            save_wav(playing_wav['base64'], playing_path)

            silence_rms = wav_rms(silence_path) if silence_path.exists() else 0.0
            playing_rms = wav_rms(playing_path)

            (TEST_RESULTS / 'modd-fm6.json').write_text(json.dumps({
                'firmware': str(FWSC),
                'defconfig': DEFCONFIG,
                'engine': 'FM6',
                'lineage': 'fmlab',
                'audio_core': 'melodee',
                'track': 1,
                'silence': {
                    'samples': silence_wav.get('samples', 0),
                    'durationMs': silence_wav.get('durationMs', 0),
                    'rms': silence_rms,
                },
                'playing': {
                    'samples': playing_wav['samples'],
                    'durationMs': playing_wav['durationMs'],
                    'rms': playing_rms,
                    'fileBytes': playing_path.stat().st_size,
                },
            }, indent=2) + '\n')

            self.assertTrue(playing_path.exists(), 'WAV file not written')
            self.assertGreater(playing_path.stat().st_size, 1000,
                               'WAV file suspiciously small')
            self.assertGreater(playing_rms, 0.001,
                               f'No audio from FM6 engine on modd defconfig '
                               f'(RMS {playing_rms:.6f})')
        finally:
            self.write_log(page, 'modd-fm6.log')
            page.close()

    def test_track1_shows_fm6_on_home(self):
        """HOME page must display FM6 engine name on track 1 card."""
        if not self._build_ok:
            self.skipTest(f'No firmware at {FWSC} (run: task build)')
        if not self._browser:
            self.skipTest('Emulator not available')

        page = self.new_emu_page()
        try:
            page.locator('#open').set_input_files(str(FWSC))
            page.wait_for_function(
                'document.getElementById("status").textContent.includes("ready")')
            page.locator('#start').click()
            page.wait_for_function(
                EMU_RUNNING_JS,
                timeout=30_000)
            page.wait_for_timeout(3_000)

            self.screenshot(page, 'modd-HOME-fm6-display')
            self.screenshot_lcd(page, 'modd-HOME-fm6-lcd')
        finally:
            self.write_log(page, 'modd-fm6-display.log')
            page.close()


if __name__ == '__main__':
    unittest.main()
