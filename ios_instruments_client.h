#ifndef IOS_INSTRUMENTS_CLIENT
#define IOS_INSTRUMENTS_CLIENT

#include "cftypes.h"

//-----------------------------------------------------------------------------
struct DTXMessageHeader
{
  uint32_t magic;
  uint32_t cb;
  uint16_t fragmentId;
  uint16_t fragmentCount;
  uint32_t length;
  uint32_t identifier;
  uint32_t conversationIndex;
  uint32_t channelCode;
  uint32_t expectsReply;
};

//-----------------------------------------------------------------------------
struct DTXMessagePayloadHeader
{
  uint32_t flags;
  uint32_t auxiliaryLength;
  uint64_t totalLength;
};

//------------------------------------------------------------------------------
// helper class for serializing method arguments
class message_aux_t
{
  bytevec_t buf;

public:
  void append_int(int32_t val);
  void append_long(int64_t val);
  void append_obj(CFTypeRef obj);

  void get_bytes(bytevec_t *out) const;
};

#endif // IOS_INSTRUMENTS_CLIENT
