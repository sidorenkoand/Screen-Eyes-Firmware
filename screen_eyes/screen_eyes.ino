// Screen Eyes - firmware for ESP32-C3 + 2x GC9A01
//
// Draws two animated anime-style eyes. Control them over Bluetooth LE
// (see ble_control.h, docs/protocol.md) or from the Serial Monitor
// (115200 baud, "Newline" line ending) with the same text commands, e.g.:
//   emo happy    fx hearts    look 0.5 -0.3    blink    wink l    color 3080E0
// Type "help" for the full list (see commands.h).

#include "config.h"
#include "eye_display.h"
#include "eye_animator.h"
#include "eye_renderer.h"
#include "commands.h"
#include "ble_control.h"

// Bluetooth device name shown on the phone.
constexpr const char* kDeviceName = "ScreenEyes";

// Print frames per second to Serial every few seconds.
constexpr bool kPrintFps = true;

EyeDisplay leftEye(PIN_CS_LEFT);
EyeDisplay rightEye(PIN_CS_RIGHT);

// Frame buffer for half a screen (240x120, 57.6 KB). Each eye is drawn as two
// bands (top, bottom): a full 115 KB frame does not fit in one free RAM block.
constexpr int BAND_HEIGHT = SCREEN_SIZE / 2;
LGFX_Sprite frame;

EyeAnimator eyes(esp_random());
EyeRenderer renderer;

// --- Startup ---------------------------------------------------------------

static void showLabel(EyeDisplay& d, uint16_t bg, const char* text) {
  d.fillScreen(bg);
  d.setTextColor(TFT_WHITE, bg);
  d.setTextDatum(textdatum_t::middle_center);
  d.setFont(&fonts::FreeSansBold18pt7b);
  d.drawString(text, SCREEN_SIZE / 2, SCREEN_SIZE / 2);
}

static void initDisplays() {
  resetDisplays();
  if (!leftEye.init())  Serial.println("ERROR: left display init failed");
  if (!rightEye.init()) Serial.println("ERROR: right display init failed");
  initBacklight();

  // Short wiring check: which display is which.
  showLabel(leftEye,  TFT_BLUE, "LEFT");
  showLabel(rightEye, TFT_RED,  "RIGHT");
  delay(1500);
}

void setup() {
  Serial.begin(115200);
  // USB serial (CDC) blocks up to 100 ms per print when the cable is plugged
  // into a PC but no program reads the port. That stalls the main loop and
  // delays Bluetooth commands by seconds. Drop output instead of waiting.
  Serial.setTxTimeoutMs(0);
  delay(500);
  Serial.println("Screen Eyes starting");

  initDisplays();

  frame.setColorDepth(16);
  frame.setPsram(false);
  if (!frame.createSprite(SCREEN_SIZE, BAND_HEIGHT)) {
    Serial.println("ERROR: not enough RAM for frame sprite");
    while (true) delay(1000);
  }
  if (!renderer.begin(SCREEN_SIZE)) {
    Serial.println("ERROR: not enough RAM for iris cache");
    while (true) delay(1000);
  }
  // BLE after the buffers, so the large blocks get their RAM first.
  bleBegin(kDeviceName);

  Serial.printf("Free heap: %u bytes\n", ESP.getFreeHeap());
  Serial.println("Ready. Type 'help' for commands.");
}

// --- Serial commands -------------------------------------------------------

static void pollSerial() {
  static char line[96];
  static size_t len = 0;
  while (Serial.available()) {
    char ch = (char)Serial.read();
    if (ch == '\r') continue;
    if (ch == '\n') {
      line[len] = '\0';
      if (len > 0) {
        char reply[128];
        handleCommand(eyes, line, reply, sizeof(reply));
        Serial.println(reply);
      }
      len = 0;
    } else if (len < sizeof(line) - 1) {
      line[len++] = ch;
    }
  }
}

// --- Bluetooth commands ----------------------------------------------------

static void pollBle() {
  char line[BLE_MAX_COMMAND];
  // Limit per frame so a burst of commands can't stall the animation.
  for (int i = 0; i < 8 && blePopCommand(line, sizeof(line)); i++) {
    char reply[128];
    handleCommand(eyes, line, reply, sizeof(reply));
    bleSendReply(reply);
    Serial.printf("BLE> %s -> %s\n", line, reply);
  }
}

// --- Main loop -------------------------------------------------------------

// Time spent rendering vs. sending to the displays (for the FPS report).
static uint32_t gRenderUs = 0, gPushUs = 0;

static void drawEye(EyeDisplay& display, const EyeFrame& f) {
  uint32_t tp = micros();
  renderer.prepare(f);
  gRenderUs += micros() - tp;
  for (int y = 0; y < SCREEN_SIZE; y += BAND_HEIGHT) {
    uint32_t t0 = micros();
    renderer.drawBand(frame, y);
    uint32_t t1 = micros();
    frame.pushSprite(&display, 0, y);
    gPushUs += micros() - t1;
    gRenderUs += t1 - t0;
  }
}

void loop() {
  pollSerial();
  pollBle();

  eyes.update(millis());

  drawEye(leftEye, eyes.frame(EyeAnimator::kLeft));
  drawEye(rightEye, eyes.frame(EyeAnimator::kRight));

  if (kPrintFps) {
    static uint32_t frames = 0;
    static uint32_t lastReport = millis();
    frames++;
    uint32_t now = millis();
    if (now - lastReport >= 5000) {
      Serial.printf("FPS: %.1f (per frame: render %lu ms, push %lu ms)\n",
                    frames * 1000.0f / (now - lastReport),
                    (unsigned long)(gRenderUs / frames / 1000), (unsigned long)(gPushUs / frames / 1000));
      gRenderUs = gPushUs = 0;
      frames = 0;
      lastReport = now;
    }
  }
}
