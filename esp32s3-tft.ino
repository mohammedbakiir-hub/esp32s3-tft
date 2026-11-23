#include <Adafruit_GFX.h>
#include <Adafruit_ST7735.h>
#include <SPI.h>
#include <WiFi.h>
#include <WebServer.h>
#include <Update.h>
#include <time.h>
#include <math.h>

// ============================================================
// NEXUS | PERSONAL SYSTEM CORE
// ESP32-S3 + ST7735 | Wi-Fi service | Firmware OTA | Time module
//
// Project identity is intentionally generic. Add future sensors,
// actuators, control pages, and other modules in their own sections.
// ============================================================

// ------------------------- USER CONFIG ------------------------
// Keep the display name short (about 6 characters) for the 128 px TFT header.
const char* DEVICE_NAME = "NEXUS";

const char* WIFI_SSID = "Qwerty1";
const char* WIFI_PASS = "zxcvbnm123";

// HTTP Basic Authentication for local firmware maintenance.
// Change this before using OTA. Do not expose port 80 to the internet.
const char* OTA_USER = "admin";
const char* OTA_PASS = "111115";

// Indonesia Western Time (WIB, UTC+7)
const long GMT_OFFSET_SEC = 7L * 3600L;
const int DAYLIGHT_OFFSET_SEC = 0;

// --------------------------- TFT PINS -------------------------
#define TFT_CS    13
#define TFT_RST   9
#define TFT_DC    10
#define TFT_MOSI  11
#define TFT_SCLK  12

Adafruit_ST7735 tft(TFT_CS, TFT_DC, TFT_MOSI, TFT_SCLK, TFT_RST);
WebServer server(80);

// ------------------------ DISPLAY LAYOUT ----------------------
constexpr int SCREEN_W = 128;
constexpr int SCREEN_H = 160;
constexpr int CLK_CX = 64;
constexpr int CLK_CY = 74;
constexpr int CLK_R  = 45;

// A 16-bit canvas stores the still background. Only old hand pixels
// are restored from it, so the face never needs to be redrawn per tick.
GFXcanvas16 faceCanvas(SCREEN_W, SCREEN_H);

// ------------------------- COLOR SYSTEM -----------------------
constexpr uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b) {
  return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}

const uint16_t COL_BG        = rgb565(5, 10, 18);
const uint16_t COL_FACE      = rgb565(9, 20, 32);
const uint16_t COL_FACE_HI   = rgb565(14, 29, 43);
const uint16_t COL_GOLD      = rgb565(221, 187, 126);
const uint16_t COL_GOLD_DIM  = rgb565(112, 91, 61);
const uint16_t COL_TICK      = rgb565(76, 99, 116);
const uint16_t COL_IVORY     = rgb565(239, 239, 229);
const uint16_t COL_MUTED     = rgb565(125, 146, 160);
const uint16_t COL_CORAL     = rgb565(222, 104, 99);
const uint16_t COL_GREEN     = rgb565(99, 195, 147);
const uint16_t COL_RED       = rgb565(222, 104, 99);

// -------------------------- RUNTIME ---------------------------
int prevSec = -1;
int prevMin = -1;
int prevHour24 = -1;
int prevDay = -1;
int prevMonth = -1;
int prevYear = -1;

float prevHourAngle = 0.0f;
float prevMinuteAngle = 0.0f;
float prevSecondAngle = 0.0f;
bool handsDrawn = false;

bool timeSynced = false;
bool lastWifiConnected = false;
unsigned long lastTickMs = 0;
unsigned long lastNtpAttemptMs = 0;

bool otaUploadStarted = false;
bool otaUploadFinished = false;
bool otaUploadFailed = false;
bool serviceScreenActive = false;

// ============================================================
// WEB CONSOLE
// General device maintenance UI - not tied to the clock module.
// Add new dashboard sections/routes here as the platform grows.
// ============================================================
const char* consoleHtml = R"rawliteral(
<!DOCTYPE html>
<html lang="id">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1, viewport-fit=cover">
  <meta name="theme-color" content="#080f19">
  <title>NEXUS | Device Console</title>
  <style>
    :root {
      color-scheme: dark;
      --bg: #080f19;
      --panel: #101b29;
      --panel-soft: #142234;
      --line: rgba(200,216,228,.12);
      --text: #f2f0e9;
      --muted: #94a6b5;
      --gold: #dfbd80;
      --gold-deep: #ad8950;
      --mint: #78c8ae;
      --red: #ef8580;
      --shadow: 0 28px 80px rgba(0,0,0,.42);
    }
    * { box-sizing: border-box; -webkit-tap-highlight-color: transparent; }
    body {
      margin: 0; min-height: 100vh;
      padding: max(26px,env(safe-area-inset-top)) 18px max(26px,env(safe-area-inset-bottom));
      display: grid; place-items: center; color: var(--text);
      font-family: Inter,ui-sans-serif,system-ui,-apple-system,BlinkMacSystemFont,"Segoe UI",sans-serif;
      background:
        radial-gradient(ellipse at 50% -15%,rgba(196,158,94,.15),transparent 48%),
        radial-gradient(ellipse at 100% 85%,rgba(67,133,143,.10),transparent 42%),
        linear-gradient(145deg,#080f19,#0b1420 55%,#080f19);
    }
    .shell { width: min(100%,490px); }
    .topline { display:flex;align-items:center;justify-content:space-between;gap:14px;margin:0 2px 25px; }
    .brand { display:flex;align-items:center;gap:11px;min-width:0; }
    .brand-mark {
      width:38px;height:38px;flex:0 0 38px;border:1px solid rgba(223,189,128,.55);
      border-radius:12px;display:grid;place-items:center;color:var(--gold);
      font-family:Georgia,serif;font-size:19px;box-shadow:inset 0 0 0 4px rgba(223,189,128,.06);
    }
    .brand-name { font-size:12px;font-weight:800;letter-spacing:.22em; }
    .brand-caption { margin-top:3px;color:var(--muted);font-size:9px;letter-spacing:.15em; }
    .device-state {
      display:inline-flex;align-items:center;gap:7px;padding:7px 10px;
      border:1px solid rgba(120,200,174,.2);border-radius:99px;
      color:var(--mint);background:rgba(120,200,174,.07);
      font-size:9px;font-weight:800;letter-spacing:.1em;white-space:nowrap;
    }
    .state-dot { width:6px;height:6px;border-radius:50%;background:var(--mint); }
    .intro { margin:0 2px 22px; }
    .eyebrow { color:var(--gold);font-size:10px;font-weight:800;letter-spacing:.2em;text-transform:uppercase; }
    h1 { margin:9px 0 9px;font-family:Georgia,"Times New Roman",serif;font-size:clamp(32px,8vw,41px);line-height:1.05;font-weight:500;letter-spacing:-.035em; }
    .intro p { max-width:420px;margin:0;color:var(--muted);font-size:13px;line-height:1.75; }
    .system-meta { display:grid;grid-template-columns:repeat(3,1fr);gap:8px;margin:0 0 15px; }
    .meta-item { min-width:0;padding:12px 11px;border:1px solid var(--line);border-radius:13px;background:rgba(16,27,41,.66); }
    .meta-label { color:var(--muted);font-size:8px;font-weight:800;letter-spacing:.13em; }
    .meta-value { margin-top:5px;color:var(--text);font-size:11px;font-weight:700;white-space:nowrap;overflow:hidden;text-overflow:ellipsis; }
    .card {
      position:relative;overflow:hidden;padding:21px;border:1px solid var(--line);border-radius:21px;
      background:linear-gradient(155deg,rgba(20,34,52,.96),rgba(13,24,37,.98));box-shadow:var(--shadow);
    }
    .card::before { content:"";position:absolute;inset:0 18% auto;height:1px;background:linear-gradient(90deg,transparent,rgba(223,189,128,.45),transparent); }
    .section-head { display:flex;align-items:center;justify-content:space-between;gap:12px;margin-bottom:15px; }
    .section-title { font-size:11px;font-weight:850;letter-spacing:.1em; }
    .section-tag { color:var(--gold);font-size:9px;letter-spacing:.12em; }
    .dropzone {
      min-height:166px;padding:21px 16px;border:1px dashed rgba(223,189,128,.43);border-radius:15px;
      display:flex;flex-direction:column;align-items:center;justify-content:center;text-align:center;
      cursor:pointer;background:rgba(5,12,20,.36);transition:border-color .18s ease,background .18s ease,transform .18s ease;
    }
    .dropzone:hover,.dropzone.dragover { border-color:var(--gold);background:rgba(223,189,128,.07);transform:translateY(-1px); }
    .upload-icon { width:44px;height:44px;display:grid;place-items:center;margin-bottom:11px;border-radius:13px;color:var(--gold);background:rgba(223,189,128,.1); }
    .upload-icon svg { width:22px;height:22px; }
    .drop-title { font-size:13px;font-weight:750; }
    .drop-hint { margin-top:6px;color:var(--muted);font-size:10px;line-height:1.55; }
    input[type=file] { position:absolute;width:1px;height:1px;opacity:0;pointer-events:none; }
    .file-row { display:none;align-items:center;gap:11px;margin-top:12px;padding:11px 12px;border:1px solid var(--line);border-radius:12px;background:rgba(5,12,20,.3); }
    .file-row.show { display:flex; }
    .file-symbol { flex:0 0 32px;width:32px;height:35px;display:grid;place-items:center;color:var(--gold);border-radius:8px;background:rgba(223,189,128,.1);font-size:9px;font-weight:850; }
    .file-info { flex:1;min-width:0; }
    .file-name { overflow:hidden;text-overflow:ellipsis;white-space:nowrap;font-size:11px;font-weight:700; }
    .file-size { margin-top:3px;color:var(--muted);font-size:10px; }
    .file-check { color:var(--mint);font-size:17px; }
    .button {
      width:100%;min-height:48px;margin-top:16px;padding:0 18px;border:0;border-radius:12px;
      color:#17140e;background:linear-gradient(110deg,#e8cb98,#c89e60);
      font:inherit;font-size:11px;font-weight:900;letter-spacing:.12em;cursor:pointer;
      box-shadow:0 9px 25px rgba(185,143,79,.17);transition:filter .18s ease,transform .18s ease;
    }
    .button:hover:not(:disabled) { filter:brightness(1.06);transform:translateY(-1px); }
    .button:active:not(:disabled) { transform:translateY(1px); }
    .button:disabled { cursor:not-allowed;opacity:.38;box-shadow:none; }
    .progress { display:none;margin-top:18px; }
    .progress.show { display:block; }
    .progress-head { display:flex;justify-content:space-between;gap:10px;margin-bottom:9px;color:var(--muted);font-size:9px;letter-spacing:.07em; }
    .progress-value { color:var(--gold);font-weight:850;font-variant-numeric:tabular-nums; }
    .track { height:6px;overflow:hidden;border-radius:99px;background:rgba(255,255,255,.09); }
    .fill { width:0;height:100%;border-radius:inherit;background:linear-gradient(90deg,#b58c53,#ead09d);transition:width .15s ease; }
    .status { min-height:18px;margin-top:13px;color:var(--muted);font-size:10px;line-height:1.6; }
    .status.ok { color:var(--mint); }
    .status.err { color:var(--red); }
    .notice { display:flex;gap:9px;align-items:flex-start;margin:14px 3px 0;color:#788b9b;font-size:10px;line-height:1.65; }
    .notice svg { flex:0 0 14px;width:14px;height:14px;margin-top:1px;color:var(--gold-deep); }
    .footer { margin-top:22px;text-align:center;color:#607181;font-size:9px;letter-spacing:.16em; }
    @media(max-width:380px) { .card{padding:16px}.system-meta{gap:6px}.meta-item{padding:10px 8px}.topline{margin-bottom:20px} }
    @media(prefers-reduced-motion:reduce) { *,*::before,*::after{transition:none!important} }
  </style>
</head>
<body>
  <main class="shell">
    <header class="topline">
      <div class="brand">
        <div class="brand-mark">N</div>
        <div><div class="brand-name">NEXUS</div><div class="brand-caption">PERSONAL SYSTEM PLATFORM</div></div>
      </div>
      <div class="device-state"><span class="state-dot"></span>LOCAL DEVICE</div>
    </header>

    <section class="intro">
      <div class="eyebrow">ESP32-S3 / SYSTEM MAINTENANCE</div>
      <h1>Device<br>Console.</h1>
      <p>Pusat pemeliharaan perangkat untuk instalasi firmware dan pengembangan sistem. Antarmuka ini dirancang sebagai fondasi bagi modul-modul yang akan ditambahkan berikutnya.</p>
    </section>

    <section class="system-meta" aria-label="Informasi platform">
      <div class="meta-item"><div class="meta-label">PLATFORM</div><div class="meta-value">ESP32-S3</div></div>
      <div class="meta-item"><div class="meta-label">CONNECTION</div><div class="meta-value">Wi-Fi OTA</div></div>
      <div class="meta-item"><div class="meta-label">PACKAGE</div><div class="meta-value">FIRMWARE .BIN</div></div>
    </section>

    <section class="card">
      <div class="section-head">
        <div class="section-title">FIRMWARE DEPLOYMENT</div>
        <div class="section-tag">SYSTEM SERVICE</div>
      </div>
      <form id="otaForm">
        <label class="dropzone" id="dropzone" for="fileInput">
          <span class="upload-icon">
            <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.6" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true">
              <path d="M12 16V4m0 0L7.5 8.5M12 4l4.5 4.5"/><path d="M4 16.5v2A1.5 1.5 0 0 0 5.5 20h13a1.5 1.5 0 0 0 1.5-1.5v-2"/>
            </svg>
          </span>
          <span class="drop-title">Pilih paket firmware</span>
          <span class="drop-hint">Ketuk untuk membuka Files atau seret berkas .bin ke area ini</span>
        </label>
        <input type="file" id="fileInput" name="update" accept=".bin,application/octet-stream">

        <div class="file-row" id="fileRow">
          <div class="file-symbol">BIN</div>
          <div class="file-info"><div class="file-name" id="fileName">-</div><div class="file-size" id="fileSize">-</div></div>
          <div class="file-check">â</div>
        </div>
        <button class="button" id="uploadBtn" type="submit" disabled>INSTALL FIRMWARE</button>
      </form>

      <div class="progress" id="progressWrap">
        <div class="progress-head"><span id="progressLabel">SENDING PACKAGE</span><span class="progress-value" id="progressPct">0%</span></div>
        <div class="track"><div class="fill" id="progressFill"></div></div>
      </div>
      <div class="status" id="statusMsg" role="status" aria-live="polite">Menunggu paket firmware.</div>
    </section>

    <div class="notice">
      <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.7" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true"><path d="M12 3 4.5 6v5.2c0 4.6 3.2 7.8 7.5 9.8 4.3-2 7.5-5.2 7.5-9.8V6L12 3Z"/><path d="M12 8v4m0 3h.01"/></svg>
      <span>Gunakan firmware yang sesuai dengan board dan konfigurasi partisi perangkat. Jangan matikan perangkat atau menutup halaman selama instalasi berlangsung.</span>
    </div>
    <div class="footer">NEXUS Â· PERSONAL DEVICE INTERFACE</div>
  </main>

  <script>
    const input = document.getElementById('fileInput');
    const dropzone = document.getElementById('dropzone');
    const fileRow = document.getElementById('fileRow');
    const fileName = document.getElementById('fileName');
    const fileSize = document.getElementById('fileSize');
    const uploadBtn = document.getElementById('uploadBtn');
    const form = document.getElementById('otaForm');
    const progressWrap = document.getElementById('progressWrap');
    const progressFill = document.getElementById('progressFill');
    const progressPct = document.getElementById('progressPct');
    const progressLabel = document.getElementById('progressLabel');
    const statusMsg = document.getElementById('statusMsg');
    let selectedFile = null;

    function formatBytes(bytes) {
      return bytes >= 1048576 ? (bytes / 1048576).toFixed(2) + ' MB' : (bytes / 1024).toFixed(1) + ' KB';
    }
    function setStatus(message, type = '') {
      statusMsg.className = 'status' + (type ? ' ' + type : '');
      statusMsg.textContent = message;
    }
    function selectFile(file) {
      if (!file) return;
      if (!/\.bin$/i.test(file.name)) {
        selectedFile = null;
        input.value = '';
        fileRow.classList.remove('show');
        uploadBtn.disabled = true;
        setStatus('Format tidak sesuai. Pilih berkas firmware .bin.', 'err');
        return;
      }
      selectedFile = file;
      fileName.textContent = file.name;
      fileSize.textContent = formatBytes(file.size);
      fileRow.classList.add('show');
      uploadBtn.disabled = false;
      progressWrap.classList.remove('show');
      setStatus('Paket firmware siap dipasang.');
    }
    input.addEventListener('change', () => selectFile(input.files[0]));
    ['dragenter','dragover'].forEach(name => dropzone.addEventListener(name, event => {
      event.preventDefault(); dropzone.classList.add('dragover');
    }));
    ['dragleave','drop'].forEach(name => dropzone.addEventListener(name, event => {
      event.preventDefault(); dropzone.classList.remove('dragover');
    }));
    dropzone.addEventListener('drop', event => {
      const file = event.dataTransfer.files[0];
      if (!file) return;
      try { input.files = event.dataTransfer.files; } catch (_) {}
      selectFile(file);
    });

    form.addEventListener('submit', event => {
      event.preventDefault();
      const file = selectedFile;
      if (!file || !/\.bin$/i.test(file.name)) return;

      const data = new FormData();
      data.append('update', file, file.name);
      const xhr = new XMLHttpRequest();
      uploadBtn.disabled = true;
      progressWrap.classList.add('show');
      progressFill.style.width = '0%';
      progressPct.textContent = '0%';
      progressLabel.textContent = 'SENDING PACKAGE';
      setStatus('Perangkat sedang menerima firmware. Jangan tutup halaman ini.');

      xhr.upload.addEventListener('progress', event => {
        if (!event.lengthComputable) return;
        const percent = Math.min(100, Math.round(event.loaded / event.total * 100));
        progressFill.style.width = percent + '%';
        progressPct.textContent = percent + '%';
        if (percent >= 100) {
          progressLabel.textContent = 'VERIFYING & INSTALLING';
          setStatus('Transfer selesai. Perangkat sedang memverifikasi firmware.');
        }
      });
      xhr.addEventListener('load', () => {
        if (xhr.status === 200 && /^OK\b/i.test((xhr.responseText || '').trim())) {
          progressFill.style.width = '100%';
          progressPct.textContent = '100%';
          progressLabel.textContent = 'INSTALLATION COMPLETE';
          setStatus('Firmware berhasil dipasang. Perangkat akan memulai ulang otomatis.', 'ok');
        } else {
          setStatus('Instalasi gagal: ' + ((xhr.responseText || 'respons perangkat tidak valid').trim()), 'err');
          uploadBtn.disabled = false;
        }
      });
      xhr.addEventListener('error', () => {
        setStatus('Koneksi terputus. Periksa Wi-Fi dan kondisi perangkat.', 'err');
        uploadBtn.disabled = false;
      });
      xhr.addEventListener('timeout', () => {
        setStatus('Waktu koneksi habis. Periksa perangkat sebelum mencoba kembali.', 'err');
        uploadBtn.disabled = false;
      });
      xhr.open('POST', '/update', true);
      xhr.timeout = 180000;
      xhr.send(data);
    });
  </script>
</body>
</html>
)rawliteral";

// ============================================================
// DISPLAY MODULE
// ============================================================

// Draws the still clock face into the canvas. This function is called
// only when the interface is first shown or restored after OTA service.
void drawStaticScene() {
  faceCanvas.fillScreen(COL_BG);
  faceCanvas.setTextWrap(false);

  // Header identity
  faceCanvas.setTextSize(1);
  faceCanvas.setTextColor(COL_GOLD);
  faceCanvas.setCursor(8, 6);
  faceCanvas.print(DEVICE_NAME);
  faceCanvas.setTextColor(COL_MUTED);
  faceCanvas.setCursor(47, 6);
  faceCanvas.print("/ CORE");
  faceCanvas.drawFastHLine(7, 18, SCREEN_W - 14, COL_GOLD_DIM);

  // Bezel and dial
  faceCanvas.fillCircle(CLK_CX, CLK_CY, CLK_R + 1, COL_GOLD_DIM);
  faceCanvas.fillCircle(CLK_CX, CLK_CY, CLK_R, COL_FACE);
  faceCanvas.drawCircle(CLK_CX, CLK_CY, CLK_R - 1, COL_GOLD);
  faceCanvas.drawCircle(CLK_CX, CLK_CY, CLK_R - 3, COL_FACE_HI);
  faceCanvas.drawCircle(CLK_CX, CLK_CY, 25, COL_GOLD_DIM);

  // 60 fixed minute markers. No part of this dial is animated.
  for (int i = 0; i < 60; ++i) {
    const float rad = (i * 6.0f - 90.0f) * (PI / 180.0f);
    const bool major = (i % 5 == 0);
    const int outerR = CLK_R - 5;
    const int innerR = major ? (CLK_R - 12) : (CLK_R - 8);
    const int x1 = CLK_CX + (int)lroundf(cosf(rad) * innerR);
    const int y1 = CLK_CY + (int)lroundf(sinf(rad) * innerR);
    const int x2 = CLK_CX + (int)lroundf(cosf(rad) * outerR);
    const int y2 = CLK_CY + (int)lroundf(sinf(rad) * outerR);
    faceCanvas.drawLine(x1, y1, x2, y2, major ? COL_GOLD : COL_TICK);
  }

  // Cardinal numerals
  faceCanvas.setTextSize(1);
  faceCanvas.setTextColor(COL_GOLD);
  faceCanvas.setCursor(58, 43); faceCanvas.print("12");
  faceCanvas.setCursor(92, 70); faceCanvas.print("3");
  faceCanvas.setCursor(61, 99); faceCanvas.print("6");
  faceCanvas.setCursor(31, 70); faceCanvas.print("9");

  // Footer divider is part of the still background.
  faceCanvas.drawFastHLine(18, 126, SCREEN_W - 36, COL_GOLD_DIM);
}

void presentStaticScene() {
  tft.drawRGBBitmap(0, 0, faceCanvas.getBuffer(), SCREEN_W, SCREEN_H);
}

void drawNetworkStatus() {
  // This small status field is independent of the dial and only changes
  // when Wi-Fi connectivity changes.
  tft.fillRect(91, 3, 34, 13, COL_BG);
  const bool connected = WiFi.status() == WL_CONNECTED;
  tft.fillCircle(95, 9, 2, connected ? COL_GREEN : COL_MUTED);
  tft.setTextWrap(false);
  tft.setTextSize(1);
  tft.setTextColor(connected ? COL_GREEN : COL_MUTED);
  tft.setCursor(101, 6);
  tft.print(connected ? "NET" : "OFF");
}

// Draw or restore a hand using the same integer raster path. In restore
// mode, each old hand pixel is replaced by the corresponding pixel from
// the static canvas, preserving the dial under it without a full redraw.
void plotHand(float angleDeg, int length, uint16_t color, uint8_t thickness, bool restore) {
  const float rad = (angleDeg - 90.0f) * (PI / 180.0f);
  const int xEnd = CLK_CX + (int)lroundf(cosf(rad) * length);
  const int yEnd = CLK_CY + (int)lroundf(sinf(rad) * length);

  const float perp = rad + (PI / 2.0f);
  const int ox = (int)lroundf(cosf(perp));
  const int oy = (int)lroundf(sinf(perp));
  const int halfWidth = (thickness - 1) / 2;

  int x = CLK_CX;
  int y = CLK_CY;
  const int dx = abs(xEnd - x);
  const int sx = (x < xEnd) ? 1 : -1;
  const int dy = -abs(yEnd - y);
  const int sy = (y < yEnd) ? 1 : -1;
  int err = dx + dy;

  while (true) {
    for (int offset = -halfWidth; offset <= halfWidth; ++offset) {
      const int px = x + ox * offset;
      const int py = y + oy * offset;
      if (px < 0 || px >= SCREEN_W || py < 0 || py >= SCREEN_H) continue;

      if (restore) {
        tft.drawPixel(px, py, faceCanvas.getPixel(px, py));
      } else {
        tft.drawPixel(px, py, color);
      }
    }

    if (x == xEnd && y == yEnd) break;
    const int twiceErr = 2 * err;
    if (twiceErr >= dy) { err += dy; x += sx; }
    if (twiceErr <= dx) { err += dx; y += sy; }
  }
}

void drawHandHub() {
  tft.fillCircle(CLK_CX, CLK_CY, 4, COL_GOLD_DIM);
  tft.fillCircle(CLK_CX, CLK_CY, 2, COL_IVORY);
  tft.drawPixel(CLK_CX, CLK_CY, COL_CORAL);
}

void renderHands(int hour24, int minute, int second) {
  const float hourAngle = ((hour24 % 12) + minute / 60.0f + second / 3600.0f) * 30.0f;
  const float minuteAngle = (minute + second / 60.0f) * 6.0f;
  const float secondAngle = second * 6.0f;

  if (handsDrawn) {
    // Restore only the former hand strokes. The face and its markers remain still.
    plotHand(prevHourAngle,   CLK_R - 23, COL_GOLD,  3, true);
    plotHand(prevMinuteAngle, CLK_R - 14, COL_IVORY, 3, true);
    plotHand(prevSecondAngle, CLK_R - 7,  COL_CORAL, 1, true);
  }

  plotHand(hourAngle,   CLK_R - 23, COL_GOLD,  3, false);
  plotHand(minuteAngle, CLK_R - 14, COL_IVORY, 3, false);
  plotHand(secondAngle, CLK_R - 7,  COL_CORAL, 1, false);
  drawHandHub();

  prevHourAngle = hourAngle;
  prevMinuteAngle = minuteAngle;
  prevSecondAngle = secondAngle;
  handsDrawn = true;
}

const char* dayNameShort(int day) {
  static const char* names[] = {"MIN", "SEN", "SEL", "RAB", "KAM", "JUM", "SAB"};
  return names[(day >= 0 && day <= 6) ? day : 0];
}

const char* monthNameShort(int month) {
  static const char* names[] = {"JAN", "FEB", "MAR", "APR", "MEI", "JUN", "JUL", "AGU", "SEP", "OKT", "NOV", "DES"};
  return names[(month >= 0 && month <= 11) ? month : 0];
}

void renderFooter(const struct tm& t, bool forceRedraw) {
  const bool timeChanged = forceRedraw || t.tm_hour != prevHour24 || t.tm_min != prevMin;
  const bool dateChanged = forceRedraw || t.tm_mday != prevDay ||
                           t.tm_mon != prevMonth || (t.tm_year + 1900) != prevYear;

  if (timeChanged) {
    tft.fillRect(24, 130, 82, 18, COL_BG);
    char timeText[6];
    snprintf(timeText, sizeof(timeText), "%02d:%02d", t.tm_hour, t.tm_min);
    tft.setTextSize(2);
    tft.setTextColor(COL_IVORY);
    tft.setCursor(31, 131);
    tft.print(timeText);

    tft.setTextSize(1);
    tft.setTextColor(COL_GOLD);
    tft.setCursor(94, 137);
    tft.print("WIB");
  }

  if (dateChanged) {
    tft.fillRect(7, 150, 114, 10, COL_BG);
    char dateText[24];
    snprintf(dateText, sizeof(dateText), "%s %02d %s %04d",
             dayNameShort(t.tm_wday), t.tm_mday,
             monthNameShort(t.tm_mon), t.tm_year + 1900);
    tft.setTextSize(1);
    tft.setTextColor(COL_MUTED);
    tft.setCursor(8, 151);
    tft.print(dateText);
  }
}

void renderWaitingFooter() {
  tft.fillRect(15, 131, 98, 28, COL_BG);
  tft.setTextSize(2);
  tft.setTextColor(COL_IVORY);
  tft.setCursor(32, 131);
  tft.print("--:--");
  tft.setTextSize(1);
  tft.setTextColor(COL_GOLD);
  tft.setCursor(94, 137);
  tft.print("WIB");
  tft.setTextColor(COL_MUTED);
  tft.setCursor(31, 151);
  tft.print("WAITING FOR NTP");
}

void restoreMainDisplay() {
  serviceScreenActive = false;
  drawStaticScene();
  presentStaticScene();
  drawNetworkStatus();
  handsDrawn = false;

  struct tm timeinfo;
  if (getLocalTime(&timeinfo, 100) && timeinfo.tm_year >= (2024 - 1900)) {
    renderHands(timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
    renderFooter(timeinfo, true);
    prevHour24 = timeinfo.tm_hour;
    prevMin = timeinfo.tm_min;
    prevSec = timeinfo.tm_sec;
    prevDay = timeinfo.tm_mday;
    prevMonth = timeinfo.tm_mon;
    prevYear = timeinfo.tm_year + 1900;
  } else {
    renderWaitingFooter();
  }
}

// ============================================================
// SERVICE DISPLAY (shown only during OTA maintenance)
// ============================================================
void drawServiceScreen(const char* title, const char* subtitle, int percent, bool success = false) {
  serviceScreenActive = true;
  tft.fillScreen(COL_BG);
  tft.setTextWrap(false);
  tft.setTextSize(1);
  tft.setTextColor(COL_GOLD);
  tft.setCursor(9, 10);
  tft.print(DEVICE_NAME);
  tft.print(" / SYSTEM SERVICE");
  tft.drawFastHLine(8, 23, SCREEN_W - 16, COL_GOLD_DIM);

  tft.setTextColor(success ? COL_GREEN : COL_IVORY);
  tft.setCursor(12, 44);
  tft.print(title);
  tft.setTextColor(COL_MUTED);
  tft.setCursor(12, 59);
  tft.print(subtitle);

  tft.setTextSize(2);
  tft.setTextColor(success ? COL_GREEN : COL_GOLD);
  tft.setCursor(percent >= 0 ? 44 : 49, 84);
  if (percent >= 0) {
    char value[8];
    snprintf(value, sizeof(value), "%d%%", constrain(percent, 0, 100));
    tft.print(value);
  } else {
    tft.print("RUN");
  }

  tft.drawRoundRect(10, 113, 108, 10, 3, COL_GOLD_DIM);
  const int fillWidth = (percent < 0) ? 48 : constrain((104 * percent) / 100, 0, 104);
  if (fillWidth > 0) {
    tft.fillRoundRect(12, 115, fillWidth, 6, 2, success ? COL_GREEN : COL_GOLD);
  }

  tft.setTextSize(1);
  tft.setTextColor(COL_MUTED);
  tft.setCursor(15, 141);
  tft.print(success ? "RESTARTING DEVICE..." : "KEEP POWER CONNECTED");
}

// ============================================================
// NETWORK & TIME SERVICES
// ============================================================
void connectWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.persistent(false);
  WiFi.begin(WIFI_SSID, WIFI_PASS);

  Serial.print("Connecting to Wi-Fi");
  const unsigned long started = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - started < 15000) {
    delay(250);
    Serial.print('.');
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("Wi-Fi connected. IP: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("Wi-Fi not available. The device will retry automatically.");
  }
}

void syncNTPTime() {
  if (WiFi.status() != WL_CONNECTED) return;

  lastNtpAttemptMs = millis();
  configTime(GMT_OFFSET_SEC, DAYLIGHT_OFFSET_SEC,
             "pool.ntp.org", "time.google.com", "time.nist.gov");

  struct tm timeinfo;
  const unsigned long started = millis();
  while (!getLocalTime(&timeinfo, 250) && millis() - started < 8000) {
    delay(150);
  }

  if (getLocalTime(&timeinfo, 250) && timeinfo.tm_year >= (2024 - 1900)) {
    timeSynced = true;
    Serial.println("Time synchronized (WIB).");
  } else {
    Serial.println("NTP synchronization pending; will retry later.");
  }
}

// ============================================================
// WEB SERVICE / OTA
// Add future HTTP routes in this function, grouped by module.
// ============================================================
bool requireOtaAuth() {
  if (server.authenticate(OTA_USER, OTA_PASS)) return true;
  server.requestAuthentication();
  return false;
}

void setupWebConsole() {
  server.on("/", HTTP_GET, []() {
    if (!requireOtaAuth()) return;
    server.sendHeader("Cache-Control", "no-store");
    server.send(200, "text/html; charset=utf-8", consoleHtml);
  });

  server.on("/update", HTTP_POST, []() {
    if (!requireOtaAuth()) return;

    const bool success = otaUploadFinished && !otaUploadFailed && !Update.hasError();
    server.sendHeader("Connection", "close");
    server.send( success ? 200 : 500,
                 "text/plain",
                 success ? "OK" : "FAIL: firmware installation failed" );

    if (success) {
      drawServiceScreen("UPDATE COMPLETE", "Firmware installed successfully", 100, true);
      delay(700);
      ESP.restart();
    } else {
      drawServiceScreen("UPDATE FAILED", "Check firmware and try again", 0, false);
      otaUploadStarted = false;
    }
  }, []() {
    if (!requireOtaAuth()) return;

    HTTPUpload& upload = server.upload();

    if (upload.status == UPLOAD_FILE_START) {
      otaUploadStarted = false;
      otaUploadFinished = false;
      otaUploadFailed = false;

      String filename = upload.filename;
      filename.toLowerCase();
      if (!filename.endsWith(".bin")) {
        otaUploadFailed = true;
        Serial.println("OTA rejected: file extension is not .bin");
        drawServiceScreen("INVALID PACKAGE", "Select a .bin firmware file", 0, false);
        return;
      }

      Serial.printf("OTA start: %s\n", upload.filename.c_str());
      drawServiceScreen("PREPARING UPDATE", "Opening firmware partition...", -1, false);

      if (Update.begin(UPDATE_SIZE_UNKNOWN)) {
        otaUploadStarted = true;
      } else {
        otaUploadFailed = true;
        Update.printError(Serial);
        drawServiceScreen("UPDATE ERROR", "Cannot open firmware partition", 0, false);
      }
    }
    else if (upload.status == UPLOAD_FILE_WRITE) {
      if (!otaUploadStarted || otaUploadFailed) return;

      const size_t written = Update.write(upload.buf, upload.currentSize);
      if (written != upload.currentSize) {
        otaUploadFailed = true;
        Update.printError(Serial);
        drawServiceScreen("WRITE ERROR", "Firmware could not be written", 0, false);
      } else {
        drawServiceScreen("INSTALLING", "Writing firmware to flash...", -1, false);
      }
    }
    else if (upload.status == UPLOAD_FILE_END) {
      if (otaUploadStarted && !otaUploadFailed && Update.end(true)) {
        otaUploadFinished = true;
        Serial.printf("OTA success: %u bytes\n", upload.totalSize);
        drawServiceScreen("VERIFYING", "Firmware image accepted", 100, true);
      } else {
        otaUploadFailed = true;
        otaUploadFinished = false;
        Update.printError(Serial);
        drawServiceScreen("UPDATE FAILED", "Firmware verification failed", 0, false);
      }
    }
    else if (upload.status == UPLOAD_FILE_ABORTED) {
      otaUploadFailed = true;
      otaUploadFinished = false;
      if (otaUploadStarted) Update.end();
      otaUploadStarted = false;
      Serial.println("OTA upload aborted.");
      drawServiceScreen("UPLOAD ABORTED", "No firmware was installed", 0, false);
    }
  });

  server.begin();
  Serial.println("NEXUS device console started on port 80.");
}

// ============================================================
// ARDUINO ENTRY POINTS
// ============================================================
void setup() {
  Serial.begin(115200);

  tft.initR(INITR_BLACKTAB);
  tft.setRotation(0);
  tft.fillScreen(COL_BG);
  tft.setTextWrap(false);

  connectWiFi();
  setupWebConsole();
  if (WiFi.status() == WL_CONNECTED) syncNTPTime();

  restoreMainDisplay();
  lastWifiConnected = (WiFi.status() == WL_CONNECTED);
}

void loop() {
  server.handleClient();

  // Keep OTA/service feedback stable; do not draw clock hands over it.
  if (serviceScreenActive) {
    delay(5);
    return;
  }

  const bool wifiConnected = (WiFi.status() == WL_CONNECTED);
  if (wifiConnected != lastWifiConnected) {
    lastWifiConnected = wifiConnected;
    drawNetworkStatus();
    if (wifiConnected) {
      Serial.print("Wi-Fi reconnected. IP: ");
      Serial.println(WiFi.localIP());
    }
  }

  const unsigned long now = millis();
  if (wifiConnected && !timeSynced && now - lastNtpAttemptMs > 60000UL) {
    syncNTPTime();
  }

  if (now - lastTickMs >= 1000UL) {
    lastTickMs = now;

    struct tm timeinfo;
    if (getLocalTime(&timeinfo, 50) && timeinfo.tm_year >= (2024 - 1900)) {
      const bool secondChanged = timeinfo.tm_sec != prevSec;
      const bool minuteChanged = timeinfo.tm_min != prevMin;
      const bool hourChanged = timeinfo.tm_hour != prevHour24;

      if (secondChanged || minuteChanged || hourChanged || !handsDrawn) {
        renderHands(timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
        renderFooter(timeinfo, false);

        prevHour24 = timeinfo.tm_hour;
        prevMin = timeinfo.tm_min;
        prevSec = timeinfo.tm_sec;
        prevDay = timeinfo.tm_mday;
        prevMonth = timeinfo.tm_mon;
        prevYear = timeinfo.tm_year + 1900;
      }
      timeSynced = true;
    }
  }

  delay(5);
}
