#ifndef JOURNALD__HASH__H__
#define JOURNALD__HASH__H__

#include <crc/crc64.h>
#define HASH_SIZE (sizeof(uint64))
typedef uint64 HASH_CTX;

#define hash_init(H) do{ *(H) = CRC64INIT; }while(0)
#define hash_update(H,B,L) do{ *H = crc64_update(*H,B,L); }while(0)
#define hash_finish(H,BUF) do{ uint64 tmp = ~(*(H)); memcpy(BUF, &tmp, sizeof tmp); }while(0)

#endif
