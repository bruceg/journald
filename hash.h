#ifndef JOURNALD__HASH__H__
#define JOURNALD__HASH__H__

#include "md4.h"
#define HASH_SIZE 16
typedef struct md4_ctx HASH_CTX;

#define hash_init(H) do{ md4_init_ctx(H); }while(0)
#define hash_update(H,B,L) do{ md4_process_bytes(B,L,H); }while(0)
#define hash_finish(H,BUF) do{ md4_finish_ctx(H,BUF); }while(0)

#endif
