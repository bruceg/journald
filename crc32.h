/* Use a type CRC32 variable to store the crc value.
 * Initialise the variable to 0xFFFFFFFF before running the crc routine.
 * for more details see the CRC32.C file */

typedef unsigned long CRC32; /* the type of the 32-bit CRC */

#define CRC32init 0xFFFFFFFFL /* the initializer for the 32-bit CRC */

CRC32 CRC32_block(CRC32 a, unsigned char *s, int len);
/* update running CRC calculation with contents of a buffer */
