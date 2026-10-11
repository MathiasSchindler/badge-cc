#ifndef BADGE_IR_INTEGER_H
#define BADGE_IR_INTEGER_H

#include <stdint.h>

uint64_t ir_integer_mask(unsigned);
uint64_t ir_integer_extend(uint64_t, unsigned);
int ir_integer_binary(const char *, unsigned, uint64_t, uint64_t, uint64_t *);
int ir_integer_compare(const char *, unsigned, uint64_t, uint64_t, uint64_t *);

#endif
