#ifndef CRC64__H__
#define CRC64__H__

#define POLY64REV	0xd800000000000000ULL
#include <uint64.h>

#define CRC64INIT (0xffffffffffffffffULL)

extern uint64 crc64_update(uint64 crc, const unsigned char* bytes, long len);

#endif
