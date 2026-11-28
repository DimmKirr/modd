# LCD mirror over WebSocket

`CONFIG_MIRROR` streams the FM-1's screen to a browser and takes panel input back. It works, and it
is **off in every defconfig since 2026-11-27** because of the transport limits listed in
[Known limits](#known-limits). Turn it on for a debug session only.

## Enable and view

1. Set `CONFIG_MIRROR=y` (`task config`, Debug menu; needs `RPC` and `LWIP`) and build.
2. Open `http://<fm1-address>:<rpc-port>/mirror` in a browser. The page is served by the firmware
   and opens a WebSocket to the same host.

The page has zoom (x2, x3), a **Smooth** toggle (the browser's own interpolation in place of square
pixels; it removes the staircase and softens small text), Refresh and Save PNG.

Cost on the device: about 11 KB of RAM (7.5 KB of row hashes, the codec's buffers) and about 8 KB
of flash for the page. No framebuffer.

## Parts

| File | Class | Role |
|------|-------|------|
| `firmware/upstream/fm1-base/device/mirror.c` | INCLUDED (MPL-2.0, Charles Vestal) | The protocol: pixel codec, dirty-row tracking, input reports, remote control |
| `firmware/src/debug/mirror_ws.c` | modd adapter | The transport hooks `mirror.c` asks for, on a WebSocket |
| `firmware/src/rpc/rpc_ws.c` | modd | `ws_send_bin()`, binary frames |
| `firmware/src/rpc/rpc_server.c` | modd | Routes binary frames to `mirror_ws_recv()` |
| `firmware/src/rpc/mirror_page.h` | modd, decoder based on fm1-base `web/mirror/index.html` | The viewer, one C string, route `/mirror` in `api_ctl.c` |
| `patches/melodee/0001-lcd-debug-framebuffer-hooks.patch` | PATCHED | Calls `mirror_blit` / `mirror_fill` from `lcd_blit` / `lcd_fill` |

Because the hook is in `lcd_blit` / `lcd_fill`, every shell that draws through that LCD driver is
mirrored, not only the LVGL one.

## Wire format

One WebSocket binary message is one command: `cmd` (1 byte), body length (u16, little-endian), body.
The bodies are upstream's, unchanged; the full description is the comment at the top of `mirror.c`.

| Cmd | Direction | Body |
|-----|-----------|------|
| `0x50` request / hello | both | host: 0 off, 1 on (also the keep-alive, at least every 6 s), 2 refresh. Device: version, on, width - 1, height - 1 |
| `0x51` rect | device to host | x, y, w, h, seq, then the pixels, run-length coded |
| `0x52` fill | device to host | x, y, w, h, seq, colour (RGB565, high byte first) |
| `0x53` input | device to host | buttons, keys, encoder detents, MASTER, lit and dim lights (27 bytes) |
| `0x54` remote | both | host: seq, op, a, b, c (press a button or key, turn an encoder). Device: seq, op, done |

The pixel code works on RGB565 with a 64-entry colour cache that is reset for every rect:

| Byte | Meaning |
|------|---------|
| `0nnnnnnn` | n + 1 more pixels of the current colour (1..128) |
| `10iiiiii` | current colour = cache[i]; one pixel of it |
| `11nnnnnn hi lo` | new colour, stored in the cache; n + 1 pixels of it (1..64) |

Cache slot of a colour: `(c * 40503) >> 10 & 63`.

## Why this codec stays

For pixels from a device with no framebuffer and no room for zlib it is the right choice: a flat UI
codes to a few hundred bytes per band against 115 KB for a raw frame, the state is about 11 KB, and
input, lights and remote control are already in the protocol. What was looked at and not taken:

| Alternative | Why not |
|-------------|---------|
| RFB (VNC) over the same WebSocket | Works with stock clients (noVNC), but the bytes are about the same without zlib, and it is a second, modd-native protocol next to upstream's. Encoders and lights have no RFB equivalent |
| WebSocket permessage-deflate, PNG / MJPEG | Need a deflate state or a full framebuffer on the device |
| Shadow framebuffer and diff | 115 KB of RAM on hardware |
| LVGL | 9.6 has no remote-display or VNC driver; its manual says to forward the flush buffer, which is what the `lcd_blit` hook does. Display events (`LV_EVENT_REFR_READY`, `lv_display_flush_is_last()`) can mark the end of a refresh for batching |
| A real 2x stream from the device | About +82 KB RAM and +105 KB flash (estimates) against 55 KB and 92 KB free in the `modd` build, and the shell sizes things in raw pixels. 2x renders belong in the emulator |

## Known limits

All four are in modd's transport around the codec, which is still shaped for USB-MIDI. They are the
reason the option is off; the work is in `.scratch/tasks/todo/` (`mirror-ws-transport-backpressure`).

1. **No backpressure.** `mr_free()` always answers 8192 and `mr_put()` ignores the result of
   `ws_send_bin()`. A send that fails is recorded by `mirror.c` as delivered, so its drop-and-redraw
   recovery never runs; the viewer only notices through a gap in `seq` and asks for a refresh.
2. **The send blocks the LCD flush.** `rpc_tcp_send()` waits for room in the TCP send buffer, up to
   `RPC_SEND_TIMEOUT_TICKS` (10 s). It is called from `lcd_blit`, so a slow or stalled link stalls
   the UI task.
3. **Pack and unpack round trip.** Upstream's `mr_seal` packs every frame 8-to-7 with a checksum for
   SysEx; `mr_put()` unpacks it and copies it twice more. Four passes over each frame for nothing.
4. **One small message per rect.** A rect's code is capped near 256 bytes (`MR_CAP`), so a full
   redraw is dozens of WebSocket messages. Upstream's skip of unchanged pixels applies to full-width
   rows only, and LVGL's partial render mode flushes narrower areas, which are sent whole.

Also: the RPC server takes one TCP connection, so the REST API does not answer while a viewer is
connected.
