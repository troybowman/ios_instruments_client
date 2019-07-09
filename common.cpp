#include "common.h"

#define MAXSTR 1024

//-----------------------------------------------------------------------------
void append_v(bytevec_t &out, const void *v, size_t len)
{
  const uint8_t *begin = (const uint8_t *)v;
  const uint8_t *end   = begin + len;
  out.insert(out.end(), begin, end);
}

//-----------------------------------------------------------------------------
void append_d(bytevec_t &out, uint32_t num)
{
  append_v(out, &num, sizeof(num));
}

//-----------------------------------------------------------------------------
void append_q(bytevec_t &out, uint64_t num)
{
  append_v(out, &num, sizeof(num));
}

//-----------------------------------------------------------------------------
void append_b(bytevec_t &out, const bytevec_t &bv)
{
  append_v(out, bv.data(), bv.size());
}

//-----------------------------------------------------------------------------
void vsprnt(string_t *out, const char *format, va_list va)
{
  char buf[MAXSTR];
  if ( out != NULL && vsnprintf(buf, sizeof(buf), format, va) > 0 )
    *out = buf;
}

//-----------------------------------------------------------------------------
void sprnt(string_t *out, const char *format, ...)
{
  va_list va;
  va_start(va, format);
  vsprnt(out, format, va);
  va_end(va);
}
