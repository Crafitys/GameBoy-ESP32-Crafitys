#include <esp32-hal.h>
#include <stdio.h>

#include <Arduino.h>
#include <LittleFS.h>

#include "cpu.h"
#include "gbrom.h"
#include "lcd.h"
#include "mem.h"
#include "rom.h"
#include "sd.h"
#include "sdl.h"
#include "timer.h"

TaskHandle_t emu_task_handle;

static constexpr uint32_t emulator_cpu_freq = 4200000 / 4;
static constexpr uint32_t frames_per_sec = 60;
static uint32_t cpu_freq = 0;
static uint32_t cycles_per_frame = 0;
static uint32_t cycles_in_micro_sec = 0;

// ================== Battery save via LittleFS ==================
static constexpr uint32_t SRAM_BASE = 0xA000;
static constexpr uint32_t SRAM_SIZE = 0x2000;  // 8KB
static const char* SAVE_PATH = "/zelda.sav";

static bool fs_ok = false;

// “quiet save” control
static unsigned int last_seq = 0;
static uint32_t last_seq_change_ms = 0;
static uint32_t last_save_request_ms = 0;

// Task save request
static volatile bool save_request = false;
static volatile bool save_busy = false;

// --- LittleFS init ---
static void fs_init_or_format() {
  fs_ok = LittleFS.begin(true);
}

// --- Load SRAM once at boot ---
static void load_sram_from_fs() {
  if (!fs_ok) return;

  uint8_t* raw = (uint8_t*)mem_get_raw();
  if (!raw) return;

  File f = LittleFS.open(SAVE_PATH, "r");
  if (!f) return;

  if ((size_t)f.size() != SRAM_SIZE) {
    f.close();
    return;
  }

  size_t got = f.read(raw + SRAM_BASE, SRAM_SIZE);
  f.close();

  if (got == SRAM_SIZE) {
    mem_sram_clear_dirty();
  }
}

// --- Actual write, called from task only ---
static bool write_sram_file_task() {
  if (!fs_ok) return false;

  uint8_t* raw = (uint8_t*)mem_get_raw();
  if (!raw) return false;

  // Ecriture atomique : write dans temp puis rename
  const char* TMP_PATH = "/zelda.tmp";

  File f = LittleFS.open(TMP_PATH, "w");
  if (!f) return false;

  const uint32_t block = 2048;
  for (uint32_t off = 0; off < SRAM_SIZE; off += block) {
    size_t put = f.write(raw + SRAM_BASE + off, block);
    if (put != block) {
      f.close();
      LittleFS.remove(TMP_PATH);
      return false;
    }
    if ((off & 0x7FF) == 0) vTaskDelay(1);
  }

  f.flush();
  f.close();

  // remplace l'ancien fichier
  LittleFS.remove(SAVE_PATH);
  bool ok = LittleFS.rename(TMP_PATH, SAVE_PATH);
  if (!ok) {
    LittleFS.remove(TMP_PATH);
    return false;
  }

  return true;
}

// --- FreeRTOS task that performs saving ---
static void save_task(void* pv) {
  (void)pv;
  for (;;) {
    if (save_request && !save_busy) {
      save_busy = true;

      // Sauve
      if (write_sram_file_task()) {
        mem_sram_clear_dirty();
      }

      save_request = false;
      save_busy = false;
    }

    vTaskDelay(pdMS_TO_TICKS(50));
  }
}

// --- In loop: request save only when SRAM quiet ---
static void request_save_when_quiet() {
  if (!mem_sram_dirty()) return;
  if (save_busy) return;          // évite de spam pendant une save
  if (save_request) return;       // déjà demandé

  uint32_t now = millis();
  unsigned int seq = mem_sram_seq();

  if (last_seq_change_ms == 0) {
    last_seq = seq;
    last_seq_change_ms = now;
    return;
  }

  if (seq != last_seq) {
    last_seq = seq;
    last_seq_change_ms = now;
    return;
  }

  // stable depuis 4s
  if (now - last_seq_change_ms < 8000) return;

  // Anti-usure : au max une demande toutes les 20s
  if (now - last_save_request_ms < 60000) return;

  save_request = true;
  last_save_request_ms = now;
  last_seq_change_ms = 0;
}
// ===============================================================

void setup() {
  int r = rom_init(gb_rom);
  (void)r;

  sdl_init();
  // sd_init();

  fs_init_or_format();

  gameboy_mem_init();

  load_sram_from_fs();

  cpu_init();

  cpu_freq = getCpuFrequencyMhz();
  printf("CPU Freq = %u Mhz\n", cpu_freq);
  cpu_freq *= 1000000;

  cycles_per_frame = cpu_freq / frames_per_sec;
  cycles_in_micro_sec = cpu_freq / 1000000;
  printf("cycles_per_frame %d cycles_in_micro_sec %d\n",
         cycles_per_frame, cycles_in_micro_sec);

  // Lancer la tâche de sauvegarde sur core 0 (souvent plus safe)
  xTaskCreatePinnedToCore(save_task, "save_task", 4096, NULL, 1, NULL, 0);
}

void loop() {
  bool screen_updated = false;

  uint32_t start_frame_cycle = ESP.getCycleCount();
  uint32_t emulator_cpu_cycle = 0;

  while (!screen_updated) {
    emulator_cpu_cycle = cpu_cycle();
    screen_updated = lcd_cycle(emulator_cpu_cycle);
    timer_cycle(emulator_cpu_cycle);

    if ((ESP.getCycleCount() & 0x1FFF) == 0) {
      yield();
    }
  }

  sdl_update();

  // delay next frame
  uint32_t end_frame_cycle = ESP.getCycleCount();
  uint32_t cycles_delta = end_frame_cycle - start_frame_cycle;
  if (cycles_delta < cycles_per_frame) {
    delayMicroseconds(cycles_delta / cycles_in_micro_sec);
  }

  // Demande de sauvegarde (pas d'écriture ici)
  request_save_when_quiet();
}
