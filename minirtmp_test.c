#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include "minirtmp.h"
#include "system.h"
#include "minirtmp_player.h"
#define ENABLE_AUDIO 1
#if ENABLE_AUDIO
#include <fdk-aac/aacenc_lib.h>
#include <fdk-aac/aacdecoder_lib.h>
#endif

#define VIDEO_FPS  24
#define AUDIO_RATE 12000
#define DEF_STREAM_FILE "stream.h264"
#define DEF_STREAM_PCM  "stream.pcm"
#define DEF_PLAY_URL    "rtmp://184.72.239.149/vod/BigBuckBunny_115k.mov"

static uint8_t *preload(const char *path, int *data_size)
{
    FILE *file = fopen(path, "rb");
    uint8_t *data;
    *data_size = 0;
    if (!file)
        return 0;
    if (fseek(file, 0, SEEK_END))
        exit(1);
    *data_size = (int)ftell(file);
    if (*data_size < 0)
        exit(1);
    if (fseek(file, 0, SEEK_SET))
        exit(1);
    data = (unsigned char*)malloc(*data_size);
    if (!data)
        exit(1);
    if ((int)fread(data, 1, *data_size, file) != *data_size)
        exit(1);
    fclose(file);
    return data;
}

static int get_nal_size(uint8_t *buf, int size)
{
    int pos = 3;
    while ((size - pos) > 3)
    {
        if (buf[pos] == 0 && buf[pos + 1] == 0 && buf[pos + 2] == 1)
            return pos;
        if (buf[pos] == 0 && buf[pos + 1] == 0 && buf[pos + 2] == 0 && buf[pos + 3] == 1)
            return pos;
        pos++;
    }
    return size;
}

void on_packet(void *user, MRTMP_Packet *pkt)
{
    FILE *f = (FILE *)user;
    static int packet = 0;
    if (pkt->type == RTMP_PACKET_TYPE_VIDEO || pkt->type == RTMP_PACKET_TYPE_AUDIO)
    {
        printf("packet %d %s size=%d\n", packet++, pkt->type == RTMP_PACKET_TYPE_VIDEO ? "video" : "audio", pkt->size);
        if (pkt->type == RTMP_PACKET_TYPE_VIDEO)
        {
            uint8_t *nal = (uint8_t *)pkt->data;
            if (!nal[1])
            {   // stream headers in avcc format
                uint32_t startcode = 0x01000000;
                uint8_t *avcc = nal + 5;
                assert(avcc[0] == 1);
                assert(avcc[4] == 0xFF);
                assert(avcc[5] == 0xE1);
                int sps_size = ((int)avcc[6] << 8) | avcc[7];
                if (f)
                {
                    fwrite(&startcode, 4, 1, f);
                    fwrite(avcc + 8, sps_size, 1, f);
                }
                avcc += 8 + sps_size;
                assert(avcc[0] == 1);
                int pps_size = ((int)avcc[1] << 8) | avcc[2];
                if (f)
                {
                    fwrite(&startcode, 4, 1, f);
                    fwrite(avcc + 3, pps_size, 1, f);
                }
            } else if (pkt->size > 9)
            {
                nal += 5;
                *(uint32_t *)nal = 0x01000000;
                if (f)
                    fwrite(nal, pkt->size - 5, 1, f);
            }
        } else if (pkt->size > 2)
        {
#if ENABLE_AUDIO
            uint8_t *buf = (uint8_t *)pkt->data;
            static HANDLE_AACDECODER dec;
            static FILE *audiof;
            UCHAR *frame = buf + 2;
            UINT frame_size = pkt->size - 2;
            if (!buf[1] && !dec)
            {
                dec = aacDecoder_Open(TT_MP4_RAW, 1);
                if (AAC_DEC_OK != aacDecoder_ConfigRaw(dec, &frame, &frame_size))
                {
                    printf("error: aac config fail\n");
                    exit(1);
                }
                audiof = fopen("audio.raw", "wb");
            } else if (dec)
            {
                UINT valid = frame_size;
                if (AAC_DEC_OK != aacDecoder_Fill(dec, &frame, &frame_size, &valid))
                {
                    printf("error: aac decode fail\n");
                    exit(1);
                }
                INT_PCM pcm[2048*8];
                int err = aacDecoder_DecodeFrame(dec, pcm, sizeof(pcm), 0);
                if (AAC_DEC_OK != err/*err == AAC_DEC_NOT_ENOUGH_BITS*/)
                {
                    printf("error: aac decode fail %d\n", err);
                    exit(1);
                }
                CStreamInfo *info = aacDecoder_GetStreamInfo(dec);
                if (!info)
                {
                    printf("error: aac decode fail\n");
                    exit(1);
                }
                fwrite(pcm, sizeof(INT_PCM)*info->frameSize*info->numChannels, 1, audiof);
            }
#endif
        }
    } else
        printf("packet %d type=%d size=%d\n", packet++, pkt->type, pkt->size);
}

void on_event(void *user, int event, int code)
{
    printf("event=%d code=%d\n", event, code);
}

int do_receive(const char *fname, const char *play_url)
{
    FILE *f = fopen(fname, "wb");
    if (!f)
    {
        printf("error: can't open file\n");
        return 0;
    }
    MINIRTMP r;
    if (minirtmp_init(&r, play_url, 0))
    {
        fclose(f);
        printf("error: can't open RTMP url\n");
        return 0;
    }
    while (1)
    {
        int res = minirtmp_read(&r);
        if (MINIRTMP_EOF == res)
            break;
        if (MINIRTMP_OK == res)
        {
            RTMPPacket *pkt = &r.rtmpPacket;
            MRTMP_Packet p;
            p.data = pkt->m_body;
            p.size = pkt->m_nBodySize;
            p.type = pkt->m_packetType;
            on_packet(f, &p);
        }
    }
    fclose(f);
    minirtmp_close(&r);
    return 0;
}

int do_receive_async(const char *fname, const char *play_url)
{
    MRTMP_Player p;
    mrtmp_player_init(&p);
    mrtmp_set_packet_callback(&p, on_packet, 0);
    mrtmp_set_event_callback(&p, on_event, 0);
    mrtmp_open_url_async(&p, play_url);
    mrtmp_play(&p);
    thread_sleep(10000);
    mrtmp_close_url(&p);
    return 0;
}

int main(int argc, char **argv)
{
    if (argc < 2)
    {
        printf("usage: minirtmp URL/file_name [file_name or URL]\n"
               "  if first param is URL, then streamer mode is used.\n"
               "  if first param is file name, then player mode is used.\n"
               "  streamer mode:\n"
               "    first param  - destination URL to stream\n"
               "    second param - h264 file to stream (%s default).\n"
               "  player mode:\n"
               "    first param  - destination file to save h264\n"
               "    second param - URL to play (%s default).\n", DEF_STREAM_FILE, DEF_PLAY_URL);
        return 0;
    }
#ifdef WIN32
    RTMP_InitWinSock();
#endif
    int h264_size, pcm_size, last_sps_bytes = 0, last_pps_bytes = 0, header_sent = 0, frame = 0, avcc_size;
    unsigned char last_sps[100], last_pps[100], avcc[200];
    uint32_t start_time;
    int stream = 0 != strstr(argv[1], "rtmp://");
    const char *param2 = 0;
    if (argc > 2)
    {
        param2 = argv[2];
    }
    if (!stream)
        return do_receive(argv[1], param2 ? param2 : DEF_PLAY_URL);
    MINIRTMP r;
    if (minirtmp_init(&r, argv[1], 1))
    {
        printf("error: can't open RTMP url %s\n", argv[1]);
        return 0;
    }
    minirtmp_metadata(&r, 240, 160, 0);
    uint8_t *alloc_buf;
    uint8_t *buf_h264 = alloc_buf = preload(param2 ? param2 : DEF_STREAM_FILE, &h264_size);
    if (!buf_h264)
    {
        printf("error: can't open h264 file\n");
        return 0;
    }
#if ENABLE_AUDIO
    int16_t *alloc_pcm;
    int16_t *buf_pcm  = alloc_pcm = (int16_t *)preload(DEF_STREAM_PCM, &pcm_size);
    if (!buf_pcm)
    {
        printf("error: can't open pcm file\n");
        return 0;
    }
    uint32_t sample = 0, total_samples = pcm_size/2, ats = 0;
    HANDLE_AACENCODER aacenc;
    AACENC_InfoStruct info;
    aacEncOpen(&aacenc, 0, 0);
    aacEncoder_SetParam(aacenc, AACENC_TRANSMUX, 0);
    aacEncoder_SetParam(aacenc, AACENC_AFTERBURNER, 1);
    aacEncoder_SetParam(aacenc, AACENC_BITRATE, 64000);
    aacEncoder_SetParam(aacenc, AACENC_SAMPLERATE, AUDIO_RATE);
    aacEncoder_SetParam(aacenc, AACENC_CHANNELMODE, 1);
    aacEncEncode(aacenc, NULL, NULL, NULL, NULL);
    aacEncInfo(aacenc, &info);
    minirtmp_write(&r, info.confBuf, info.confSize, 0, 0, 1, 1);
#endif
    while (h264_size > 0)
    {
        int nal_size = get_nal_size(buf_h264, h264_size);
        int startcode_size = 4;
        if (buf_h264[0] == 0 && buf_h264[1] == 0 && buf_h264[2] == 1)
            startcode_size = 3;
        buf_h264 += startcode_size;
        nal_size -= startcode_size;
        h264_size -= startcode_size;

        int nal_type = buf_h264[0] & 31;
        if (nal_size < (signed)sizeof(last_sps) && (nal_type == 7))
            memcpy(last_sps, buf_h264, last_sps_bytes = nal_size);
        if (nal_size < (signed)sizeof(last_pps) && (nal_type == 8))
            memcpy(last_pps, buf_h264, last_pps_bytes = nal_size);
        if (last_sps_bytes < 1 || last_pps_bytes < 1)
            goto done;
        uint32_t ts = (uint64_t)frame*1000/VIDEO_FPS;
        uint32_t rts = (uint32_t)(GetTime()/1000);
        if (!header_sent)
        {
            avcc_size = minirtmp_format_avcc(avcc, last_sps, last_sps_bytes, last_pps, last_pps_bytes);
            minirtmp_write(&r, avcc, avcc_size, 0, 1, 1, 1);
            header_sent = 1;
            start_time = rts;
        } else if ((nal_type != 7) && (nal_type != 8))
        {
            int is_intra = nal_type == 5;
            rts -= start_time;
            printf("frame %d, intra=%d, time=%.3f, size=%d\n", frame++, is_intra, ts/1000.0, nal_size);
            minirtmp_write(&r, buf_h264, nal_size, ts, 1, is_intra, 0);
#if ENABLE_AUDIO
            while (ats < ts)
            {
                AACENC_BufDesc in_buf, out_buf;
                AACENC_InArgs  in_args;
                AACENC_OutArgs out_args;
                uint8_t buf[2048];
                if (total_samples < 1024)
                {
                    buf_pcm = alloc_pcm;
                    total_samples = pcm_size/2;
                }
                in_args.numInSamples = 1024;
                void *in_ptr = buf_pcm, *out_ptr = buf;
                int in_size          = 2*in_args.numInSamples;
                int in_element_size  = 2;
                int in_identifier    = IN_AUDIO_DATA;
                int out_size         = sizeof(buf);
                int out_identifier   = OUT_BITSTREAM_DATA;
                int out_element_size = 1;

                in_buf.numBufs            = 1;
                in_buf.bufs               = &in_ptr;
                in_buf.bufferIdentifiers  = &in_identifier;
                in_buf.bufSizes           = &in_size;
                in_buf.bufElSizes         = &in_element_size;
                out_buf.numBufs           = 1;
                out_buf.bufs              = &out_ptr;
                out_buf.bufferIdentifiers = &out_identifier;
                out_buf.bufSizes          = &out_size;
                out_buf.bufElSizes        = &out_element_size;

                if (AACENC_OK != aacEncEncode(aacenc, &in_buf, &out_buf, &in_args, &out_args))
                {
                    printf("error: aac encode fail\n");
                    exit(1);
                }
                sample  += in_args.numInSamples;
                buf_pcm += in_args.numInSamples;
                total_samples -= in_args.numInSamples;
                ats = (uint64_t)sample*1000/AUDIO_RATE;

                minirtmp_write(&r, buf, out_args.numOutBytes, ats, 0, 1, 0);
            }
#endif
            if (ts > rts)
                thread_sleep(ts - rts);
        }
done:
        buf_h264 += nal_size;
        h264_size -= nal_size;
    }
    if (alloc_buf)
        free(alloc_buf);
#if ENABLE_AUDIO
    if (alloc_pcm)
        free(alloc_pcm);
#endif
    minirtmp_close(&r);
}
