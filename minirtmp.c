#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <sys/time.h>
#include <assert.h>
#include "minirtmp.h"

#define GET_FLV_HEADER(have_audio, have_video) \
    char header[] = "FLV\x1\x5\0\0\0\x9\0\0\0\0"; \
    header[4] = (have_audio ? 0x04 : 0x00) | (have_video ? 0x01 : 0x00);

static const int format_flv(uint8_t *buf, uint8_t *data, int size, int type, int64_t pts, int64_t dts, int keyframe, int stream_hdrs)
{
    uint8_t *orig_buf = buf;
    int dataSize = size;
    *buf++ = type;
    dataSize += (9 == type) ? 5 : 2;
    dataSize += (9 == type && !stream_hdrs) ? 4 : 0;
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
        *buf++ = (keyframe ? 0x10 : 0x20) | 0x07; // FrameType + CodecID
        *buf++ = stream_hdrs ? 0x00 : 0x01;

        pts = stream_hdrs ? 0 : (pts - dts);
        *buf++ = (pts >> 16) & 0xFF;
        *buf++ = (pts >>  8) & 0xFF;
        *buf++ = (pts >>  0) & 0xFF;
        if (!stream_hdrs)
        {
            *buf++ = (size >> 24) & 0xFF;
            *buf++ = (size >> 16) & 0xFF;
            *buf++ = (size >> 8) & 0xFF;
            *buf++ = size & 0xFF;
        }
    } else
    {   //audio
        *buf++ = 0xA0 | 0x0F; // CodecID + SoundFormat
        *buf++ = stream_hdrs ? 0x00 : 0x01;
    }

    memcpy(buf, data, size);
    buf += size;

    dataSize += 11;
    *buf++ = (dataSize >> 24) & 0xFF;
    *buf++ = (dataSize >> 16) & 0xFF;
    *buf++ = (dataSize >>  8) & 0xFF;
    *buf++ = (dataSize >>  0) & 0xFF;
    assert((buf - orig_buf) == (dataSize + 4));
    return buf - orig_buf;
}

int minirtmp_write(MINIRTMP *r, uint8_t *data, int size, uint32_t timestamp, int is_video, int keyframe, int stream_hdrs)
{
    if (!RTMP_IsConnected(r->rtmp) || RTMP_IsTimedout(r->rtmp))
        return MINIRTMP_ERROR;

    if (r->flv_buf_size < (size + 32))
    {
        r->flv_buf_size = size + 32;
        if (r->flv_buf)
            free(r->flv_buf);
        r->flv_buf = malloc(r->flv_buf_size);
    }

    int flv_size = format_flv(r->flv_buf, data, size, is_video ? 9 : 8, timestamp, timestamp, keyframe, stream_hdrs);
    RTMP_Write(r->rtmp, (const char *)r->flv_buf, flv_size);

    fd_set sockset; struct timeval timeout = { 0, 0 };
    FD_ZERO(&sockset); FD_SET(RTMP_Socket(r->rtmp), &sockset);
    int result = select(RTMP_Socket(r->rtmp) + 1, &sockset, NULL, NULL, &timeout);
    if (result == 1 && FD_ISSET(RTMP_Socket(r->rtmp), &sockset))
    {
        RTMP_ReadPacket(r->rtmp, &r->rtmpPacket);
        if (RTMPPacket_IsReady(&r->rtmpPacket))
        {
            RTMP_ClientPacket(r->rtmp, &r->rtmpPacket);
            RTMPPacket_Free(&r->rtmpPacket);
        }
    }
    return MINIRTMP_OK;
}

int minirtmp_read(MINIRTMP *r)
{
    if (r->packet_reveived)
    {
        r->packet_reveived = 0;
        RTMPPacket_Free(&r->rtmpPacket);
    }
    RTMP_ReadPacket(r->rtmp, &r->rtmpPacket);
    if (RTMPPacket_IsReady(&r->rtmpPacket))
    {
        r->packet_reveived = 1;
        RTMP_ClientPacket(r->rtmp, &r->rtmpPacket);
    }
    return r->packet_reveived ? MINIRTMP_OK : MINIRTMP_MORE_DATA;
}

int minirtmp_init(MINIRTMP *r, const char *url, int stream)
{
    memset(r, 0, sizeof(*r));
    r->rtmp = RTMP_Alloc();
    RTMP_Init(r->rtmp);
    RTMP_SetupURL(r->rtmp, (char *)url);
    if (stream)
        RTMP_EnableWrite(r->rtmp);
    RTMP_Connect(r->rtmp, NULL);
    RTMP_ConnectStream(r->rtmp, 0);
    return MINIRTMP_OK;
}

void minirtmp_close(MINIRTMP *r)
{
    if (r->flv_buf)
        free(r->flv_buf);
    if (r->rtmp)
        RTMP_Free(r->rtmp);
    RTMPPacket_Free(&r->rtmpPacket);
}

int minirtmp_metadata(MINIRTMP *r, int width, int height, int have_audio)
{
    char buf[512];
    char *pbuf = buf;
    char *end = buf + sizeof(buf);

    *pbuf++ = 0x12; // tagtype scripte
    *pbuf++ = 0; // len
    *pbuf++ = 0;
    *pbuf++ = 0;
    *pbuf++ = 0; // time stamp
    *pbuf++ = 0;
    *pbuf++ = 0;
    *pbuf++ = 0;
    *pbuf++ = 0x00; // stream id 0
    *pbuf++ = 0x00;
    *pbuf++ = 0x00;

static const AVal g_duration = AVC("duration");
static const AVal g_onMetaData = AVC("onMetaData");
static const AVal g_width = AVC("width");
static const AVal g_height = AVC("height");
static const AVal g_videocodecid = AVC("videocodecid");
static const AVal g_audiocodecid = AVC("audiocodecid");
    pbuf = AMF_EncodeString(pbuf, end, &g_onMetaData);
    *pbuf++ = AMF_ECMA_ARRAY;

    pbuf = AMF_EncodeInt32(pbuf, end, 5);
    pbuf = AMF_EncodeNamedNumber(pbuf, end, &g_duration, 0.0);
    if (width > 0 && height > 0)
    {
        pbuf = AMF_EncodeNamedNumber(pbuf, end, &g_width, width);
        pbuf = AMF_EncodeNamedNumber(pbuf, end, &g_height, height);
        pbuf = AMF_EncodeNamedNumber(pbuf, end, &g_videocodecid, 7);
    }
    if (have_audio)
        pbuf = AMF_EncodeNamedNumber(pbuf, end, &g_audiocodecid, 10);
    pbuf = AMF_EncodeInt24(pbuf, end, AMF_OBJECT_END);

    int size = pbuf - buf - 11;

    buf[1] = (uint8_t) (size >> 16);
    buf[2] = (uint8_t) (size >> 8);
    buf[3] = (uint8_t) size;
    size += 11;

    *pbuf++ = (size >> 24) & 0xFF;
    *pbuf++ = (size >> 16) & 0xFF;
    *pbuf++ = (size >>  8) & 0xFF;
    *pbuf++ = (size >>  0) & 0xFF;
    assert((pbuf - buf) == (size + 4));

    return RTMP_Write(r->rtmp, buf, pbuf - buf);
}
