#include <Arduino_GFX_Library.h>
#include <math.h>
#include <string.h>

#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <esp_arduino_version.h>

#define ESPNOW_CHANNEL 1

typedef struct __attribute__((packed)) {
  uint32_t seq;
  float pupilX;
  float pupilY;
  float openness;
} EyePacket;


// ESP32-C3 Super Mini + GC9A01 240x240
// Realistic blue eye: almond-shaped sclera, iris texture, eyelids and smooth motion.
// Uses a full-frame canvas to reduce visible flicker.

#define TFT_SCK   4
#define TFT_MOSI  6
#define TFT_CS    7
#define TFT_DC    2
#define TFT_RST   3

Arduino_DataBus *bus = new Arduino_ESP32SPI(
  TFT_DC, TFT_CS, TFT_SCK, TFT_MOSI, GFX_NOT_DEFINED, FSPI
);
Arduino_GC9A01 *panel = new Arduino_GC9A01(bus, TFT_RST, 0, true);
Arduino_Canvas *gfx = new Arduino_Canvas(240, 240, panel);

#define BLACK       0x0000
#define WHITE       0xFFFF
#define OFFWHITE    0xFFDF
#define SCLERA      0xF7DE
#define REDVEIN     0xE8A8
#define DARKRED     0xA8A4
#define SKIN        0xD3AA
#define SKINLIGHT   0xF5D2
#define SKINDARK    0x8A45
#define LASH        0x2104
#define IRISDARK    0x3000
#define IRISBLUE    0xFC00
#define BLUE        0xFA20
#define CYAN        0xFD20
#define PALEBLUE    0xFFE0
#define GOLD        0x87E0
#define DARKGOLD    0x43A0

const int CX = 120;
const int CY = 120;

float pupilX = 0, pupilY = 0;
float targetX = 0, targetY = 0;
unsigned long nextLook = 0, nextBlink = 0, blinkStart = 0;
bool blinking = false;

float smoothStep(float a, float b, float t) {
  return a + (b - a) * t;
}

// Eye-opening half width at a given y. Almond-shaped, with pointed corners.
int eyeHalfWidth(float y, float halfH, float halfW) {
  float n = fabsf(y / halfH);
  if (n >= 1.0f) return 0;
  float taper = sqrtf(1.0f - n * n);
  // Sharper at the corners, fuller through the middle.
  return (int)(halfW * taper * (0.28f + 0.72f * (1.0f - n)));
}

bool insideEye(int x, int y, int halfH, int halfW) {
  int hw = eyeHalfWidth((float)y, (float)halfH, (float)halfW);
  return abs(x) <= hw;
}

void drawEyeShape(int halfH, int halfW, uint16_t color) {
  for (int y = -halfH; y <= halfH; y++) {
    int hw = eyeHalfWidth((float)y, (float)halfH, (float)halfW);
    if (hw > 0) gfx->drawFastHLine(CX - hw, CY + y, hw * 2 + 1, color);
  }
}

void drawCurvedLid(int halfH, int halfW, uint16_t color, int thickness) {
  for (int t = 0; t < thickness; t++) {
    int hh = halfH + t;
    int hw = halfW + t / 2;
    int px = CX - hw, py = CY;
    for (int x = -hw; x <= hw; x++) {
      float n = (float)x / (float)hw;
      int y = (int)(-hh * sqrtf(fmaxf(0.0f, 1.0f - n*n)) * 0.64f);
      int sx = CX + x, sy = CY + y;
      if (x != -hw) gfx->drawLine(px, py, sx, sy, color);
      px = sx; py = sy;
    }
  }
}

void drawLowerLid(int halfH, int halfW, uint16_t color, int thickness) {
  for (int t = 0; t < thickness; t++) {
    int hh = halfH + t;
    int hw = halfW + t / 2;
    int px = CX - hw, py = CY;
    for (int x = -hw; x <= hw; x++) {
      float n = (float)x / (float)hw;
      int y = (int)(hh * sqrtf(fmaxf(0.0f, 1.0f - n*n)) * 0.64f);
      int sx = CX + x, sy = CY + y;
      if (x != -hw) gfx->drawLine(px, py, sx, sy, color);
      px = sx; py = sy;
    }
  }
}

void drawIris(int ix, int iy) {
  // Iris scaled down to stay inside the circular display while allowing gaze movement.
  gfx->fillCircle(ix, iy, 82, IRISDARK);
  gfx->fillCircle(ix, iy, 78, IRISBLUE);
  gfx->fillCircle(ix, iy, 70, BLUE);
  gfx->fillCircle(ix, iy, 59, CYAN);
  gfx->fillCircle(ix, iy, 47, BLUE);

  // Dense Halloween radial fibers: orange ember tones with toxic-green accents.
  for (int a = 0; a < 360; a += 3) {
    float r = a * PI / 180.0f;
    int inner = 46 + (a % 4) * 2;
    int outer = 73 + (a % 7);
    uint16_t c;
    if ((a % 21) < 4) c = GOLD;
    else if ((a % 12) < 4) c = PALEBLUE;
    else if ((a % 6) < 3) c = CYAN;
    else c = IRISDARK;
    int x1 = ix + (int)(inner * cosf(r));
    int y1 = iy + (int)(inner * sinf(r));
    int x2 = ix + (int)(outer * cosf(r));
    int y2 = iy + (int)(outer * sinf(r));
    gfx->drawLine(x1, y1, x2, y2, c);
  }

  // Multiple fine concentric rings add depth and a natural iris texture.
  gfx->drawCircle(ix, iy, 76, CYAN);
  gfx->drawCircle(ix, iy, 66, IRISDARK);
  gfx->drawCircle(ix, iy, 56, PALEBLUE);
  gfx->drawCircle(ix, iy, 47, GOLD);
  gfx->drawCircle(ix, iy, 43, DARKGOLD);

  // Enlarged pupil, kept central and glossy.
  gfx->fillCircle(ix, iy, 36, BLACK);
  gfx->drawCircle(ix, iy, 36, IRISDARK);

  // Catchlights on the iris/pupil.
  gfx->fillCircle(ix - 23, iy - 27, 11, WHITE);
  gfx->fillCircle(ix - 19, iy - 30, 5, PALEBLUE);
  gfx->fillCircle(ix + 24, iy + 21, 5, WHITE);
}

void drawEye(float px, float py, float openness) {
  gfx->fillScreen(BLACK);

  // Slightly offset iris movement, limited so it stays inside the round display.
  int ix = CX + (int)(px * 0.75f);
  int iy = CY + (int)(py * 0.65f);

  // Dark circular background and safe margin around the iris.
  gfx->fillCircle(CX, CY, 119, 0x0841);
  drawIris(ix, iy);

  // Blink with a black shutter only: no skin/cream-colored eyelid.
  // Two black panels slide toward the center and then reopen.
  if (openness < 0.98f) {
    int coverH = (int)((1.0f - openness) * 120.0f);
    if (coverH > 0) {
      gfx->fillRect(0, 0, 240, coverH, BLACK);
      gfx->fillRect(0, 240 - coverH, 240, coverH, BLACK);
    }
    if (openness < 0.18f) {
      gfx->fillRect(0, 0, 240, 240, BLACK);
    }
  }

  gfx->flush();
}


volatile uint32_t radioSuccessCount = 0;
volatile uint32_t radioFailCount = 0;

#if ESP_ARDUINO_VERSION_MAJOR >= 3
void onDataSent(const wifi_tx_info_t *tx_info, esp_now_send_status_t status) {
#else
void onDataSent(const uint8_t *mac_addr, esp_now_send_status_t status) {
#endif
  if (status == ESP_NOW_SEND_SUCCESS) radioSuccessCount++;
  else radioFailCount++;
}

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println("Halloween EYE MASTER - ESP-NOW");

  panel->begin();
  panel->fillScreen(BLACK);

  if (!gfx->begin()) {
    while (true) delay(1000);
  }

  randomSeed(micros());
  nextLook = millis() + 900;
  nextBlink = millis() + 2300;
  drawEye(0, 0, 1.0f);

  // Start ESP-NOW only AFTER the display canvas has initialized.
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  esp_wifi_set_channel(ESPNOW_CHANNEL, WIFI_SECOND_CHAN_NONE);

  if (esp_now_init() == ESP_OK) {
    esp_now_register_send_cb(onDataSent);
    esp_now_peer_info_t peerInfo = {};
    const uint8_t slaveMac[6] = {0xD4, 0x05, 0x92, 0xE7, 0xCB, 0x84};
    memcpy(peerInfo.peer_addr, slaveMac, 6); // exact SLAVE MAC
    peerInfo.channel = ESPNOW_CHANNEL;
    peerInfo.ifidx = WIFI_IF_STA;
    peerInfo.encrypt = false;
    if (esp_now_add_peer(&peerInfo) == ESP_OK) {
      Serial.println("ESP-NOW MASTER READY - unicast to D4:05:92:E7:CB:84");
    } else {
      Serial.println("ESP-NOW peer add FAILED");
    }
  } else {
    Serial.println("ESP-NOW init FAILED");
  }
}

const uint8_t slaveAddress[6] = {0xD4, 0x05, 0x92, 0xE7, 0xCB, 0x84};

uint32_t packetSequence = 0;
uint32_t sendOkCount = 0;
uint32_t sendFailCount = 0;
unsigned long lastStatsPrint = 0;

void loop() {
  unsigned long now = millis();

  if (now >= nextLook && !blinking) {
    targetX = random(-42, 43);
    targetY = random(-30, 31);
    nextLook = now + random(450, 1400);
  }

  if (!blinking && now >= nextBlink) {
    blinking = true;
    blinkStart = now;
  }

  float openness = 1.0f;
  if (blinking) {
    unsigned long elapsed = now - blinkStart;
    if (elapsed < 130) {
      openness = 1.0f - (float)elapsed / 130.0f;
    } else if (elapsed < 260) {
      openness = (float)(elapsed - 130) / 130.0f;
    } else {
      blinking = false;
      nextBlink = now + random(2200, 5200);
    }
  }

  pupilX = smoothStep(pupilX, targetX, 0.22f);
  pupilY = smoothStep(pupilY, targetY, 0.22f);

  drawEye(pupilX, pupilY, openness);

  EyePacket packet;
  packet.seq = ++packetSequence;
  packet.pupilX = pupilX;
  packet.pupilY = pupilY;
  packet.openness = openness;
  esp_err_t sendResult = esp_now_send(slaveAddress, (const uint8_t *)&packet, sizeof(packet)); // unicast to SLAVE MAC
  if (sendResult == ESP_OK) sendOkCount++;
  else {
    sendFailCount++;
    Serial.print("ESP-NOW send error: ");
    Serial.print((int)sendResult);
    Serial.print(" (");
    Serial.print(esp_err_to_name(sendResult));
    Serial.println(")");
  }

  if (now - lastStatsPrint >= 1000) {
    lastStatsPrint = now;
    Serial.print("TX seq="); Serial.print(packetSequence);
    Serial.print(" queued_OK="); Serial.print(sendOkCount);
    Serial.print(" queue_errors="); Serial.print(sendFailCount);
    Serial.print(" radio_OK="); Serial.print((uint32_t)radioSuccessCount);
    Serial.print(" radio_FAIL="); Serial.println((uint32_t)radioFailCount);
  }

  delay(40);
}
