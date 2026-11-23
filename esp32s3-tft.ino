#include <Adafruit_GFX.h>
#include <Adafruit_ST7735.h>
#include <SPI.h>
#include <WiFi.h>
#include <WebServer.h>
#include <Update.h>
#include <time.h>
#include <math.h>

// ============================================================
// A U R A  |  ESP32-S3 Analog Clock + Wireless OTA
// Visual direction: midnight navy, champagne gold, warm ivory.
// ============================================================

// Ganti dengan kredensial Wi-Fi milik Anda.
const char* WIFI_SSID = "Qwerty1";
const char* WIFI_PASS = "zxcvbnm123";

// Ubah kedua nilai ini. Browser akan meminta autentikasi saat
// membuka halaman OTA maupun saat mengirim firmware.
const char* OTA_USER = "admin";
const char* OTA_PASS = "111115";

// WIB = UTC+7
const long GMT_OFFSET_SEC = 7L * 3600L;
const int DAYLIGHT_OFFSET_SEC = 0;

#define TFT_CS    13
#define TFT_RST   9
#define TFT_DC    10
#define TFT_MOSI  11
#define TFT_SCLK  12

Adafruit_ST7735 tft(TFT_CS, TFT_DC, TFT_MOSI, TFT_SCLK, TFT_RST);
WebServer server(80);

constexpr int SCREEN_W = 128;
constexpr int SCREEN_H = 160;
constexpr int CLK_CX = 64;
constexpr int CLK_CY = 76;
constexpr int CLK_R  = 46;

// RGB565 palette
constexpr uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b) {
  return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}

const uint16_t COL_BG       = rgb565(5, 10, 18);
const uint16_t COL_FACE     = rgb565(9, 20, 32);
const uint16_t COL_FACE_HI  = rgb565(13, 27, 41);
const uint16_t COL_GOLD     = rgb565(221, 187, 126);
const uint16_t COL_GOLD_DIM = rgb565(112, 91, 61);
const uint16_t COL_TICK     = rgb565(79, 104, 124);
const uint16_t COL_IVORY    = rgb565(239, 239, 229);
const uint16_t COL_MUTED    = rgb565(125, 146, 160);
const uint16_t COL_CYAN     = rgb565(101, 194, 199);
const uint16_t COL_RED      = rgb565(222, 104, 99);
const uint16_t COL_GREEN    = rgb565(99, 195, 147);

int prevSec = -1;
int prevMin = -1;
int prevHour24 = -1;
int prevDay = -1;
int prevMonth = -1;
int prevYear = -1;
unsigned long lastTickMs = 0;
bool timeSynced = false;
bool lastWifiConnected = false;
bool otaReady = false;

// ------------------------------------------------------------
// OTA WEB PAGE
// ------------------------------------------------------------
const char* otaPageHtml = R"rawliteral(
<!DOCTYPE html>
<html lang="id">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1, viewport-fit=cover">
  <meta name="theme-color" content="#080f19">
  <title>Meridian | Firmware Studio</title>
  <style>
    :root {
      color-scheme: dark;
      --bg: #080f19;
      --panel: #101b29;
      --panel-soft: #142234;
      --line: rgba(200, 216, 228, .12);
      --text: #f2f0e9;
      --muted: #94a6b5;
      --gold: #dfbd80;
      --gold-deep: #ad8950;
      --mint: #78c8ae;
      --red: #ef8580;
      --shadow: 0 28px 80px rgba(0, 0, 0, .42);
    }
    * { box-sizing: border-box; -webkit-tap-highlight-color: transparent; }
    body {
      margin: 0;
      min-height: 100vh;
      padding: max(28px, env(safe-area-inset-top)) 18px max(28px, env(safe-area-inset-bottom));
      display: grid;
      place-items: center;
      color: var(--text);
      font-family: Inter, ui-sans-serif, system-ui, -apple-system, BlinkMacSystemFont, "Segoe UI", sans-serif;
      background:
        radial-gradient(ellipse at 50% -15%, rgba(196, 158, 94, .16), transparent 48%),
        radial-gradient(ellipse at 100% 85%, rgba(67, 133, 143, .10), transparent 42%),
        linear-gradient(145deg, #080f19, #0b1420 55%, #080f19);
    }
    .shell { width: min(100%, 470px); }
    .topline {
      display: flex; align-items: center; justify-content: space-between;
      margin: 0 2px 22px;
    }
    .brand { display: flex; align-items: center; gap: 11px; }
    .brand-mark {
      width: 36px; height: 36px; border: 1px solid rgba(223,189,128,.55);
      border-radius: 50%; display: grid; place-items: center; position: relative;
      color: var(--gold); font-family: Georgia, serif; font-size: 18px;
      box-shadow: inset 0 0 0 4px rgba(223,189,128,.06);
    }
    .brand-mark::after { content: ""; position: absolute; width: 3px; height: 3px; border-radius: 50%; background: var(--gold); }
    .brand-name { font-size: 12px; font-weight: 750; letter-spacing: .22em; }
    .brand-caption { margin-top: 3px; color: var(--muted); font-size: 9px; letter-spacing: .18em; }
    .live {
      display: inline-flex; align-items: center; gap: 7px; padding: 7px 10px;
      border: 1px solid rgba(120,200,174,.2); border-radius: 99px;
      color: var(--mint); background: rgba(120,200,174,.07);
      font-size: 10px; font-weight: 700; letter-spacing: .12em;
    }
    .live-dot { width: 6px; height: 6px; border-radius: 50%; background: var(--mint); box-shadow: 0 0 10px rgba(120,200,174,.8); }
    .intro { margin: 0 2px 22px; }
    .eyebrow { color: var(--gold); font-size: 10px; font-weight: 750; letter-spacing: .2em; text-transform: uppercase; }
    h1 { margin: 9px 0 8px; font-family: Georgia, "Times New Roman", serif; font-size: clamp(31px, 8vw, 39px); line-height: 1.06; font-weight: 500; letter-spacing: -.035em; }
    .intro p { max-width: 390px; margin: 0; color: var(--muted); font-size: 13px; line-height: 1.7; }
    .card {
      padding: 21px; border: 1px solid var(--line); border-radius: 22px;
      background: linear-gradient(155deg, rgba(20,34,52,.96), rgba(13,24,37,.98));
      box-shadow: var(--shadow); position: relative; overflow: hidden;
    }
    .card::before { content: ""; position: absolute; inset: 0 18% auto; height: 1px; background: linear-gradient(90deg, transparent, rgba(223,189,128,.45), transparent); }
    .section-head { display: flex; align-items: center; justify-content: space-between; gap: 12px; margin-bottom: 15px; }
    .section-title { font-size: 12px; font-weight: 750; letter-spacing: .08em; }
    .file-type { color: var(--gold); font-size: 10px; letter-spacing: .12em; }
    .dropzone {
      min-height: 174px; padding: 22px 16px; border: 1px dashed rgba(223,189,128,.43);
      border-radius: 16px; display: flex; flex-direction: column; align-items: center;
      justify-content: center; text-align: center; cursor: pointer;
      background: rgba(5,12,20,.36); transition: .2s ease;
    }
    .dropzone:hover, .dropzone.dragover { border-color: var(--gold); background: rgba(223,189,128,.07); transform: translateY(-1px); }
    .upload-icon { width: 45px; height: 45px; display: grid; place-items: center; margin-bottom: 12px; border-radius: 14px; color: var(--gold); background: rgba(223,189,128,.1); }
    .upload-icon svg { width: 22px; height: 22px; }
    .drop-title { font-size: 13px; font-weight: 700; }
    .drop-hint { margin-top: 6px; color: var(--muted); font-size: 11px; line-height: 1.5; }
    input[type=file] { position: absolute; width: 1px; height: 1px; opacity: 0; pointer-events: none; }
    .file-row {
      display: none; align-items: center; gap: 11px; margin-top: 12px; padding: 11px 12px;
      border: 1px solid var(--line); border-radius: 13px; background: rgba(5,12,20,.3);
    }
    .file-row.show { display: flex; }
    .file-symbol { flex: 0 0 32px; width: 32px; height: 36px; display: grid; place-items: center; color: var(--gold); border-radius: 8px; background: rgba(223,189,128,.1); font-size: 9px; font-weight: 800; }
    .file-info { min-width: 0; flex: 1; }
    .file-name { overflow: hidden; text-overflow: ellipsis; white-space: nowrap; font-size: 12px; font-weight: 650; }
    .file-size { margin-top: 3px; color: var(--muted); font-size: 10px; }
    .file-check { color: var(--mint); font-size: 17px; }
    .button {
      width: 100%; min-height: 49px; margin-top: 16px; padding: 0 18px; border: 0;
      border-radius: 13px; color: #17140e; background: linear-gradient(110deg, #e8cb98, #c89e60);
      font: inherit; font-size: 12px; font-weight: 850; letter-spacing: .12em;
      cursor: pointer; box-shadow: 0 9px 25px rgba(185,143,79,.17); transition: .18s ease;
    }
    .button:hover:not(:disabled) { filter: brightness(1.06); transform: translateY(-1px); }
    .button:active:not(:disabled) { transform: translateY(1px); }
    .button:disabled { cursor: not-allowed; opacity: .38; box-shadow: none; }
    .progress { display: none; margin-top: 19px; }
    .progress.show { display: block; }
    .progress-head { display: flex; justify-content: space-between; gap: 10px; margin-bottom: 9px; color: var(--muted); font-size: 10px; letter-spacing: .06em; }
    .progress-value { color: var(--gold); font-weight: 800; font-variant-numeric: tabular-nums; }
    .track { height: 6px; overflow: hidden; border-radius: 99px; background: rgba(255,255,255,.09); }
    .fill { width: 0; height: 100%; border-radius: inherit; background: linear-gradient(90deg, #b58c53, #ead09d); transition: width .16s ease; }
    .status { min-height: 18px; margin-top: 13px; color: var(--muted); font-size: 11px; line-height: 1.55; }
    .status.ok { color: var(--mint); }
    .status.err { color: var(--red); }
    .footnote { display: flex; gap: 9px; align-items: flex-start; margin: 15px 3px 0; color: #778a9b; font-size: 10px; line-height: 1.6; }
    .footnote svg { flex: 0 0 14px; width: 14px; height: 14px; margin-top: 1px; color: var(--gold-deep); }
    .copyright { margin-top: 22px; text-align: center; color: #607181; font-size: 9px; letter-spacing: .16em; }
    @media (max-width: 380px) { .card { padding: 16px; } .topline { margin-bottom: 18px; } }
    @media (prefers-reduced-motion: reduce) { *, *::before, *::after { transition: none !important; } }
  </style>
</head>
<body>
  <main class="shell">
    <header class="topline">
      <div class="brand">
        <div class="brand-mark">M</div>
        <div><div class="brand-name">MERIDIAN</div><div class="brand-caption">PRECISION TIMEKEEPING</div></div>
      </div>
      <div class="live"><span class="live-dot"></span>DEVICE READY</div>
    </header>

    <section class="intro">
      <div class="eyebrow">ESP32-S3 / WIRELESS SERVICE</div>
      <h1>Firmware<br>Control Studio.</h1>
      <p>Perbarui perangkat secara nirkabel dengan firmware terbaru. Pilih berkas hasil kompilasi, lalu mulai proses instalasi.</p>
    </section>

    <section class="card">
      <div class="section-head">
        <div class="section-title">FIRMWARE PACKAGE</div>
        <div class="file-type">BINARY Â· .BIN</div>
      </div>

      <form id="otaForm">
        <label class="dropzone" id="dropzone" for="fileInput">
          <span class="upload-icon">
            <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.6" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true">
              <path d="M12 16V4m0 0L7.5 8.5M12 4l4.5 4.5"/><path d="M4 16.5v2A1.5 1.5 0 0 0 5.5 20h13a1.5 1.5 0 0 0 1.5-1.5v-2"/>
            </svg>
          </span>
          <span class="drop-title">Pilih firmware untuk diunggah</span>
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
        <div class="progress-head"><span id="progressLabel">MENGIRIM FIRMWARE</span><span class="progress-value" id="progressPct">0%</span></div>
        <div class="track"><div class="fill" id="progressFill"></div></div>
      </div>
      <div class="status" id="statusMsg" role="status" aria-live="polite">Menunggu berkas firmware.</div>
    </section>

    <div class="footnote">
      <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.7" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true"><path d="M12 3 4.5 6v5.2c0 4.6 3.2 7.8 7.5 9.8 4.3-2 7.5-5.2 7.5-9.8V6L12 3Z"/><path d="M12 8v4m0 3h.01"/></svg>
      <span>Pastikan berkas berasal dari build ESP32-S3 yang benar. Jangan matikan perangkat atau menutup halaman selama instalasi berlangsung.</span>
    </div>
    <div class="copyright">MERIDIAN Â· LOCAL DEVICE SERVICE</div>
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
        setStatus('Format tidak sesuai. Pilih firmware dengan ekstensi .bin.', 'err');
        return;
      }
      selectedFile = file;
      fileName.textContent = file.name;
      fileSize.textContent = formatBytes(file.size);
      fileRow.classList.add('show');
      uploadBtn.disabled = false;
      progressWrap.classList.remove('show');
      setStatus('Firmware siap dipasang.');
    }

    input.addEventListener('change', () => selectFile(input.files[0]));
    ['dragenter', 'dragover'].forEach(eventName => dropzone.addEventListener(eventName, event => {
      event.preventDefault(); dropzone.classList.add('dragover');
    }));
    ['dragleave', 'drop'].forEach(eventName => dropzone.addEventListener(eventName, event => {
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
      progressLabel.textContent = 'MENGIRIM FIRMWARE';
      setStatus('Perangkat sedang menerima firmware. Jangan tutup halaman ini.');

      xhr.upload.addEventListener('progress', event => {
        if (!event.lengthComputable) return;
        const pct = Math.min(100, Math.round((event.loaded / event.total) * 100));
        progressFill.style.width = pct + '%';
        progressPct.textContent = pct + '%';
        if (pct >= 100) {
          progressLabel.textContent = 'MEMVERIFIKASI & MEMASANG';
          setStatus('Transfer selesai. Perangkat sedang memverifikasi firmware.');
        }
      });

      xhr.addEventListener('load', () => {
        if (xhr.status === 200 && /^OK/i.test((xhr.responseText || '').trim())) {
          progressFill.style.width = '100%';
          progressPct.textContent = '100%';
          progressLabel.textContent = 'INSTALASI SELESAI';
          setStatus('Firmware berhasil dipasang. Perangkat akan memulai ulang secara otomatis.', 'ok');
        } else {
          setStatus('Instalasi gagal: ' + ((xhr.responseText || 'respons perangkat tidak valid').trim()), 'err');
          uploadBtn.disabled = false;
        }
      });

      xhr.addEventListener('error', () => {
        setStatus('Koneksi terputus. Periksa Wi-Fi dan status perangkat, lalu coba kembali.', 'err');
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

// ------------------------------------------------------------
// DISPLAY HELPERS
// ------------------------------------------------------------
void drawHeader() {
  tft.fillRect(0, 0, SCREEN_W, 20, COL_BG);
  tft.fillCircle(8, 9, 3, COL_GOLD_DIM);
  tft.fillCircle(8, 9, 1, COL_GOLD);

  tft.setTextWrap(false);
  tft.setTextSize(1);
  tft.setTextColor(COL_IVORY);
  tft.setCursor(16, 6);
  tft.print("MERIDIAN");

  tft.setTextColor(WiFi.status() == WL_CONNECTED ? COL_GREEN : COL_MUTED);
  tft.setCursor(100, 6);
  tft.print(WiFi.status() == WL_CONNECTED ? "NET" : "OFF");

  tft.drawFastHLine(7, 19, SCREEN_W - 14, COL_GOLD_DIM);
}

void drawClockFace() {
  // Hanya area dial yang dibersihkan; header dan footer tidak berkedip.
  tft.fillCircle(CLK_CX, CLK_CY, CLK_R + 2, COL_BG);
  tft.drawCircle(CLK_CX, CLK_CY, CLK_R + 1, COL_GOLD_DIM);
  tft.fillCircle(CLK_CX, CLK_CY, CLK_R, COL_FACE);
  tft.drawCircle(CLK_CX, CLK_CY, CLK_R - 1, COL_GOLD);
  tft.drawCircle(CLK_CX, CLK_CY, CLK_R - 3, COL_FACE_HI);
  tft.drawCircle(CLK_CX, CLK_CY, 29, COL_GOLD_DIM);

  // 60 indeks menit: indeks lima-menit dibuat lebih panjang dan tegas.
  for (int i = 0; i < 60; ++i) {
    const float rad = (i * 6.0f - 90.0f) * (PI / 180.0f);
    const bool major = (i % 5 == 0);
    const int rOuter = CLK_R - 5;
    const int rInner = major ? (CLK_R - 12) : (CLK_R - 8);
    const int x1 = CLK_CX + (int)roundf(cosf(rad) * rInner);
    const int y1 = CLK_CY + (int)roundf(sinf(rad) * rInner);
    const int x2 = CLK_CX + (int)roundf(cosf(rad) * rOuter);
    const int y2 = CLK_CY + (int)roundf(sinf(rad) * rOuter);
    tft.drawLine(x1, y1, x2, y2, major ? COL_GOLD : COL_TICK);
  }

  // Angka utama dibuat kecil agar tetap proporsional pada TFT 1.8 inci.
  tft.setTextSize(1);
  tft.setTextColor(COL_GOLD);
  tft.setCursor(61, 34);  tft.print("12");
  tft.setCursor(104, 73); tft.print("3");
  tft.setCursor(62, 109); tft.print("6");
  tft.setCursor(18, 73);  tft.print("9");
}

void drawHand(float angleDeg, int length, uint16_t color, uint8_t thickness) {
  const float rad = (angleDeg - 90.0f) * (PI / 180.0f);
  const int x2 = CLK_CX + (int)roundf(cosf(rad) * length);
  const int y2 = CLK_CY + (int)roundf(sinf(rad) * length);
  const float perp = rad + (PI / 2.0f);
  const int ox = (int)roundf(cosf(perp));
  const int oy = (int)roundf(sinf(perp));

  // Garis bayangan halus memberi kedalaman tanpa membuat dial terlalu ramai.
  if (thickness > 1) {
    tft.drawLine(CLK_CX + 1, CLK_CY + 1, x2 + 1, y2 + 1, COL_BG);
  }
  for (int offset = -((int)thickness / 2); offset <= ((int)thickness / 2); ++offset) {
    tft.drawLine(CLK_CX + ox * offset, CLK_CY + oy * offset,
                 x2 + ox * offset, y2 + oy * offset, color);
  }
}

void renderClock(const struct tm& t) {
  // Redraw dial, bukan menghapus jarum dengan warna polos. Cara ini menjaga
  // indeks menit tetap utuh dan mencegah artefak/ghosting pada layar kecil.
  drawClockFace();

  const float secondAngle = t.tm_sec * 6.0f;
  const float minuteAngle = (t.tm_min + t.tm_sec / 60.0f) * 6.0f;
  const float hourAngle = ((t.tm_hour % 12) + t.tm_min / 60.0f + t.tm_sec / 3600.0f) * 30.0f;

  drawHand(hourAngle,  CLK_R - 23, COL_GOLD, 3);
  drawHand(minuteAngle, CLK_R - 14, COL_IVORY, 3);
  drawHand(secondAngle, CLK_R - 7, COL_RED, 1);

  // Hub bertingkat sebagai detail akhir pada pusat jam.
  tft.fillCircle(CLK_CX, CLK_CY, 4, COL_GOLD_DIM);
  tft.fillCircle(CLK_CX, CLK_CY, 2, COL_IVORY);
  tft.drawPixel(CLK_CX, CLK_CY, COL_RED);
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

  if (timeChanged || dateChanged) {
    tft.fillRect(0, 126, SCREEN_W, 34, COL_BG);
    tft.drawFastHLine(18, 127, SCREEN_W - 36, COL_GOLD_DIM);
  }

  if (timeChanged) {
    char timeStr[6];
    snprintf(timeStr, sizeof(timeStr), "%02d:%02d", t.tm_hour, t.tm_min);
    tft.setTextSize(2);
    tft.setTextColor(COL_IVORY);
    tft.setCursor(32, 131);
    tft.print(timeStr);

    tft.setTextSize(1);
    tft.setTextColor(COL_GOLD);
    tft.setCursor(94, 137);
    tft.print("WIB");
  }

  if (dateChanged) {
    char dateStr[24];
    snprintf(dateStr, sizeof(dateStr), "%s %02d %s %04d",
             dayNameShort(t.tm_wday), t.tm_mday,
             monthNameShort(t.tm_mon), t.tm_year + 1900);
    tft.setTextSize(1);
    tft.setTextColor(COL_MUTED);
    tft.setCursor(19, 151);
    tft.print(dateStr);
  }
}

void showSyncMessage() {
  tft.setTextSize(1);
  tft.setTextColor(COL_GOLD);
  tft.setCursor(37, 72);
  tft.print("SYNCING TIME");
}

void drawOtaScreen(int percent, const char* title, const char* subtitle, bool success = false) {
  tft.fillScreen(COL_BG);
  tft.setTextWrap(false);

  tft.setTextSize(1);
  tft.setTextColor(COL_GOLD);
  tft.setCursor(9, 10);
  tft.print("MERIDIAN  /  SERVICE");
  tft.drawFastHLine(8, 23, SCREEN_W - 16, COL_GOLD_DIM);

  tft.setTextSize(1);
  tft.setTextColor(success ? COL_GREEN : COL_IVORY);
  tft.setCursor(12, 43);
  tft.print(title);

  tft.setTextColor(COL_MUTED);
  tft.setCursor(12, 58);
  tft.print(subtitle);

  if (success) {
    tft.fillCircle(CLK_CX, 91, 13, COL_GREEN);
    tft.setTextColor(COL_BG);
    tft.setTextSize(2);
    tft.setCursor(59, 83);
    tft.print("OK");
  } else {
    tft.setTextSize(2);
    tft.setTextColor(COL_GOLD);
    tft.setCursor(percent >= 0 ? 44 : 49, 83);
    if (percent >= 0) {
      char pct[8];
      snprintf(pct, sizeof(pct), "%d%%", percent);
      tft.print(pct);
    } else {
      tft.print("RUN");
    }
  }

  tft.drawRoundRect(10, 111, 108, 10, 3, COL_GOLD_DIM);
  const int fillWidth = (percent < 0) ? 48 : constrain((104 * percent) / 100, 0, 104);
  if (fillWidth > 0) tft.fillRoundRect(12, 113, fillWidth, 6, 2, success ? COL_GREEN : COL_GOLD);

  tft.setTextSize(1);
  tft.setTextColor(COL_MUTED);
  tft.setCursor(18, 139);
  tft.print(success ? "PLEASE WAIT - RESTARTING" : "DO NOT POWER OFF DEVICE");
}

// ------------------------------------------------------------
// WI-FI, TIME AND OTA
// ------------------------------------------------------------
void connectWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.persistent(false);
  WiFi.begin(WIFI_SSID, WIFI_PASS);

  Serial.print("Connecting to Wi-Fi");
  const unsigned long startAttempt = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - startAttempt < 15000) {
    delay(250);
    Serial.print('.');
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("Wi-Fi connected. IP: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("Wi-Fi unavailable. Clock will wait for time synchronization.");
  }
}

void syncNTPTime() {
  if (WiFi.status() != WL_CONNECTED) return;

  configTime(GMT_OFFSET_SEC, DAYLIGHT_OFFSET_SEC,
             "pool.ntp.org", "time.google.com", "time.nist.gov");

  struct tm timeinfo;
  const unsigned long startSync = millis();
  while (!getLocalTime(&timeinfo, 250) && millis() - startSync < 8000) {
    delay(150);
  }

  if (getLocalTime(&timeinfo, 250) && timeinfo.tm_year >= (2024 - 1900)) {
    timeSynced = true;
    Serial.println("NTP time synchronized (WIB).");
  }
}

bool requireOtaAuth() {
  if (server.authenticate(OTA_USER, OTA_PASS)) return true;
  server.requestAuthentication();
  return false;
}

void setupWebOTA() {
  server.on("/", HTTP_GET, []() {
    if (!requireOtaAuth()) return;
    server.sendHeader("Cache-Control", "no-store");
    server.send(200, "text/html; charset=utf-8", otaPageHtml);
  });

  server.on("/update", HTTP_POST, []() {
    if (!requireOtaAuth()) return;

    server.sendHeader("Connection", "close");
    const bool ok = otaReady && !Update.hasError();
    server.send(200, "text/plain", ok ? "OK" : "FAIL: firmware update error");

    if (ok) {
      drawOtaScreen(100, "UPDATE COMPLETE", "Firmware installed successfully", true);
      delay(700);
      ESP.restart();
    } else {
      otaReady = false;
    }
  }, []() {
    if (!requireOtaAuth()) return;

    HTTPUpload& upload = server.upload();
    if (upload.status == UPLOAD_FILE_START) {
      otaReady = false;
      drawOtaScreen(-1, "PREPARING UPDATE", "Opening firmware partition...");
      Serial.printf("OTA start: %s\n", upload.filename.c_str());

      if (Update.begin(UPDATE_SIZE_UNKNOWN)) {
        otaReady = true;
      } else {
        Update.printError(Serial);
      }
    } else if (upload.status == UPLOAD_FILE_WRITE) {
      if (!otaReady) return;

      const size_t written = Update.write(upload.buf, upload.currentSize);
      if (written != upload.currentSize) {
        otaReady = false;
        Update.printError(Serial);
      } else {
        // Ukuran total multipart tidak selalu tersedia per chunk; gunakan
        // indikator berjalan, sedangkan persentase akurat ditampilkan di web.
        drawOtaScreen(-1, "INSTALLING FIRMWARE", "Writing new application...");
      }
    } else if (upload.status == UPLOAD_FILE_END) {
      if (otaReady && Update.end(true)) {
        Serial.printf("OTA success: %u bytes\n", upload.totalSize);
        drawOtaScreen(100, "VERIFYING FIRMWARE", "Final checks completed...");
      } else {
        otaReady = false;
        Update.printError(Serial);
        drawOtaScreen(0, "UPDATE FAILED", "Please retry with valid firmware");
      }
    } else if (upload.status == UPLOAD_FILE_ABORTED) {
      otaReady = false;
      Update.end();
      Serial.println("OTA upload aborted.");
      drawOtaScreen(0, "UPLOAD CANCELLED", "No firmware was installed");
    }
  });

  server.begin();
  Serial.println("OTA server started on port 80.");
}

// ------------------------------------------------------------
// ARDUINO ENTRY POINTS
// ------------------------------------------------------------
void setup() {
  Serial.begin(115200);

  tft.initR(INITR_BLACKTAB);
  tft.setRotation(0);
  tft.fillScreen(COL_BG);
  tft.setTextWrap(false);

  connectWiFi();
  setupWebOTA();
  syncNTPTime();

  drawHeader();
  drawClockFace();
  lastWifiConnected = (WiFi.status() == WL_CONNECTED);

  struct tm timeinfo;
  if (getLocalTime(&timeinfo, 250) && timeinfo.tm_year >= (2024 - 1900)) {
    renderClock(timeinfo);
    renderFooter(timeinfo, true);
    prevHour24 = timeinfo.tm_hour;
    prevMin = timeinfo.tm_min;
    prevSec = timeinfo.tm_sec;
    prevDay = timeinfo.tm_mday;
    prevMonth = timeinfo.tm_mon;
    prevYear = timeinfo.tm_year + 1900;
  } else {
    showSyncMessage();
  }
}

void loop() {
  server.handleClient();

  const bool wifiConnected = (WiFi.status() == WL_CONNECTED);
  if (wifiConnected != lastWifiConnected) {
    lastWifiConnected = wifiConnected;
    drawHeader();
  }

  const unsigned long now = millis();
  if (now - lastTickMs >= 1000) {
    lastTickMs = now;

    if (wifiConnected && !timeSynced) {
      syncNTPTime();
    }

    struct tm timeinfo;
    if (getLocalTime(&timeinfo, 100) && timeinfo.tm_year >= (2024 - 1900)) {
      if (timeinfo.tm_sec != prevSec || timeinfo.tm_min != prevMin ||
          timeinfo.tm_hour != prevHour24) {
        renderClock(timeinfo);
        renderFooter(timeinfo, false);

        prevHour24 = timeinfo.tm_hour;
        prevMin = timeinfo.tm_min;
        prevSec = timeinfo.tm_sec;
        prevDay = timeinfo.tm_mday;
        prevMonth = timeinfo.tm_mon;
        prevYear = timeinfo.tm_year + 1900;
      }
    } else if (!timeSynced) {
      // Pesan sinkronisasi hanya ditampilkan ketika waktu belum tersedia.
      showSyncMessage();
    }
  }

  delay(10);
}
