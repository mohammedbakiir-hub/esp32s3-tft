#include <Adafruit_GFX.h>
#include <Adafruit_ST7735.h>
#include <SPI.h>
#include <WiFi.h>
#include <WebServer.h>
#include <Update.h>

const char* WIFI_SSID = "Qwerty1";
const char* WIFI_PASS = "zxcvbnm123";

// Zona waktu Indonesia: WIB = UTC+7 (7 * 3600 detik)
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
const int TOP_BAR_H = 22;

const uint16_t COL_BG      = 0x0843; // Biru gelap lembut
const uint16_t COL_CARD    = 0x10A5; // Panel jam gelap
const uint16_t COL_ACCENT  = 0x07FF; // Cyan lembut
const uint16_t COL_TIME    = ST77XX_WHITE;
const uint16_t COL_SEC     = 0xFFE0; // Kuning lembut untuk detik
const uint16_t COL_MUTED   = 0xBDF7; // Abu terang

int lastSec = -1;
int lastMin = -1;
int lastHour = -1;
int lastDay = -1;
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

const char* namaHari(int wday) {
  switch (wday) {
    case 0: return "MINGGU";
    case 1: return "SENIN";
    case 2: return "SELASA";
    case 3: return "RABU";
    case 4: return "KAMIS";
    case 5: return "JUMAT";
    case 6: return "SABTU";
    default: return "---";
  }
}

const char* namaBulan(int mon) {
  switch (mon) {
    case 0:  return "JAN";
    case 1:  return "FEB";
    case 2:  return "MAR";
    case 3:  return "APR";
    case 4:  return "MEI";
    case 5:  return "JUN";
    case 6:  return "JUL";
    case 7:  return "AGU";
    case 8:  return "SEP";
    case 9:  return "OKT";
    case 10: return "NOV";
    case 11: return "DES";
    default: return "---";
  }
}

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

void drawStaticClockLayout() {
  tft.fillScreen(COL_BG);

  // Header status IP & Web OTA
  tft.fillRect(0, 0, SCREEN_W, TOP_BAR_H, 0x18E3);
  tft.setTextSize(1);
  tft.setCursor(6, 7);
  if (WiFi.status() == WL_CONNECTED) {
    tft.setTextColor(ST77XX_CYAN);
    tft.print(WiFi.localIP());
  } else {
    tft.setTextColor(ST77XX_YELLOW);
    tft.print("Offline / No WiFi");
  }

  // Bingkai utama jam dinding
  tft.fillRoundRect(8, 32, SCREEN_W - 16, 72, 10, COL_CARD);
  tft.drawRoundRect(8, 32, SCREEN_W - 16, 72, 10, COL_ACCENT);

  // Label zona waktu di bagian bawah
  tft.setTextSize(1);
  tft.setTextColor(COL_ACCENT);
  tft.setCursor(10, 144);
  tft.print("JAM DIGITAL WIB");
}

void updateClockDisplay(const struct tm& timeinfo) {
  char hmBuf[6];
  char secBuf[5];
  char dateBuf[24];

  snprintf(hmBuf, sizeof(hmBuf), "%02d:%02d", timeinfo.tm_hour, timeinfo.tm_min);
  snprintf(secBuf, sizeof(secBuf), ":%02d", timeinfo.tm_sec);
  snprintf(dateBuf, sizeof(dateBuf), "%s, %02d %s %04d",
           namaHari(timeinfo.tm_wday),
           timeinfo.tm_mday,
           namaBulan(timeinfo.tm_mon),
           timeinfo.tm_year + 1900);

  // Update HH:MM hanya saat jam atau menit berubah
  if (timeinfo.tm_hour != lastHour || timeinfo.tm_min != lastMin) {
    tft.fillRect(14, 46, 82, 26, COL_CARD);
    tft.setTextSize(3);
    tft.setTextColor(COL_TIME);
    tft.setCursor(15, 48);
    tft.print(hmBuf);
    lastHour = timeinfo.tm_hour;
    lastMin = timeinfo.tm_min;
  }

  // Update :SS tiap detik
  if (timeinfo.tm_sec != lastSec) {
    tft.fillRect(94, 54, 22, 16, COL_CARD);
    tft.setTextSize(1);
    tft.setTextColor(COL_SEC);
    tft.setCursor(96, 59);
    tft.print(secBuf);
    lastSec = timeinfo.tm_sec;
  }

  // Update tanggal & hari bila berganti hari
  if (timeinfo.tm_mday != lastDay) {
    tft.fillRect(8, 114, SCREEN_W - 16, 16, COL_BG);
    tft.setTextSize(1);
    tft.setTextColor(COL_MUTED);
    tft.setCursor(10, 118);
    tft.print(dateBuf);
    lastDay = timeinfo.tm_mday;
  }
}

void setup() {
  Serial.begin(115200);

  tft.initR(INITR_BLACKTAB);
  tft.setRotation(0);
  tft.fillScreen(ST77XX_BLACK);

  connectWiFi();
  if (WiFi.status() == WL_CONNECTED) {
    setupWebOTA();
    syncNTPTime();
  }

  drawStaticClockLayout();

  struct tm timeinfo;
  if (getLocalTime(&timeinfo)) {
    updateClockDisplay(timeinfo);
  } else {
    tft.setTextSize(2);
    tft.setTextColor(ST77XX_YELLOW);
    tft.setCursor(18, 56);
    tft.print("SYNC...");
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
      updateClockDisplay(timeinfo);
    }
  }

  // Jeda agar CPU tidak bekerja penuh terus-menerus (lebih adem)
  delay(100);
}
