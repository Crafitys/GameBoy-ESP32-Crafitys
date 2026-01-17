#ifndef MEM_H
#define MEM_H
#ifdef __cplusplus

unsigned int mem_sram_dirty(void);
void mem_sram_clear_dirty(void);
unsigned int mem_sram_seq(void);


extern "C" {

#endif

#include <cinttypes>

#include "rom.h"
void gameboy_mem_init(void);
unsigned char mem_get_byte(unsigned short);
unsigned short mem_get_word(unsigned short);
void mem_write_byte(unsigned short, unsigned char);
void mem_write_word(unsigned short, unsigned short);
void mem_bank_switch(unsigned int);
const unsigned char *mem_get_raw();
uint32_t mem_get_bank_switches();
#ifdef __cplusplus
}
unsigned int mem_sram_dirty(void);
void mem_sram_clear_dirty(void);


#endif /* end of __cplusplus */
#endif
