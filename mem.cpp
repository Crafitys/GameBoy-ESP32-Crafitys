#ifndef INTER_MODULE_OPT

#include "mem.h"

#include <stdlib.h>
#include <string.h>

#include "cpu.h"
#include "interrupt.h"
#include "lcd.h"
#include "mbc.h"
#include "rom.h"
#include "sdl.h"
#include "timer.h"

// ===== SRAM dirty flag + write sequence =====
static volatile unsigned int sram_dirty = 0;
static volatile unsigned int sram_seq = 0;

unsigned int mem_sram_dirty(void) { return sram_dirty; }
void mem_sram_clear_dirty(void) { sram_dirty = 0; }
unsigned int mem_sram_seq(void) { return sram_seq; }
// ============================================

static const uint8_t* rom0 = nullptr;
static const uint8_t* romx = nullptr;
static uint32_t romx_bank = 1;

static unsigned char* mem;
static int DMA_pending = 0;
static int joypad_select_buttons, joypad_select_directions;
static uint32_t bank_switches = 0;

uint32_t mem_get_bank_switches() { return bank_switches; }

void mem_bank_switch(unsigned int n) {
  const uint8_t* b = rom_getbytes();
  bank_switches++;
  romx_bank = n;
  romx = b + n * 0x4000;
}

const unsigned char* mem_get_raw() { return mem; }

unsigned char mem_get_byte(unsigned short i) {
  unsigned long elapsed;
  unsigned char mask = 0;

  if (i < 0x4000) return rom0[i];
  if (i < 0x8000) return romx[i - 0x4000];

  if (DMA_pending && i < 0xFF80) {
    elapsed = cpu_get_cycles() - DMA_pending;
    if (elapsed >= 160) DMA_pending = 0;
    else return mem[0xFE00 + elapsed];
  }

  if (i < 0xFF00) return mem[i];

  switch (i) {
    case 0xFF00:
      if (!joypad_select_buttons) mask = sdl_get_buttons();
      if (!joypad_select_directions) mask = sdl_get_directions();
      return 0xC0 | (0xF ^ mask) |
             (joypad_select_buttons | joypad_select_directions);
    case 0xFF04: return timer_get_div();
    case 0xFF05: return timer_get_counter();
    case 0xFF06: return timer_get_modulo();
    case 0xFF07: return timer_get_tac();
    case 0xFF0F: return interrupt_get_IF();
    case 0xFF41: return lcd_get_stat();
    case 0xFF44: return lcd_get_line();
    case 0xFF4D: return 0xFF;
    case 0xFFFF: return interrupt_get_mask();
  }

  return mem[i];
}

unsigned short mem_get_word(unsigned short i) {
  if (i < 0x8000) {
    uint8_t lo = mem_get_byte(i);
    uint8_t hi = mem_get_byte(i + 1);
    return lo | (hi << 8);
  }
  return mem[i] | (mem[i + 1] << 8);
}

void mem_write_byte(unsigned short d, unsigned char i) {
  unsigned int filtered = 0;

  switch (rom_get_mapper()) {
    case NROM:
      if (d < 0x8000) filtered = 1;
      break;
    case MBC2:
    case MBC3:
      filtered = MBC3_write_byte(d, i);
      break;
    case MBC1:
      filtered = MBC1_write_byte(d, i);
      break;
  }

  // Si le MBC "filtre", on laisse passer la SRAM quand même (Zelda)
  if (filtered) {
    if (d >= 0xA000 && d <= 0xBFFF) {
      if (mem[d] != i) {
        sram_dirty = 1;
        sram_seq++; // <= très important
        mem[d] = i;
      }
    }
    return;
  }

  switch (d) {
    case 0xFF00:
      joypad_select_buttons = i & 0x20;
      joypad_select_directions = i & 0x10;
      break;
    case 0xFF04: timer_set_div(i); break;
    case 0xFF05: timer_set_counter(i); break;
    case 0xFF06: timer_set_modulo(i); break;
    case 0xFF07: timer_set_tac(i); break;
    case 0xFF0F: interrupt_set_IF(i); break;
    case 0xFF40: lcd_write_control(i); break;
    case 0xFF41: lcd_write_stat(i); break;
    case 0xFF42: lcd_write_scroll_y(i); break;
    case 0xFF43: lcd_write_scroll_x(i); break;
    case 0xFF45: lcd_set_ly_compare(i); break;
    case 0xFF46:
      memcpy(&mem[0xFE00], &mem[i * 0x100], 0xA0);
      DMA_pending = cpu_get_cycles();
      break;
    case 0xFF47: lcd_write_bg_palette(i); break;
    case 0xFF48: lcd_write_spr_palette1(i); break;
    case 0xFF49: lcd_write_spr_palette2(i); break;
    case 0xFF4A: lcd_set_window_y(i); break;
    case 0xFF4B: lcd_set_window_x(i); break;
    case 0xFFFF:
      interrupt_set_mask(i);
      return;
  }

  if (d >= 0xA000 && d <= 0xBFFF) {
    if (mem[d] != i) {
      sram_dirty = 1;
      sram_seq++; // <= très important
    }
  }

  mem[d] = i;
}

void mem_write_word(unsigned short d, unsigned short i) {
  // si le word touche SRAM
  if ((d >= 0xA000 && d <= 0xBFFF) || (d + 1 >= 0xA000 && d + 1 <= 0xBFFF)) {
    sram_dirty = 1;
    sram_seq++;
  }
  mem[d] = i & 0xFF;
  mem[d + 1] = i >> 8;
}

void gameboy_mem_init(void) {
  mem = (unsigned char*)calloc(1, 0x10000);

  rom0 = rom_getbytes();
  romx_bank = 1;
  romx = rom0 + 0x4000;

  sram_dirty = 0;
  sram_seq = 0;

  mem[0xFF10] = 0x80;
  mem[0xFF11] = 0xBF;
  mem[0xFF12] = 0xF3;
  mem[0xFF14] = 0xBF;
  mem[0xFF16] = 0x3F;
  mem[0xFF19] = 0xBF;
  mem[0xFF1A] = 0x7F;
  mem[0xFF1B] = 0xFF;
  mem[0xFF1C] = 0x9F;
  mem[0xFF1E] = 0xBF;
  mem[0xFF20] = 0xFF;
  mem[0xFF23] = 0xBF;
  mem[0xFF24] = 0x77;
  mem[0xFF25] = 0xF3;
  mem[0xFF26] = 0xF1;
  mem[0xFF40] = 0x91;
  mem[0xFF47] = 0xFC;
  mem[0xFF48] = 0xFF;
  mem[0xFF49] = 0xFF;
}

#endif
