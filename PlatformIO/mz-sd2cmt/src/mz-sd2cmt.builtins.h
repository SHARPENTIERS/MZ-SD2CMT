#pragma once

#include <stdfix.h>

void __builtin_avr_nop(void);
void __builtin_avr_sei(void);
void __builtin_avr_cli(void);
void __builtin_avr_sleep(void);
void __builtin_avr_wdr(void);
unsigned char __builtin_avr_swap(unsigned char);
unsigned int __builtin_avr_fmul(unsigned char, unsigned char);
int __builtin_avr_fmuls(char, char);
int __builtin_avr_fmulsu(char, unsigned char);
void __builtin_avr_delay_cycles(unsigned long ticks);
char __builtin_avr_flash_segment(const __memx void*);
uint8_t __builtin_avr_insert_bits(uint32_t map, uint8_t bits, uint8_t val);
void __builtin_avr_nops(unsigned count);
