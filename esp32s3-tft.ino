#include <Adafruit_GFX.h>
#include <Adafruit_ST7735.h>
#include <SPI.h>
#include <WiFi.h>
#include <WebServer.h>
#include <Update.h>

const char* WIFI_SSID = "Qwerty1";
const char* WIFI_PASS = "zxcvbnm123";

#define TFT_CS    13
#define TFT_RST   9
#define TFT_DC    10
#define TFT_MOSI  11
#define TFT_SCLK  12

Adafruit_ST7735 tft = Adafruit_ST7735(TFT_CS, TFT_DC, TFT_MOSI, TFT_SCLK, TFT_RST);
WebServer server(80);

const int SCREEN_W = 128;
const int SCREEN_H = 160;
const int TOP_BAR_H = 26;

float ballX = 64.0f;
float ballY = 90.0f;
float vx = 2.0f;
float vy = 1.6f;
const int radius = 9;
int prevX = (int)ballX;
int prevY = (int)ballY;

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

void setup() {
  Serial.begin(115200);

  tft.initR(INITR_BLACKTAB);
  tft.setRotation(0);
  tft.fillScreen(ST77XX_BLACK);

  connectWiFi();
  if (WiFi.status() == WL_CONNECTED) {
    setupWebOTA();
  }

  tft.fillScreen(ST77XX_BLACK);
  tft.fillRect(0, 0, SCREEN_W, TOP_BAR_H, 0x18E3);
  tft.setTextColor(ST77XX_CYAN);
  tft.setTextSize(1);
  tft.setCursor(6, 5);
  tft.print("WEB OTA READY");
  tft.setCursor(6, 15);
  tft.setTextColor(ST77XX_WHITE);
  if (WiFi.status() == WL_CONNECTED) {
    tft.print(WiFi.localIP());
  } else {
    tft.print("No WiFi (Offline)");
  }
}

void loop() {
  if (WiFi.status() == WL_CONNECTED) {
    server.handleClient();
  }

  tft.fillCircle(prevX, prevY, radius + 1, ST77XX_BLACK);

  ballX += vx;
  ballY += vy;

  int minX = radius;
  int maxX = SCREEN_W - radius - 1;
  int minY = TOP_BAR_H + radius;
  int maxY = SCREEN_H - radius - 1;

  if (ballX <= minX) { ballX = minX; vx = -vx; }
  else if (ballX >= maxX) { ballX = maxX; vx = -vx; }

  if (ballY <= minY) { ballY = minY; vy = -vy; }
  else if (ballY >= maxY) { ballY = maxY; vy = -vy; }

  int curX = (int)ballX;
  int curY = (int)ballY;

  tft.fillCircle(curX, curY, radius, ST77XX_ORANGE);
  tft.fillCircle(curX - 3, curY - 3, 3, ST77XX_YELLOW);

  prevX = curX;
  prevY = curY;

  delay(16);
}
