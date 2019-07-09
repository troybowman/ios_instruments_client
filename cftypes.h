#ifndef CFTYPES_H
#define CFTYPES_H

#include <CoreFoundation/CoreFoundation.h>
#include "common.h"

//------------------------------------------------------------------------------
// convert the given CFString to an STL string
string_t to_stlstr(CFStringRef ref);

//------------------------------------------------------------------------------
// get a human readable description of the given CF object
string_t get_description(CFTypeRef ref);

//------------------------------------------------------------------------------
// serialize a CF object
void archive(bytevec_t *buf, CFTypeRef ref);

//------------------------------------------------------------------------------
// deserialize a CF object
CFTypeRef unarchive(const uint8_t *buf, size_t bufsize);

//------------------------------------------------------------------------------
// deserialize an array of CF objects
CFArrayRef deserialize(const uint8_t *buf, size_t bufsize, string_t *errbuf = NULL);

#endif // CFTYPES_H
