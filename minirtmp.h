#pragma once
#define LIBRTMP
#ifdef LIBRTMP
#include "librtmp/rtmp.h"
#endif

#define MINIRTMP_OK    0
#define MINIRTMP_ERROR 1
#define MINIRTMP_MORE_DATA 2

typedef struct MINIRTMP
{
#ifdef LIBRTMP
    RTMP       *rtmp;
    RTMPPacket rtmpPacket;
#endif
    uint8_t  *flv_buf;
    int flv_buf_size, packet_reveived;
} MINIRTMP;

int minirtmp_init(MINIRTMP *r, const char *url, int stream);
int minirtmp_close(MINIRTMP *r);
// nals without sync point, stream headers in avcc format
int minirtmp_write(MINIRTMP *r, uint8_t *data, int size, uint32_t timestamp, int is_video, int keyframe, int stream_hdrs);
int minirtmp_metadata(MINIRTMP *r, int width, int height, int have_audio);
int minirtmp_read(MINIRTMP *r);
