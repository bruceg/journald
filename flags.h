#ifndef JOURNALD__FLAGS__H__
#define JOURNALD__FLAGS__H__

#define HEADER_SIZE (4+4+4+4+4)

#define RECORD_TYPE 0xf
#define RECORD_EOT 0
#define RECORD_INFO 0x01
#define RECORD_DATA 0x02
#define RECORD_EOS 0x04
#define RECORD_ABORT 0x08

#endif
