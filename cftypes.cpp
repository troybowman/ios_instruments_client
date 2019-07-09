#include <Foundation/Foundation.h>
#include "cftypes.h"

//------------------------------------------------------------------------------
string_t to_stlstr(CFStringRef ref)
{
  if ( ref == NULL )
    return "";

  CFIndex length = CFStringGetLength(ref);
  if ( length <= 0 )
    return "";

  CFIndex bufsize = CFStringGetMaximumSizeForEncoding(length, kCFStringEncodingUTF8) + 1;
  char *buf = (char *)calloc(bufsize, 1);

  string_t ret;
  if ( CFStringGetCString(ref, buf, bufsize, kCFStringEncodingUTF8) )
    ret = buf;

  free(buf);
  return ret;
}

//------------------------------------------------------------------------------
string_t get_description(CFTypeRef ref)
{
  CFStringRef desc = CFCopyDescription(ref);
  string_t ret = to_stlstr(desc);
  CFRelease(desc);
  return ret;
}

//-----------------------------------------------------------------------------
void archive(bytevec_t *buf, CFTypeRef ref)
{
  @autoreleasepool
  {
    id object = (__bridge id)ref;
    NSData *data = [NSKeyedArchiver archivedDataWithRootObject:object];
    const void *bytes = [data bytes];
    int length = [data length];
    append_v(*buf, bytes, length);
  }
}

//-----------------------------------------------------------------------------
CFTypeRef unarchive(const uint8_t *buf, size_t bufsize)
{
  @autoreleasepool
  {
    NSData *data = [NSData dataWithBytesNoCopy:(void *)buf length:bufsize freeWhenDone:false];
    id object = [NSKeyedUnarchiver unarchiveObjectWithData:data];
    return (__bridge CFTypeRef)[object retain];
  }
}

//-----------------------------------------------------------------------------
CFArrayRef deserialize(
        const uint8_t *buf,
        size_t bufsize,
        string_t *errbuf)
{
  if ( bufsize < 16 )
  {
    sprnt(errbuf, "Error: buffer of size 0x%lx is too small for a serialized array", bufsize);
    return NULL;
  }

  uint64_t size = *((uint64_t *)buf+1);
  if ( size > bufsize )
  {
    sprnt(errbuf, "size of array object (%llx) is larger than total length of data (%lx)", size, bufsize);
    return NULL;
  }

  CFMutableArrayRef array = CFArrayCreateMutable(NULL, 0, &kCFTypeArrayCallBacks);

  uint64_t off = sizeof(uint64_t) * 2;
  uint64_t end = off + size;

  while ( off < end )
  {
    int length = 0;
    int type = *((int *)(buf+off));
    off += sizeof(int);

    CFTypeRef ref = NULL;

    switch ( type )
    {
      case 2:
        // archived object
        length = *((int *)(buf+off));
        off += sizeof(int);
        ref = unarchive(buf+off, length);
        break;

      case 3:
      case 5:
        // 32-bit int
        ref = CFNumberCreate(NULL, kCFNumberSInt32Type, buf+off);
        length = 4;
        break;

      case 4:
      case 6:
        // 64-bit int
        ref = CFNumberCreate(NULL, kCFNumberSInt64Type, buf+off);
        length = 8;
        break;

      case 10:
        // dictionary key. for arrays, the keys are empty and we ignore them
        continue;

      default:
        // there are more. we will deal with them as necessary
        break;
    }

    if ( ref == NULL )
    {
      sprnt(errbuf, "invalid object at offset %llx, type: %d\n", off, type);
      return NULL;
    }

    CFArrayAppendValue(array, ref);
    CFRelease(ref);
    off += length;
  }

  return (CFArrayRef)array;
}
