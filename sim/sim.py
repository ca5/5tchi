#!/usr/bin/env python3
"""Host simulator for tama-stage-minimal (local Web UI + headless smoke test)."""

import ctypes
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import os
import struct
import sys
import threading
import time
import urllib.parse
import webbrowser
import zlib

LCD_W = 128
LCD_H = 128

BTN_A = 1 << 0
BTN_B = 1 << 1
BTN_C = 1 << 2
BTN_DIAL = 1 << 3

HTML_PAGE = """<!doctype html>
<html lang="ja">
<head>
<meta charset="utf-8">
<title>Tama STAGE Simulator (128x128 RGB565)</title>
<style>
  body {
    margin: 0; padding: 24px; background: #14161a; color: #e6edf3;
    font-family: -apple-system, BlinkMacSystemFont, sans-serif;
    display: flex; flex-direction: column; align-items: center; gap: 14px;
    user-select: none;
  }
  canvas {
    width: 384px; height: 384px; image-rendering: pixelated;
    border: 2px solid #30363d; border-radius: 8px; background: #000;
  }
  .controls { display: flex; gap: 8px; flex-wrap: wrap; justify-content: center; }
  button {
    background: #21262d; color: #e6edf3; border: 1px solid #30363d;
    border-radius: 6px; padding: 10px 14px; font-size: 14px; cursor: pointer;
  }
  button:active, button.active { background: #388bfd; border-color: #58a6ff; }
  .status { font-size: 12px; color: #8b949e; font-family: monospace; }
  .uart-box {
    width: 384px; display: flex; flex-direction: column; gap: 6px;
    background: #1c2128; border: 1px solid #30363d; border-radius: 8px; padding: 10px;
    box-sizing: border-box;
  }
  .uart-box label { font-size: 12px; color: #8b949e; }
  .uart-box textarea {
    width: 100%; height: 96px; background: #0d1117; color: #e6edf3;
    border: 1px solid #30363d; border-radius: 6px; font-family: monospace; font-size: 12px;
    padding: 6px; box-sizing: border-box; resize: vertical;
  }
</style>
</head>
<body>
  <canvas id="lcd" width="128" height="128"></canvas>
  <div class="controls">
    <button data-bit="1">A (Next)</button>
    <button data-bit="2">B (OK)</button>
    <button data-bit="4">C (Beat/Back)</button>
    <button data-bit="8">DIAL (OK)</button>
    <button id="dial-minus">Dial -</button>
    <button id="dial-plus">Dial +</button>
  </div>
  <div class="status" id="status">Connecting...</div>
  <div class="uart-box">
    <label>PC連携 CSV セットリスト転送 (UART1 460800bps 模擬)</label>
    <textarea id="csv-input">1,OP SE,90,128
2,SONG A,240,135
3,MC,120,120
4,ENCORE,300,140</textarea>
    <button id="send-csv">Send CSV &amp; Sync Time</button>
  </div>
<script>
const canvas = document.getElementById('lcd');
const ctx = canvas.getContext('2d');
const img = ctx.createImageData(128, 128);
const statusEl = document.getElementById('status');
let mask = 0;

function sendButtons(newMask) {
  mask = newMask;
  fetch('/api/buttons?mask=' + mask, { method: 'POST' }).catch(() => {});
}
function sendDial(delta) {
  fetch('/api/dial?delta=' + delta, { method: 'POST' }).catch(() => {});
}

document.querySelectorAll('button[data-bit]').forEach(btn => {
  const bit = parseInt(btn.dataset.bit, 10);
  const down = (e) => { e.preventDefault(); btn.classList.add('active'); sendButtons(mask | bit); };
  const up = () => { btn.classList.remove('active'); sendButtons(mask & ~bit); };
  btn.addEventListener('mousedown', down);
  btn.addEventListener('mouseup', up);
  btn.addEventListener('mouseleave', up);
  btn.addEventListener('touchstart', down);
  btn.addEventListener('touchend', up);
});
document.getElementById('dial-minus').addEventListener('click', () => sendDial(-1));
document.getElementById('dial-plus').addEventListener('click', () => sendDial(1));
document.getElementById('send-csv').addEventListener('click', () => {
  const now = new Date();
  const hh = String(now.getHours()).padStart(2, '0');
  const mm = String(now.getMinutes()).padStart(2, '0');
  const ss = String(now.getSeconds()).padStart(2, '0');
  const payload = `TIME ${hh}:${mm}:${ss}\nCLEAR\n` + document.getElementById('csv-input').value.trim() + `\nAPPLY\n`;
  fetch('/api/uart', { method: 'POST', body: payload }).catch(() => {});
});

const keyMap = { 'a': 1, 'b': 2, 'c': 4, ' ': 8 };
window.addEventListener('keydown', (e) => {
  if (e.target && e.target.tagName === 'TEXTAREA') return;
  if (e.repeat) return;
  const k = e.key.toLowerCase();
  if (k in keyMap) { e.preventDefault(); sendButtons(mask | keyMap[k]); }
  else if (e.key === 'ArrowLeft') { e.preventDefault(); sendDial(-1); }
  else if (e.key === 'ArrowRight') { e.preventDefault(); sendDial(1); }
});
window.addEventListener('keyup', (e) => {
  const k = e.key.toLowerCase();
  if (k in keyMap) { sendButtons(mask & ~keyMap[k]); }
});

async function pollFrame() {
  try {
    const res = await fetch('/api/frame');
    const frame = res.headers.get('X-Frame-Count') || '0';
    const bl = res.headers.get('X-Backlight') || '100';
    const buf = new Uint8Array(await res.arrayBuffer());
    img.data.set(buf);
    ctx.putImageData(img, 0, 0);
    statusEl.textContent = `Frame: ${frame} | Backlight: ${bl}% | Keys: A/B/C/Space(Dial), Left/Right(Rotate)`;
  } catch (e) {}
  setTimeout(pollFrame, 33);
}
pollFrame();
</script>
</body>
</html>
"""


def load_sim_lib():
    here = os.path.dirname(os.path.abspath(__file__))
    lib_path = os.path.join(here, "libtama_sim.dylib")
    if not os.path.exists(lib_path):
        raise FileNotFoundError(
            f"{lib_path} not found. Run `make -C tama-stage-minimal sim` first."
        )
    lib = ctypes.CDLL(lib_path)
    lib.app_main.restype = ctypes.c_int
    lib.sim_set_buttons.argtypes = [ctypes.c_uint8]
    lib.sim_add_dial_delta.argtypes = [ctypes.c_int8]
    lib.sim_push_uart_line.argtypes = [ctypes.c_char_p]
    lib.sim_get_frame_count.restype = ctypes.c_uint32
    lib.sim_get_backlight.restype = ctypes.c_uint8
    lib.sim_get_frontbuffer.restype = ctypes.POINTER(ctypes.c_uint16 * (LCD_W * LCD_H))
    return lib


def framebuffer_to_rgba(buf_ptr, backlight_pct: int) -> bytes:
    raw = buf_ptr.contents
    scale = max(10, backlight_pct) / 100.0
    out = bytearray(LCD_W * LCD_H * 4)
    idx = 0
    for px in raw:
        out[idx] = int((((px >> 11) & 0x1F) * 255 // 31) * scale)
        out[idx + 1] = int((((px >> 5) & 0x3F) * 255 // 63) * scale)
        out[idx + 2] = int(((px & 0x1F) * 255 // 31) * scale)
        out[idx + 3] = 255
        idx += 4
    return bytes(out)


def write_png(path: str, buf_ptr, backlight_pct: int) -> None:
    rgba = framebuffer_to_rgba(buf_ptr, backlight_pct)
    rows = bytearray()
    stride = LCD_W * 4
    for y in range(LCD_H):
        rows.append(0)
        rows.extend(rgba[y * stride : (y + 1) * stride])

    def chunk(tag: bytes, data: bytes) -> bytes:
        return struct.pack(">I", len(data)) + tag + data + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)

    ihdr = struct.pack(">IIBBBBB", LCD_W, LCD_H, 8, 6, 0, 0, 0)
    png = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", ihdr) + chunk(b"IDAT", zlib.compress(bytes(rows))) + chunk(b"IEND", b"")
    with open(path, "wb") as f:
        f.write(png)


def run_headless_smoke_test(lib, screenshot_path=None):
    t = threading.Thread(target=lib.app_main, daemon=True)
    t.start()
    time.sleep(0.25)
    frames = lib.sim_get_frame_count()
    bl = lib.sim_get_backlight()
    buf = lib.sim_get_frontbuffer().contents
    non_black = sum(1 for px in buf if px != 0)
    print(f"Smoke test: frames={frames}, backlight={bl}%, non_black_pixels={non_black}/{LCD_W * LCD_H}")
    if screenshot_path:
        write_png(screenshot_path, lib.sim_get_frontbuffer(), bl)
        print(f"Saved screenshot: {screenshot_path}")

    lib.sim_set_buttons(BTN_DIAL)
    time.sleep(2.15)
    fc_before = lib.sim_get_frame_count()
    lib.sim_set_buttons(0)
    deadline = time.time() + 0.5
    while lib.sim_get_frame_count() <= fc_before and time.time() < deadline:
        time.sleep(0.02)
    buf_menu = lib.sim_get_frontbuffer().contents
    yellow_pixels = sum(1 for px in buf_menu if px == 0xFFE0)
    print(f"Menu long-press test: yellow_pixels={yellow_pixels}")
    return frames > 0 and non_black > 0 and yellow_pixels > 0


def main():
    lib = load_sim_lib()
    if "--smoke" in sys.argv:
        shot = None
        if "--screenshot" in sys.argv:
            idx = sys.argv.index("--screenshot")
            if idx + 1 < len(sys.argv):
                shot = sys.argv[idx + 1]
        ok = run_headless_smoke_test(lib, shot)
        sys.exit(0 if ok else 1)

    t = threading.Thread(target=lib.app_main, daemon=True)
    t.start()

    class Handler(BaseHTTPRequestHandler):
        def log_message(self, format, *args):
            pass

        def do_GET(self):
            if self.path == "/":
                body = HTML_PAGE.encode("utf-8")
                self.send_response(200)
                self.send_header("Content-Type", "text/html; charset=utf-8")
                self.send_header("Content-Length", str(len(body)))
                self.end_headers()
                self.wfile.write(body)
            elif self.path == "/api/frame":
                fc = lib.sim_get_frame_count()
                bl = lib.sim_get_backlight()
                rgba = framebuffer_to_rgba(lib.sim_get_frontbuffer(), bl)
                self.send_response(200)
                self.send_header("Content-Type", "application/octet-stream")
                self.send_header("X-Frame-Count", str(fc))
                self.send_header("X-Backlight", str(bl))
                self.send_header("Content-Length", str(len(rgba)))
                self.end_headers()
                self.wfile.write(rgba)
            else:
                self.send_error(404)

        def do_POST(self):
            parsed = urllib.parse.urlparse(self.path)
            qs = urllib.parse.parse_qs(parsed.query)
            if parsed.path == "/api/buttons":
                m = int(qs.get("mask", ["0"])[0]) & 0xFF
                lib.sim_set_buttons(m)
                self.send_response(204)
                self.end_headers()
            elif parsed.path == "/api/dial":
                d = max(-128, min(127, int(qs.get("delta", ["0"])[0])))
                lib.sim_add_dial_delta(d)
                self.send_response(204)
                self.end_headers()
            elif parsed.path == "/api/uart":
                length = int(self.headers.get("Content-Length", "0"))
                raw_body = self.rfile.read(length).decode("utf-8", errors="ignore")
                for ln in raw_body.splitlines():
                    ln = ln.strip()
                    if ln:
                        lib.sim_push_uart_line(ln.encode("ascii", errors="ignore"))
                self.send_response(204)
                self.end_headers()
            else:
                self.send_error(404)

    server = ThreadingHTTPServer(("127.0.0.1", 0), Handler)
    port = server.server_address[1]
    url = f"http://127.0.0.1:{port}/"
    print(f"Tama STAGE Simulator running at {url}", flush=True)
    webbrowser.open(url)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass


if __name__ == "__main__":
    main()
