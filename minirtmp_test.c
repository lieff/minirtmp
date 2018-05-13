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
    int pos = 4;
    while ((size - pos) > 4)
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

int main(int argc, char **argv)
{
    int h264_size, last_sps_bytes = 0, last_pps_bytes = 0, header_sent = 0, frame = 0;
    unsigned char last_sps[100], last_pps[100], avcc[200];
    MINIRTMP r;
    minirtmp_init(&r, argv[1]);
    uint8_t *buf_h264 = preload("stream.h264", &h264_size);
    while (h264_size)
    {
        int nal_size = get_nal_size(buf_h264, h264_size);
        int nal_type = buf_h264[4] & 31;
        if (nal_size < (signed)sizeof(last_sps) && (nal_type == 7))
            memcpy(last_sps, buf_h264 + 4, last_sps_bytes = nal_size);
        if (nal_size < (signed)sizeof(last_pps) && (nal_type == 8))
            memcpy(last_pps, buf_h264 + 4, last_pps_bytes = nal_size);
        if (last_sps_bytes > 1 && last_pps_bytes > 1)
        {
            if (!header_sent)
            {
                int avcc_size = format_avcc(avcc, last_sps, last_sps_bytes, last_pps, last_pps_bytes);
                minirtmp_write(&r, avcc, avcc_size, 0, 1, 0, 1);
                header_sent = 1;
            } else
            {
                printf("frame %d\n", frame++);
                int is_intra = nal_type == 5;
                minirtmp_write(&r, buf_h264, nal_size, GetTime(), 1, is_intra, 0);
                thread_sleep(40);
            }
        }
        buf_h264 += nal_size;
        h264_size += nal_size;
    }
}
