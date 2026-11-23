#include <Adafruit_GFX.h>
#include <Adafruit_ST7735.h>
#include <SPI.h>
#include <WiFi.h>
#include <ArduinoOTA.h>

const char* WIFI_SSID = "Qwerty";
const char* WIFI_PASS = "qwerty123";

#define TFT_CS    13
#define TFT_RST   9
#define TFT_DC    10
#define TFT_MOSI  11
#define TFT_SCLK  12

Adafruit_ST7735 tft = Adafruit_ST7735(TFT_CS, TFT_DC, TFT_MOSI, TFT_SCLK, TFT_RST);

const int SCREEN_W = 128;
const int SCREEN_H = 160;
const int TOP_BAR_H = 24;

float ballX = 64.0f;
float ballY = 80.0f;
float vx = 2.2f;
float vy = 1.7f;
const int radius = 10;

int prevX = (int)ballX;
int prevY = (int)ballY;

void setupWiFiAndOTA() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);

  unsigned long startAttempt = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - startAttempt < 15000) {
    delay(250);
  }

  if (WiFi.status() == WL_CONNECTED) {
    ArduinoOTA.setHostname("esp32s3-tft");

    ArduinoOTA.onStart([]() {
      tft.fillScreen(ST77XX_BLACK);
      tft.setTextColor(ST77XX_YELLOW);
      tft.setTextSize(1);
      tft.setCursor(8, 20);
      tft.print("OTA UPDATE...");
    });

    ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
      int pct = (progress * 100) / total;
      tft.fillRect(8, 40, SCREEN_W - 16, 12, ST77XX_BLACK);
      tft.drawRect(8, 40, SCREEN_W - 16, 12, ST77XX_WHITE);
      tft.fillRect(10, 42, ((SCREEN_W - 20) * pct) / 100, 8, ST77XX_GREEN);
    });

    ArduinoOTA.onEnd([]() {
      tft.fillScreen(ST77XX_BLACK);
      tft.setTextColor(ST77XX_GREEN);
      tft.setTextSize(1);
      tft.setCursor(8, 20);
      tft.print("OTA DONE! REBOOT...");
    });

    ArduinoOTA.begin();
  }
}

void setup() {
  Serial.begin(115200);

  tft.initR(INITR_BLACKTAB);
  tft.setRotation(0);
  tft.fillScreen(ST77XX_BLACK);

  setupWiFiAndOTA();

  tft.fillScreen(ST77XX_BLACK);
  tft.fillRect(0, 0, SCREEN_W, TOP_BAR_H, 0x18E3);
  tft.setTextColor(ST77XX_CYAN);
  tft.setTextSize(1);
  tft.setCursor(8, 6);
  tft.print("ESP32-S3 OTA READY");
  tft.setCursor(8, 15);
  tft.setTextColor(ST77XX_WHITE);
  if (WiFi.status() == WL_CONNECTED) {
    tft.print(WiFi.localIP());
  } else {
    tft.print("No WiFi (Offline)");
  }
}

void loop() {
  if (WiFi.status() == WL_CONNECTED) {
    ArduinoOTA.handle();
  }

  tft.fillCircle(prevX, prevY, radius + 1, ST77XX_BLACK);

  ballX += vx;
  ballY += vy;

  int minX = radius;
  int maxX = SCREEN_W - radius - 1;
  int minY = TOP_BAR_H + radius;
  int maxY = SCREEN_H - radius - 1;

  if (ballX <= minX) {
    ballX = minX;
    vx = -vx;
  } else if (ballX >= maxX) {
    ballX = maxX;
    vx = -vx;
  }

  if (ballY <= minY) {
    ballY = minY;
    vy = -vy;
  } else if (ballY >= maxY) {
    ballY = maxY;
    vy = -vy;
  }

  int curX = (int)ballX;
  int curY = (int)ballY;

  tft.fillCircle(curX, curY, radius, ST77XX_ORANGE);
  tft.fillCircle(curX - 3, curY - 3, 3, ST77XX_YELLOW);

  prevX = curX;
  prevY = curY;

  delay(16);
}
