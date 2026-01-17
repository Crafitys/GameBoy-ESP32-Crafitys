#include "sdl.h"
#include <Arduino.h>
#include <TFT_eSPI.h>
#include <SPI.h>

/* ================== PINOUT ================== */
#define TFT_BL 22

#define BTN_LEFT   GPIO_NUM_1
#define BTN_RIGHT  GPIO_NUM_3
#define BTN_UP     GPIO_NUM_33
#define BTN_DOWN   GPIO_NUM_25
#define BTN_SELECT GPIO_NUM_26
#define BTN_START  GPIO_NUM_32
#define BTN_A      GPIO_NUM_27
#define BTN_B      GPIO_NUM_21

/* ================== SCREEN ================== */
#define GAMEBOY_WIDTH   160
#define GAMEBOY_HEIGHT  144

#define DRAW_WIDTH      160
#define DRAW_HEIGHT     144

#define SCREEN_WIDTH    240
#define SCREEN_HEIGHT   240

// =======================
// SCALE Game Boy -> 240x240
// =======================
static const int SRC_W = 160;
static const int SRC_H = 144;

static const int TFT_W = 240;
static const int TFT_H = 240;

// Taille finale max en gardant le ratio : 240 x 216
static const int DST_W = 240;
static const int DST_H = 216;

// Centrage vertical (bords noirs en haut/bas)
static const int Y_OFF = (TFT_H - DST_H) / 2;  // 12

// Tables de conversion (évite divisions dans la boucle)
static bool scaleMapsReady = false;
static uint16_t xMap[DST_W];
static uint16_t yMap[DST_H];

// Palette 4 couleurs en RGB565 (tu peux ajuster si tu veux un look différent)
static uint16_t palette565[4] = {
  0xE7E0, // clair
  0xA6C8,
  0x5D80,
  0x2C40  // foncé
};

static void prepareScaleMaps() {
  if (scaleMapsReady) return;

  for (int x = 0; x < DST_W; x++) {
    xMap[x] = (x * SRC_W) / DST_W;   // x*160/240
  }
  for (int y = 0; y < DST_H; y++) {
    yMap[y] = (y * SRC_H) / DST_H;   // y*144/216
  }

  scaleMapsReady = true;
}


/* ================== TFT ================== */
TFT_eSPI tft = TFT_eSPI();

/* ================== FRAMEBUFFER ================== */
static uint8_t *frame_buffer;

/* ================== INPUT ================== */
static int button_start, button_select, button_a, button_b;
static int button_down, button_up, button_left, button_right;

/* ================== DRAW TASK ================== */
static volatile bool frame_ready = false;
TaskHandle_t draw_task_handle;

/* ================== PALETTE ================== */
static uint16_t gb_palette[4] = {
  TFT_WHITE,
  TFT_LIGHTGREY,
  TFT_DARKGREY,
  TFT_BLACK
};

/* ================== BACKLIGHT ================== */
void backlighting(bool state) {
  digitalWrite(TFT_BL, state ? HIGH : LOW);
}

/* ================== DRAW BUTTON ================== */
void draw_button(bool value, int x, int y, const char *label = nullptr) {
  tft.fillCircle(x, y, 7, value ? TFT_WHITE : TFT_BLACK);
  tft.drawCircle(x, y, 7, TFT_WHITE);

  if (label) {
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setTextSize(1);
    tft.setCursor(x - 10, y + 12);
    tft.print(label);
  }
}

/* ================== FRAMEBUFFER DRAW ================== */
void draw_framebuffer() {
  prepareScaleMaps();

  // Buffer d'une ligne (240 pixels en RGB565)
  static uint16_t linebuf[DST_W];

  // On dessine SEULEMENT la zone image (240x216) centrée.
  // Les bandes noires seront dessinées une seule fois ailleurs.
  for (int dy = 0; dy < DST_H; dy++) {
    const int sy = yMap[dy];

    for (int dx = 0; dx < DST_W; dx++) {
      const int sx = xMap[dx];
      uint8_t p = frame_buffer[sy * SRC_W + sx] & 3;
      linebuf[dx] = palette565[p];
    }

    tft.setAddrWindow(0, dy + Y_OFF, DST_W, 1);
    tft.pushColors(linebuf, DST_W, true);

    // Petit yield pour éviter watchdog / laisser l'autre task respirer
    // (important si tu dessines dans une task FreeRTOS)
    if ((dy & 15) == 15) vTaskDelay(1);
  }
}




/* ================== DRAW TASK ================== */
void draw_task(void *parameter) {
  for (;;) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY); // dort jusqu'à notification
    draw_framebuffer();                      // dessine 1 frame
  }
}


void sdl_frame(void) {
  if (draw_task_handle) xTaskNotifyGive(draw_task_handle);
}


/* ================== SDL INIT ================== */
void sdl_init(void) {
  frame_buffer = new uint8_t[DRAW_WIDTH * DRAW_HEIGHT];

  tft.init();
  tft.setRotation(2);
  tft.fillScreen(TFT_BLACK);

  pinMode(TFT_BL, OUTPUT);
  backlighting(true);

  gpio_num_t gpios[] = {
    BTN_LEFT, BTN_RIGHT, BTN_UP, BTN_DOWN,
    BTN_START, BTN_SELECT, BTN_A, BTN_B
  };

  for (gpio_num_t pin : gpios) {
    pinMode(pin, INPUT_PULLUP);
  }
  xTaskCreatePinnedToCore(
  draw_task,
  "drawTask",
  8192,
  NULL,
  1,
  &draw_task_handle,
  0
);

}

/* ================== SDL UPDATE ================== */
int sdl_update(void) {
  button_up     = !gpio_get_level(BTN_UP);
  button_down   = !gpio_get_level(BTN_DOWN);
  button_left   = !gpio_get_level(BTN_LEFT);
  button_right  = !gpio_get_level(BTN_RIGHT);

  button_start  = !gpio_get_level(BTN_START);
  button_select = !gpio_get_level(BTN_SELECT);
  button_a      = !gpio_get_level(BTN_A);
  button_b      = !gpio_get_level(BTN_B);

  sdl_frame();
  return 0;
}

/* ================== INPUT API ================== */
unsigned int sdl_get_buttons(void) {
  return (button_start << 3) |
         (button_select << 2) |
         (button_b << 1) |
         button_a;
}

unsigned int sdl_get_directions(void) {
  return (button_down << 3) |
         (button_up << 2) |
         (button_left << 1) |
         button_right;
}

/* ================== FRAME API ================== */
uint8_t *sdl_get_framebuffer(void) {
  return frame_buffer;
}


