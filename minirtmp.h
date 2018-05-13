#pragma once
#define LIBRTMP
#ifdef LIBRTMP
#include <librtmp/rtmp.h>
#endif

#define MINIRTMP_OK    0
#define MINIRTMP_ERROR 1

typedef struct MINIRTMP
{
#ifdef LIBRTMP
    RTMP       *rtmp;
    RTMPPacket rtmpPacket;
#endif
} MINIRTMP;

int minirtmp_init(MINIRTMP *r, const char *url);
int minirtmp_write(MINIRTMP *r, uint8_t *data, int size, uint32_t timestamp, int is_video, int keyframe, int sequence_hdr);
