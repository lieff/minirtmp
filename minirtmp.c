#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <sys/time.h>
#include "minirtmp.h"

#define GET_FLV_HEADER(have_audio, have_video) \
    char header[] = "FLV\x1\x5\0\0\0\x9\0\0\0\0"; \
    header[4] = (have_audio ? 0x04 : 0x00) | (have_video ? 0x01 : 0x00);

static const int format_flv(uint8_t *buf, uint8_t *data, int size, int type, int64_t pts, int64_t dts, int keyframe, int sequence_hdr)
{
    uint8_t *orig_buf = buf;
    int dataSize = size;
    *buf++ = type;
    dataSize += (9 == type) ? 5 : 2;
    *buf++ = (dataSize >> 16) & 0xFF;
    *buf++ = (dataSize >>  8) & 0xFF;
    *buf++ = (dataSize >>  0) & 0xFF;

    *buf++ = (dts >> 16) & 0xFF;
    *buf++ = (dts >>  8) & 0xFF;
    *buf++ = (dts >>  0) & 0xFF;
    *buf++ = (dts >> 24) & 0xFF;

    *buf++ = 0; // StreamId
    *buf++ = 0;
    *buf++ = 0;
    if (9 == type)
    {   // video
        *buf++ = keyframe ? 0x10 : 0x20 | 0x07; // FrameType + CodecID
        *buf++ = sequence_hdr ? 0x00 : 0x01;

        pts = sequence_hdr ? 0 : (pts - dts);
        *buf++ = (pts >> 16) & 0xFF;
        *buf++ = (pts >>  8) & 0xFF;
        *buf++ = (pts >>  0) & 0xFF;
    } else
    {   //audio
        *buf++ = 0xA0 | 0x0F; // CodecID + SoundFormat
        *buf++ = sequence_hdr ? 0x00 : 0x01;
    }

    memcpy(buf, data, size);
    buf += size;

    dataSize += 11;
    *buf++ = (dataSize >> 24) & 0xFF;
    *buf++ = (dataSize >> 16) & 0xFF;
    *buf++ = (dataSize >>  8) & 0xFF;
    *buf++ = (dataSize >>  0) & 0xFF;
    return buf - orig_buf;
}

int minirtmp_write(MINIRTMP *r, uint8_t *data, int size, uint32_t timestamp, int is_video, int keyframe, int sequence_hdr)
{
    if (!RTMP_IsConnected(r->rtmp) || RTMP_IsTimedout(r->rtmp))
        return MINIRTMP_ERROR;

    uint8_t *buf = malloc(size + 32);
    int flv_size = format_flv(buf, data, size, is_video ? 9 : 8, timestamp, timestamp, keyframe, sequence_hdr);
    RTMP_Write(r->rtmp, buf, flv_size);
    free(buf);

    fd_set sockset; struct timeval timeout = { 0, 0 };
    FD_ZERO(&sockset); FD_SET(RTMP_Socket(r->rtmp), &sockset);
    int result = select(RTMP_Socket(r->rtmp) + 1, &sockset, NULL, NULL, &timeout);
    if (result == 1 && FD_ISSET(RTMP_Socket(r->rtmp), &sockset))
    {
        RTMP_ReadPacket(r->rtmp, &r->rtmpPacket);
        if (!RTMPPacket_IsReady(&r->rtmpPacket))
        {
            RTMP_ClientPacket(r->rtmp, &r->rtmpPacket);
            RTMPPacket_Free(&r->rtmpPacket);
        }
    }
    return MINIRTMP_OK;
}

int minirtmp_init(MINIRTMP *r, const char *url)
{
    memset(r, 0, sizeof(r));
    r->rtmp = RTMP_Alloc();
    RTMP_Init(r->rtmp);
    RTMP_SetupURL(r->rtmp, (char *)url);
    RTMP_EnableWrite(r->rtmp);
    RTMP_Connect(r->rtmp, NULL);
    RTMP_ConnectStream(r->rtmp, 0);
}
