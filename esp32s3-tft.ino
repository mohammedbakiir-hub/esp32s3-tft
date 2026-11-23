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
const int TOP_BAR_H = 24;

// Warna kustom (RGB565) agar wajah robot terlihat menggemaskan
const uint16_t COL_BG       = 0x0843; // Biru gelap lembut
const uint16_t COL_FACE     = 0xF79E; // Putih gading hangat
const uint16_t COL_SHADE    = 0xCE59; // Bayangan abu hangat
const uint16_t COL_EYE_BG   = ST77XX_WHITE;
const uint16_t COL_PUPIL    = 0x10A2; // Biru tua pekat
const uint16_t COL_SPARKLE  = ST77XX_WHITE;
const uint16_t COL_BLUSH    = 0xF9B2; // Pink pipi menggemaskan
const uint16_t COL_MOUTH    = 0xA145; // Merah bata hangat
const uint16_t COL_ANTENNA  = 0xFD20; // Oranye keemasan

unsigned long animStartMs = 0;
unsigned long lastBlinkMs = 0;
bool isBlinking = false;
int lastLookX = 999;
int lastLookY = 999;
bool lastBlinkState = false;
bool faceDrawn = false;

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

void drawTopBar() {
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

void drawRobotStaticBase(int bobOffset) {
  // Bersihkan area animasi di bawah top bar
  tft.fillRect(0, TOP_BAR_H, SCREEN_W, SCREEN_H - TOP_BAR_H, COL_BG);

  int headX = 18;
  int headY = 44 + bobOffset;
  int headW = 92;
  int headH = 84;

  // Antena lucu di atas kepala
  int antX = SCREEN_W / 2;
  tft.drawLine(antX, headY, antX, headY - 10, COL_FACE);
  tft.drawLine(antX + 1, headY, antX + 1, headY - 10, COL_FACE);
  tft.fillCircle(antX, headY - 13, 5, COL_ANTENNA);
  tft.fillCircle(antX - 1, headY - 14, 2, ST77XX_WHITE);

  // Telinga kecil kiri & kanan
  tft.fillRoundRect(headX - 6, headY + 28, 7, 22, 3, COL_SHADE);
  tft.fillRoundRect(headX + headW - 1, headY + 28, 7, 22, 3, COL_SHADE);

  // Bayangan & badan kepala robot membulat
  tft.fillRoundRect(headX + 2, headY + 3, headW, headH, 18, COL_SHADE);
  tft.fillRoundRect(headX, headY, headW, headH, 18, COL_FACE);
  tft.drawRoundRect(headX, headY, headW, headH, 18, COL_PUPIL);

  // Pipi merona (blush) kiri & kanan
  int blushY = headY + 50;
  tft.fillCircle(headX + 16, blushY, 7, COL_BLUSH);
  tft.fillCircle(headX + headW - 16, blushY, 7, COL_BLUSH);

  // Senyum lucu & menggemaskan (lengkungan bawah dengan ujung melengkung ke atas)
  int mouthCenterX = SCREEN_W / 2;
  int mouthY = headY + 56;
  for (int dx = -16; dx <= 16; dx++) {
    int dy = (dx * dx) / 48; // lengkung U lembut
    tft.fillCircle(mouthCenterX + dx, mouthY + dy, 2, COL_MOUTH);
  }
  // Ujung senyum naik sedikit biar makin manis
  tft.drawLine(mouthCenterX - 17, mouthY + 5, mouthCenterX - 20, mouthY + 1, COL_MOUTH);
  tft.drawLine(mouthCenterX + 17, mouthY + 5, mouthCenterX + 20, mouthY + 1, COL_MOUTH);
}

void drawRobotEyes(int bobOffset, int lookX, int lookY, bool blinking) {
  int headY = 44 + bobOffset;
  int leftEyeX = 44;
  int rightEyeX = 84;
  int eyeY = headY + 32;

  // Bersihkan area mata saja agar tidak berkedip berlebih
  tft.fillRoundRect(leftEyeX - 13, eyeY - 13, 26, 26, 8, COL_FACE);
  tft.fillRoundRect(rightEyeX - 13, eyeY - 13, 26, 26, 8, COL_FACE);

  if (blinking) {
    // Mata tertutup ramah (^ ^)
    tft.drawLine(leftEyeX - 8, eyeY + 1, leftEyeX, eyeY - 4, COL_PUPIL);
    tft.drawLine(leftEyeX, eyeY - 4, leftEyeX + 8, eyeY + 1, COL_PUPIL);
    tft.drawLine(leftEyeX - 8, eyeY + 2, leftEyeX, eyeY - 3, COL_PUPIL);
    tft.drawLine(leftEyeX, eyeY - 3, leftEyeX + 8, eyeY + 2, COL_PUPIL);

    tft.drawLine(rightEyeX - 8, eyeY + 1, rightEyeX, eyeY - 4, COL_PUPIL);
    tft.drawLine(rightEyeX, eyeY - 4, rightEyeX + 8, eyeY + 1, COL_PUPIL);
    tft.drawLine(rightEyeX - 8, eyeY + 2, rightEyeX, eyeY - 3, COL_PUPIL);
    tft.drawLine(rightEyeX, eyeY - 3, rightEyeX + 8, eyeY + 2, COL_PUPIL);
    return;
  }

  // Bola mata putih besar
  tft.fillCircle(leftEyeX, eyeY, 11, COL_EYE_BG);
  tft.drawCircle(leftEyeX, eyeY, 11, COL_PUPIL);
  tft.fillCircle(rightEyeX, eyeY, 11, COL_EYE_BG);
  tft.drawCircle(rightEyeX, eyeY, 11, COL_PUPIL);

  // Pupil melirik halus
  int pxL = leftEyeX + lookX;
  int pyL = eyeY + lookY;
  int pxR = rightEyeX + lookX;
  int pyR = eyeY + lookY;

  tft.fillCircle(pxL, pyL, 6, COL_PUPIL);
  tft.fillCircle(pxR, pyR, 6, COL_PUPIL);

  // Kilau mata kecil (sparkle) agar terlihat hidup & lucu
  tft.fillCircle(pxL - 2, pyL - 2, 2, COL_SPARKLE);
  tft.fillCircle(pxR - 2, pyR - 2, 2, COL_SPARKLE);
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

  tft.fillScreen(COL_BG);
  drawTopBar();

  animStartMs = millis();
  lastBlinkMs = millis();
  drawRobotStaticBase(0);
  drawRobotEyes(0, 0, 0, false);
  faceDrawn = true;
}

void loop() {
  if (WiFi.status() == WL_CONNECTED) {
    server.handleClient();
  }

  unsigned long now = millis();

  // Atur jadwal kedipan mata alami
  if (!isBlinking && (now - lastBlinkMs > 2800)) {
    isBlinking = true;
    lastBlinkMs = now;
  } else if (isBlinking && (now - lastBlinkMs > 160)) {
    isBlinking = false;
    lastBlinkMs = now;
  }

  // Gerakan mengangguk/mengayun pelan dan arah pandangan mata
  float t = (now - animStartMs) / 1000.0f;
  int bobOffset = (int)round(sinf(t * 2.6f) * 3.0f);
  int lookX = (int)round(sinf(t * 1.4f) * 3.0f);
  int lookY = (int)round(cosf(t * 1.9f) * 2.0f);

  static int lastBobOffset = 999;

  if (!faceDrawn || bobOffset != lastBobOffset) {
    drawRobotStaticBase(bobOffset);
    drawRobotEyes(bobOffset, lookX, lookY, isBlinking);
    lastBobOffset = bobOffset;
    lastLookX = lookX;
    lastLookY = lookY;
    lastBlinkState = isBlinking;
    faceDrawn = true;
  } else if (lookX != lastLookX || lookY != lastLookY || isBlinking != lastBlinkState) {
    drawRobotEyes(bobOffset, lookX, lookY, isBlinking);
    lastLookX = lookX;
    lastLookY = lookY;
    lastBlinkState = isBlinking;
  }

  delay(25);
}
