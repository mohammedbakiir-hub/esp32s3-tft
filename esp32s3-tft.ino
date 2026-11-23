#include <Adafruit_GFX.h>
#include <Adafruit_ST7735.h>
#include <SPI.h>

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

void setup() {
  Serial.begin(115200);

  tft.initR(INITR_BLACKTAB);
  tft.setRotation(0);
  tft.fillScreen(ST77XX_BLACK);

  tft.fillRect(0, 0, SCREEN_W, TOP_BAR_H, 0x18E3);
  tft.setTextColor(ST77XX_CYAN);
  tft.setTextSize(1);
  tft.setCursor(8, 8);
  tft.print("ESP32-S3 TFT TEST");
}

void loop() {
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
