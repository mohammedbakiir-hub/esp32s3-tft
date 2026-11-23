#include <Adafruit_GFX.h>
#include <Adafruit_ST7735.h>
#include <SPI.h>
#include <WiFi.h>
#include <WebServer.h>
#include <Update.h>

const char* WIFI_SSID = "Qwerty1";
const char* WIFI_PASS = "zxcvbnm123";

// Zona waktu Indonesia: WIB = UTC+7
const long GMT_OFFSET_SEC = 7 * 3600;
const int  DAYLIGHT_OFFSET_SEC = 0;

#define TFT_CS    13
#define TFT_RST   9
#define TFT_DC    10
#define TFT_MOSI  11
#define TFT_SCLK  12

Adafruit_ST7735 tft = Adafruit_ST7735(TFT_CS, TFT_DC, TFT_MOSI, TFT_SCLK, TFT_RST);
WebServer server(80);

const int SCREEN_W = 128;
const int SCREEN_H = 160;

const int CLK_CX = 64;
const int CLK_CY = 82;
const int CLK_R  = 46;

const uint16_t COL_BG      = ST77XX_BLACK;
const uint16_t COL_FACE    = 0x1082;
const uint16_t COL_RING    = 0xBDF7;
const uint16_t COL_TICK    = 0x7BEF;
const uint16_t COL_HOUR_H  = ST77XX_WHITE;
const uint16_t COL_MIN_H   = 0x07FF;
const uint16_t COL_SEC_H   = 0xF800;
const uint16_t COL_TEXT    = ST77XX_WHITE;
const uint16_t COL_MUTED   = 0x8410;

int prevSec = -1;
int prevMin = -1;
int prevHour = -1;
int prevDay = -1;
unsigned long lastTickMs = 0;
bool timeSynced = false;

const char* otaPageHtml = R"rawliteral(
<!DOCTYPE html>
<html lang="id">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>ESP32-S3 OTA Studio</title>
  <style>
    :root {
      --bg: #0b0f17;
      --card: #121927;
      --card-2: #182234;
      --border: rgba(148, 163, 184, 0.16);
      --text: #f1f5f9;
      --muted: #94a3b8;
      --accent: #38bdf8;
      --accent-2: #2dd4bf;
      --ok: #22c55e;
      --err: #f87171;
    }
    * { box-sizing: border-box; -webkit-tap-highlight-color: transparent; }
    body {
      margin: 0;
      min-height: 100vh;
      font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, sans-serif;
      background:
        radial-gradient(circle at 20% 0%, rgba(56, 189, 248, 0.14), transparent 45%),
        radial-gradient(circle at 85% 15%, rgba(45, 212, 191, 0.12), transparent 45%),
        var(--bg);
      color: var(--text);
      display: flex;
      align-items: center;
      justify-content: center;
      padding: 24px 16px;
    }
    .panel {
      width: 100%;
      max-width: 460px;
      background: linear-gradient(180deg, rgba(24, 34, 52, 0.92), rgba(15, 23, 42, 0.96));
      border: 1px solid var(--border);
      border-radius: 20px;
      padding: 24px;
      box-shadow: 0 24px 60px rgba(0, 0, 0, 0.55);
      backdrop-filter: blur(12px);
    }
    .badge {
      display: inline-flex;
      align-items: center;
      gap: 8px;
      font-size: 12px;
      font-weight: 600;
      color: var(--accent-2);
      background: rgba(45, 212, 191, 0.1);
      border: 1px solid rgba(45, 212, 191, 0.25);
      padding: 6px 12px;
      border-radius: 999px;
      margin-bottom: 14px;
    }
    .dot {
      width: 8px;
      height: 8px;
      border-radius: 50%;
      background: var(--accent-2);
      box-shadow: 0 0 10px var(--accent-2);
    }
    h1 {
      margin: 0 0 6px;
      font-size: 22px;
      letter-spacing: -0.02em;
    }
    .sub {
      margin: 0 0 20px;
      font-size: 14px;
      line-height: 1.5;
      color: var(--muted);
    }
    .dropzone {
      display: flex;
      flex-direction: column;
      align-items: center;
      justify-content: center;
      text-align: center;
      min-height: 148px;
      padding: 20px;
      border-radius: 16px;
      border: 1.5px dashed rgba(56, 189, 248, 0.42);
      background: rgba(11, 15, 23, 0.55);
      cursor: pointer;
      transition: transform 0.15s ease, border-color 0.2s ease, background 0.2s ease;
    }
    .dropzone:active {
      transform: scale(0.99);
      border-color: var(--accent-2);
      background: rgba(56, 189, 248, 0.08);
    }
    .drop-icon {
      width: 44px;
      height: 44px;
      border-radius: 12px;
      display: grid;
      place-items: center;
      background: rgba(56, 189, 248, 0.14);
      color: var(--accent);
      font-size: 22px;
      margin-bottom: 10px;
    }
    .drop-title {
      font-size: 15px;
      font-weight: 600;
      color: var(--text);
      margin-bottom: 4px;
    }
    .drop-hint {
      font-size: 12px;
      color: var(--muted);
    }
    .file-pill {
      display: none;
      margin-top: 12px;
      padding: 10px 14px;
      border-radius: 12px;
      background: var(--card-2);
      border: 1px solid var(--border);
      font-size: 13px;
      color: var(--text);
      align-items: center;
      justify-content: space-between;
      gap: 10px;
    }
    .file-pill.show { display: flex; }
    .file-meta { color: var(--muted); font-size: 12px; white-space: nowrap; }
    input[type="file"] { display: none; }
    .btn {
      margin-top: 18px;
      width: 100%;
      height: 50px;
      border: none;
      border-radius: 14px;
      font-size: 15px;
      font-weight: 700;
      letter-spacing: 0.01em;
      color: #061018;
      background: linear-gradient(135deg, var(--accent), var(--accent-2));
      cursor: pointer;
      box-shadow: 0 10px 24px rgba(56, 189, 248, 0.22);
      transition: opacity 0.2s ease, transform 0.12s ease;
    }
    .btn:active:not(:disabled) { transform: scale(0.985); }
    .btn:disabled { opacity: 0.45; cursor: not-allowed; box-shadow: none; }
    .progress-wrap {
      display: none;
      margin-top: 18px;
    }
    .progress-wrap.show { display: block; }
    .progress-top {
      display: flex;
      justify-content: space-between;
      font-size: 12px;
      color: var(--muted);
      margin-bottom: 8px;
    }
    .track {
      width: 100%;
      height: 10px;
      border-radius: 999px;
      background: rgba(148, 163, 184, 0.16);
      overflow: hidden;
    }
    .fill {
      width: 0%;
      height: 100%;
      border-radius: 999px;
      background: linear-gradient(90deg, var(--accent), var(--accent-2));
      transition: width 0.15s ease;
    }
    .status {
      margin-top: 14px;
      font-size: 13px;
      min-height: 18px;
      color: var(--muted);
    }
    .status.ok { color: var(--ok); font-weight: 600; }
    .status.err { color: var(--err); font-weight: 600; }
  </style>
</head>
<body>
  <div class="panel">
    <div class="badge"><span class="dot"></span>ESP32-S3 WIRELESS OTA</div>
    <h1>Firmware Updater</h1>
    <p class="sub">Pilih file <b>esp32s3-tft.ino.bin</b> hasil build GitHub Actions untuk update tanpa kabel.</p>

    <form id="otaForm">
      <label class="dropzone" for="fileInput">
        <div class="drop-icon">+</div>
        <div class="drop-title">Tap untuk pilih file .ino.bin</div>
        <div class="drop-hint">Ambil dari folder hasil ekstrak di Files iPad</div>
      </label>
      <input type="file" id="fileInput" name="update" accept=".bin" required>

      <div class="file-pill" id="filePill">
        <span id="fileName">-</span>
        <span class="file-meta" id="fileSize">0 KB</span>
      </div>

      <button class="btn" type="submit" id="uploadBtn" disabled>Upload Firmware</button>
    </form>

    <div class="progress-wrap" id="progressWrap">
      <div class="progress-top">
        <span id="progressLabel">Mengirim firmware...</span>
        <span id="progressPct">0%</span>
      </div>
      <div class="track"><div class="fill" id="progressFill"></div></div>
    </div>

    <div class="status" id="statusMsg"></div>
  </div>

  <script>
    const input = document.getElementById("fileInput");
    const pill = document.getElementById("filePill");
    const fileName = document.getElementById("fileName");
    const fileSize = document.getElementById("fileSize");
    const uploadBtn = document.getElementById("uploadBtn");
    const form = document.getElementById("otaForm");
    const progressWrap = document.getElementById("progressWrap");
    const progressFill = document.getElementById("progressFill");
    const progressPct = document.getElementById("progressPct");
    const progressLabel = document.getElementById("progressLabel");
    const statusMsg = document.getElementById("statusMsg");

    input.addEventListener("change", () => {
      const f = input.files[0];
      if (!f) {
        pill.classList.remove("show");
        uploadBtn.disabled = true;
        return;
      }
      fileName.textContent = f.name;
      fileSize.textContent = (f.size / 1024).toFixed(1) + " KB";
      pill.classList.add("show");
      uploadBtn.disabled = false;
      statusMsg.className = "status";
      statusMsg.textContent = "";
    });

    form.addEventListener("submit", (e) => {
      e.preventDefault();
      const f = input.files[0];
      if (!f) return;

      const data = new FormData();
      data.append("update", f, f.name);

      const xhr = new XMLHttpRequest();
      uploadBtn.disabled = true;
      progressWrap.classList.add("show");
      progressFill.style.width = "0%";
      progressPct.textContent = "0%";
      progressLabel.textContent = "Mengirim firmware...";
      statusMsg.className = "status";
      statusMsg.textContent = "Jangan tutup halaman ini selama proses upload.";

      xhr.upload.addEventListener("progress", (ev) => {
        if (ev.lengthComputable) {
          const pct = Math.round((ev.loaded / ev.total) * 100);
          progressFill.style.width = pct + "%";
          progressPct.textContent = pct + "%";
          if (pct >= 100) {
            progressLabel.textContent = "Memasang ke memori ESP32-S3...";
          }
        }
      });

      xhr.addEventListener("load", () => {
        if (xhr.status === 200 && xhr.responseText.indexOf("OK") !== -1) {
          progressFill.style.width = "100%";
          progressPct.textContent = "100%";
          progressLabel.textContent = "Selesai";
          statusMsg.className = "status ok";
          statusMsg.textContent = "Upload berhasil! ESP32-S3 sedang reboot otomatis...";
        } else {
          statusMsg.className = "status err";
          statusMsg.textContent = "Upload gagal: " + (xhr.responseText || "respon tidak valid");
          uploadBtn.disabled = false;
        }
      });

      xhr.addEventListener("error", () => {
        statusMsg.className = "status err";
        statusMsg.textContent = "Koneksi terputus saat upload. Cek Wi-Fi & coba lagi.";
        uploadBtn.disabled = false;
      });

      xhr.open("POST", "/update", true);
      xhr.send(data);
    });
  </script>
</body>
</html>
)rawliteral";

void connectWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);

  unsigned long startAttempt = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - startAttempt < 15000) {
    delay(250);
  }
}

void syncNTPTime() {
  if (WiFi.status() != WL_CONNECTED) return;

  configTime(GMT_OFFSET_SEC, DAYLIGHT_OFFSET_SEC, "pool.ntp.org", "time.google.com");

  struct tm timeinfo;
  unsigned long startSync = millis();
  while (!getLocalTime(&timeinfo) && millis() - startSync < 8000) {
    delay(200);
  }
  if (getLocalTime(&timeinfo) && timeinfo.tm_year >= (2024 - 1900)) {
    timeSynced = true;
  }
}

void setupWebOTA() {
  server.on("/", HTTP_GET, []() {
    server.send(200, "text/html", otaPageHtml);
  });

  server.on("/update", HTTP_POST, []() {
    server.sendHeader("Connection", "close");
    bool ok = !Update.hasError();
    server.send(200, "text/plain", ok ? "OK! Rebooting..." : "FAIL!");
    delay(400);
    ESP.restart();
  }, []() {
    HTTPUpload& upload = server.upload();
    if (upload.status == UPLOAD_FILE_START) {
      tft.fillScreen(ST77XX_BLACK);
      tft.setTextColor(ST77XX_YELLOW);
      tft.setTextSize(1);
      tft.setCursor(6, 20);
      tft.print("OTA UPLOADING...");
      if (!Update.begin(UPDATE_SIZE_UNKNOWN)) {
        Update.printError(Serial);
      }
    } else if (upload.status == UPLOAD_FILE_WRITE) {
      if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
        Update.printError(Serial);
      }
      int pct = (Update.progress() * 100) / (Update.size() > 0 ? Update.size() : 1);
      if (pct > 100) pct = 100;
      tft.fillRect(6, 42, SCREEN_W - 12, 12, ST77XX_BLACK);
      tft.drawRect(6, 42, SCREEN_W - 12, 12, ST77XX_WHITE);
      tft.fillRect(8, 44, ((SCREEN_W - 16) * pct) / 100, 8, ST77XX_GREEN);
    } else if (upload.status == UPLOAD_FILE_END) {
      if (Update.end(true)) {
        tft.fillScreen(ST77XX_BLACK);
        tft.setTextColor(ST77XX_GREEN);
        tft.setTextSize(1);
        tft.setCursor(6, 20);
        tft.print("OTA DONE! REBOOT...");
      } else {
        Update.printError(Serial);
      }
    }
  });

  server.begin();
}

void drawHand(float angleDeg, int length, uint16_t color, bool thick) {
  float rad = (angleDeg - 90.0f) * (PI / 180.0f);
  int x2 = CLK_CX + (int)round(cosf(rad) * length);
  int y2 = CLK_CY + (int)round(sinf(rad) * length);

  tft.drawLine(CLK_CX, CLK_CY, x2, y2, color);
  if (thick) {
    float perp = rad + (PI / 2.0f);
    int ox = (int)round(cosf(perp));
    int oy = (int)round(sinf(perp));
    tft.drawLine(CLK_CX + ox, CLK_CY + oy, x2 + ox, y2 + oy, color);
  }
}

void drawClockDial() {
  tft.fillScreen(COL_BG);

  tft.setTextSize(1);
  tft.setTextColor(COL_MUTED);
  tft.setCursor(6, 4);
  if (WiFi.status() == WL_CONNECTED) {
    tft.print(WiFi.localIP());
  } else {
    tft.print("OFFLINE");
  }

  tft.fillCircle(CLK_CX, CLK_CY, CLK_R, COL_FACE);
  tft.drawCircle(CLK_CX, CLK_CY, CLK_R, COL_RING);
  tft.drawCircle(CLK_CX, CLK_CY, CLK_R - 1, COL_RING);

  for (int h = 0; h < 12; h++) {
    float rad = (h * 30.0f - 90.0f) * (PI / 180.0f);
    int outerR = CLK_R - 3;
    int innerR = (h % 3 == 0) ? (CLK_R - 10) : (CLK_R - 7);

    int x1 = CLK_CX + (int)round(cosf(rad) * innerR);
    int y1 = CLK_CY + (int)round(sinf(rad) * innerR);
    int x2 = CLK_CX + (int)round(cosf(rad) * outerR);
    int y2 = CLK_CY + (int)round(sinf(rad) * outerR);

    tft.drawLine(x1, y1, x2, y2, (h % 3 == 0) ? COL_TEXT : COL_TICK);
  }

  tft.fillCircle(CLK_CX, CLK_CY, 3, COL_TEXT);
}

void renderHands(int hour12, int minute, int second, bool forceRedraw) {
  float secAngle  = second * 6.0f;
  float minAngle  = (minute + second / 60.0f) * 6.0f;
  float hourAngle = (hour12 + minute / 60.0f) * 30.0f;

  if (prevSec >= 0 && (forceRedraw || second != prevSec)) {
    drawHand(prevSec * 6.0f, CLK_R - 7, COL_FACE, false);
  }

  if (forceRedraw || minute != prevMin || hour12 != prevHour) {
    if (prevMin >= 0) {
      float pMinAng = (prevMin + (prevSec >= 0 ? prevSec : 0) / 60.0f) * 6.0f;
      drawHand(pMinAng, CLK_R - 12, COL_FACE, true);
    }
    if (prevHour >= 0) {
      float pHrAng = (prevHour + (prevMin >= 0 ? prevMin : 0) / 60.0f) * 30.0f;
      drawHand(pHrAng, CLK_R - 22, COL_FACE, true);
    }

    for (int h = 0; h < 12; h++) {
      float rad = (h * 30.0f - 90.0f) * (PI / 180.0f);
      int outerR = CLK_R - 3;
      int innerR = (h % 3 == 0) ? (CLK_R - 10) : (CLK_R - 7);
      tft.drawLine(
        CLK_CX + (int)round(cosf(rad) * innerR),
        CLK_CY + (int)round(sinf(rad) * innerR),
        CLK_CX + (int)round(cosf(rad) * outerR),
        CLK_CY + (int)round(sinf(rad) * outerR),
        (h % 3 == 0) ? COL_TEXT : COL_TICK
      );
    }
  }

  drawHand(hourAngle, CLK_R - 22, COL_HOUR_H, true);
  drawHand(minAngle,  CLK_R - 12, COL_MIN_H,  true);
  drawHand(secAngle,  CLK_R - 7,  COL_SEC_H,  false);

  tft.fillCircle(CLK_CX, CLK_CY, 3, COL_TEXT);
  tft.fillCircle(CLK_CX, CLK_CY, 1, COL_SEC_H);
}

void renderFooter(const struct tm& t, bool forceRedraw) {
  // Update teks jam (HH:MM WIB) hanya saat jam atau menit berubah (tanpa detik)
  if (forceRedraw || t.tm_hour != prevHour || t.tm_min != prevMin) {
    tft.fillRect(20, 134, 88, 10, COL_BG);
    char timeStr[12];
    snprintf(timeStr, sizeof(timeStr), "%02d:%02d WIB", t.tm_hour, t.tm_min);
    tft.setTextSize(1);
    tft.setTextColor(COL_TEXT);
    tft.setCursor(34, 135);
    tft.print(timeStr);
  }

  // Update tanggal hanya saat hari berganti atau saat draw awal
  if (forceRedraw || t.tm_mday != prevDay) {
    tft.fillRect(20, 146, 88, 10, COL_BG);
    char dateStr[16];
    snprintf(dateStr, sizeof(dateStr), "%02d/%02d/%04d", t.tm_mday, t.tm_mon + 1, t.tm_year + 1900);
    tft.setTextSize(1);
    tft.setTextColor(COL_MUTED);
    tft.setCursor(34, 147);
    tft.print(dateStr);
  }
}

void setup() {
  Serial.begin(115200);

  tft.initR(INITR_BLACKTAB);
  tft.setRotation(0);

  connectWiFi();
  if (WiFi.status() == WL_CONNECTED) {
    setupWebOTA();
    syncNTPTime();
  }

  drawClockDial();

  struct tm timeinfo;
  if (getLocalTime(&timeinfo)) {
    int h12 = timeinfo.tm_hour % 12;
    renderHands(h12, timeinfo.tm_min, timeinfo.tm_sec, true);
    renderFooter(timeinfo, true);
    prevHour = h12;
    prevMin = timeinfo.tm_min;
    prevSec = timeinfo.tm_sec;
    prevDay = timeinfo.tm_mday;
  } else {
    tft.setTextSize(1);
    tft.setTextColor(ST77XX_YELLOW);
    tft.setCursor(34, 78);
    tft.print("SYNC WAKTU...");
  }
}

void loop() {
  if (WiFi.status() == WL_CONNECTED) {
    server.handleClient();
  }

  unsigned long now = millis();
  if (now - lastTickMs >= 1000) {
    lastTickMs = now;

    if (WiFi.status() == WL_CONNECTED && !timeSynced) {
      syncNTPTime();
    }

    struct tm timeinfo;
    if (getLocalTime(&timeinfo)) {
      int h12 = timeinfo.tm_hour % 12;
      if (timeinfo.tm_sec != prevSec || timeinfo.tm_min != prevMin || h12 != prevHour) {
        renderHands(h12, timeinfo.tm_min, timeinfo.tm_sec, false);
        renderFooter(timeinfo, false);
        prevHour = h12;
        prevMin = timeinfo.tm_min;
        prevSec = timeinfo.tm_sec;
        prevDay = timeinfo.tm_mday;
      }
    }
  }

  delay(100);
}
