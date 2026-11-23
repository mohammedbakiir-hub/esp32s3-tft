#include <Adafruit_GFX.h>
#include <Adafruit_ST7735.h>
#include <SPI.h>
#include <WiFi.h>
#include <WebServer.h>
#include <Update.h>

const char* WIFI_SSID = "Qwerty1";
const char* WIFI_PASS = "zxcvbnm123";#include <Adafruit_GFX.h>

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

// Layar 128x160
const int SCREEN_W = 128;
const int SCREEN_H = 160;

// Geometri jam dinding minimalis (tengah layar)
const int CLK_CX = 64;
const int CLK_CY = 82;
const int CLK_R  = 46;

// Palet minimalis bersih
const uint16_t COL_BG      = ST77XX_BLACK;
const uint16_t COL_FACE    = 0x1082; // Abu-biru sangat gelap
const uint16_t COL_RING    = 0xBDF7; // Lingkaran jam minimalis
const uint16_t COL_TICK    = 0x7BEF; // Tanda jam/menit
const uint16_t COL_HOUR_H  = ST77XX_WHITE;
const uint16_t COL_MIN_H   = 0x07FF; // Cyan lembut
const uint16_t COL_SEC_H   = 0xF800; // Merah untuk jarum detik muter
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
<html>
<head>
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>ESP32-S3 Web OTA</title>
  <style>
    body { font-family: sans-serif; background: #111; color: #eee; padding: 20px; }
    .box { max-width: 420px; margin: auto; background: #1d1d1d; padding: 20px; border-radius: 10px; }
    input[type=file] { width: 100%; margin: 14px 0; }
    button { width: 100%; padding: 12px; background: #00b894; color: #000; font-weight: bold; border: none; border-radius: 6px; }
  </style>
</head>
<body>
  <div class="box">
    <h2>ESP32-S3 Web OTA</h2>
    <p>Pilih file <b>.ino.bin</b> hasil build GitHub Actions:</p>
    <form method="POST" action="/update" enctype="multipart/form-data">
      <input type="file" name="update" accept=".bin" required>
      <button type="submit">Upload Firmware</button>
    </form>
  </div>
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
    // Geser 1 px tegak lurus agar jarum jam & menit tegas
    float perp = rad + (PI / 2.0f);
    int ox = (int)round(cosf(perp));
    int oy = (int)round(sinf(perp));
    tft.drawLine(CLK_CX + ox, CLK_CY + oy, x2 + ox, y2 + oy, color);
  }
}

void drawClockDial() {
  tft.fillScreen(COL_BG);

  // Header IP minimalis di bagian paling atas
  tft.setTextSize(1);
  tft.setTextColor(COL_MUTED);
  tft.setCursor(6, 4);
  if (WiFi.status() == WL_CONNECTED) {
    tft.print(WiFi.localIP());
  } else {
    tft.print("OFFLINE");
  }

  // Badan jam dinding bulat
  tft.fillCircle(CLK_CX, CLK_CY, CLK_R, COL_FACE);
  tft.drawCircle(CLK_CX, CLK_CY, CLK_R, COL_RING);
  tft.drawCircle(CLK_CX, CLK_CY, CLK_R - 1, COL_RING);

  // 12 tanda jam (12, 3, 6, 9 lebih panjang)
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

  // Hapus jarum detik lama
  if (prevSec >= 0 && (forceRedraw || second != prevSec)) {
    drawHand(prevSec * 6.0f, CLK_R - 7, COL_FACE, false);
  }

  // Jika menit atau jam berubah, hapus & gambar ulang jarum menit & jam
  if (forceRedraw || minute != prevMin || hour12 != prevHour) {
    if (prevMin >= 0) {
      float pMinAng = (prevMin + (prevSec >= 0 ? prevSec : 0) / 60.0f) * 6.0f;
      drawHand(pMinAng, CLK_R - 12, COL_FACE, true);
    }
    if (prevHour >= 0) {
      float pHrAng = (prevHour + (prevMin >= 0 ? prevMin : 0) / 60.0f) * 30.0f;
      drawHand(pHrAng, CLK_R - 22, COL_FACE, true);
    }

    // Gambar ulang tanda jam agar tidak terpotong bekas jarum
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

  // Gambar jarum jam, menit, lalu detik yang muter tiap detik
  drawHand(hourAngle, CLK_R - 22, COL_HOUR_H, true);
  drawHand(minAngle,  CLK_R - 12, COL_MIN_H,  true);
  drawHand(secAngle,  CLK_R - 7,  COL_SEC_H,  false);

  // Poros tengah di atas semua jarum
  tft.fillCircle(CLK_CX, CLK_CY, 3, COL_TEXT);
  tft.fillCircle(CLK_CX, CLK_CY, 1, COL_SEC_H);
}

void renderFooter(const struct tm& t) {
  if (t.tm_mday != prevDay || t.tm_min != prevMin || t.tm_sec != prevSec) {
    tft.fillRect(0, 132, SCREEN_W, 28, COL_BG);

    char timeStr[16];
    snprintf(timeStr, sizeof(timeStr), "%02d:%02d:%02d WIB", t.tm_hour, t.tm_min, t.tm_sec);
    tft.setTextSize(1);
    tft.setTextColor(COL_TEXT);
    tft.setCursor(22, 135);
    tft.print(timeStr);

    char dateStr[20];
    snprintf(dateStr, sizeof(dateStr), "%02d/%02d/%04d", t.tm_mday, t.tm_mon + 1, t.tm_year + 1900);
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
    renderFooter(timeinfo);
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
        renderFooter(timeinfo);
        prevHour = h12;
        prevMin = timeinfo.tm_min;
        prevSec = timeinfo.tm_sec;
        prevDay = timeinfo.tm_mday;
      }
    }
  }

  delay(100);
}
