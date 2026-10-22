/* HEVAG (Sony's High Efficiency VAG) frame decoder, adapted from vgmstream (VENDOR.txt, LICENSE).
 * A frame is 16 bytes of one channel: two header bytes (table index, shift, flag) and 28 4-bit codes.
 * The caller keeps one HevagState per channel and interleaves the channels itself. */
#ifndef HEVAG_H
#define HEVAG_H
#include <stdint.h>
enum { HEVAG_FRAME_BYTES=16, HEVAG_FRAME_SAMPLES=28, HEVAG_FLAG_END=7 };
typedef struct { int32_t hist[4]; } HevagState; /* the last four samples, newest first, unclamped */
/* Decodes one frame into out and returns its flag (byte 1 & 0xf, the classic VAG flags). With
   HEVAG_FLAG_END the samples are 0, as in vgmstream. */
int hevag_decode_frame(HevagState *s, const uint8_t frame[HEVAG_FRAME_BYTES], int16_t out[HEVAG_FRAME_SAMPLES]);
#endif
