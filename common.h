#ifndef COMMON_H
#define COMMON_H

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string>
#include <vector>

typedef std::string string_t;
typedef std::vector<uint8_t> bytevec_t;

void append_d(bytevec_t &out, uint32_t num);
void append_q(bytevec_t &out, uint64_t num);
void append_b(bytevec_t &out, const bytevec_t &bv);
void append_v(bytevec_t &out, const void *v, size_t len);

void sprnt(string_t *out, const char *format, ...);
void vsprnt(string_t *out, const char *format, va_list va);

#endif // COMMON_H
