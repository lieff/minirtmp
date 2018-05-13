#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "minirtmp.h"
#include "system.h"

static uint8_t *preload(const char *path, int *data_size)
{
    FILE *file = fopen(path, "rb");
    uint8_t *data;
    *data_size = 0;
    if (!file)
        return 0;
    fseek(file, 0, SEEK_END);
    *data_size = (int)ftell(file);
    fseek(file, 0, SEEK_SET);
    data = (uint8_t*)malloc(*data_size);
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

static const int format_avcc(uint8_t *buf, uint8_t *sps, int sps_size, uint8_t *pps, int pps_size)
{
    uint8_t *orig_buf = buf;
    *buf++ = 0x01;   // version
    *buf++ = sps[1]; // profile
    *buf++ = sps[2]; // compatibility
    *buf++ = sps[3]; // level
    *buf++ = 0xFC | 3; // reserved (6 bits), NULA length size - 1 (2 bits)
    *buf++ = 0xE0 | 1; // reserved (3 bits), num of SPS (5 bits)
    *buf++ = (sps_size >> 8) & 0xFF; // 2 bytes for length of SPS
    *buf++ = sps_size & 0xFF;
    memcpy(buf, sps, sps_size);
    buf += sps_size;

    *buf++ = 1;
    *buf++ = (pps_size >> 8) & 0xFF; // 2 bytes for length of SPS
    *buf++ = pps_size & 0xFF;
    memcpy(buf, pps, pps_size);
    buf += pps_size;
    return buf - orig_buf;
}

int do_receive()
{
    int packet = 0;
    MINIRTMP r;
    minirtmp_init(&r, "rtmp://184.72.239.149/vod/BigBuckBunny_115k.mov", 0);
    while (1)
    {
        if (MINIRTMP_OK == minirtmp_read(&r))
            printf("packet %d\n", packet++);
    }
    return 0;
}

int main(int argc, char **argv)
{
    int h264_size, last_sps_bytes = 0, last_pps_bytes = 0, header_sent = 0, frame = 0, avcc_size;
    unsigned char last_sps[100], last_pps[100], avcc[200];
    uint32_t start_time;
    int stream = 0 != strstr(argv[1], "rtmp://");
    if (!stream)
        return do_receive();
    MINIRTMP r;
    minirtmp_init(&r, argv[1], 1);
    minirtmp_metadata(&r, 240, 160, 0);
    uint8_t *buf_h264 = preload("stream.h264", &h264_size);
    while (h264_size)
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
        if (last_sps_bytes > 1 && last_pps_bytes > 1)
        {
            if (!header_sent)
            {
                avcc_size = format_avcc(avcc, last_sps, last_sps_bytes, last_pps, last_pps_bytes);
                minirtmp_write(&r, avcc, avcc_size, 0, 1, 1, 1);
                header_sent = 1;
                start_time = GetTime()/1000;
            } else
            {
                int is_intra = nal_type == 5;
                uint32_t ts = (uint32_t)(GetTime()/1000) - start_time;
                printf("frame %d, intra=%d, time=%.3f, size=%d\n", frame++, is_intra, ts/1000.0, nal_size);
                minirtmp_write(&r, buf_h264, nal_size, ts, 1, is_intra, 0);
                thread_sleep(40);
            }
        }
        buf_h264 += nal_size;
        h264_size -= nal_size;
    }
}
