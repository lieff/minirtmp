#pragma once
#include "minirtmp.h"
#include "system.h"

#define MRTMP_OK    0
#define MRTMP_FAIL -1

#define MRTMP_EVENT_OPEN 0 // code=0 - stream successfully opened, fail otherwise
#define MRTMP_EVENT_STOP 1 // code=0 - stream stopped normally, fail otherwise
#define MRTMP_EVENT_LATE 2 // decoding is slow, code is not used

typedef struct MRTMP_Packet
{
    void *data;
    struct MRTMP_Packet *next;
    uint32_t pts;
    int size, type;
} MRTMP_Packet;

typedef void (*MRTMP_PACKET_CALLBACK)(void *user, MRTMP_Packet *pkt);
typedef void (*MRTMP_EVENT_CALLBACK)(void *user, int event, int code);

typedef struct MRTMP_Player
{
    CRITICAL_SECTION reader_lock;
    CRITICAL_SECTION pkt_lock;

    MINIRTMP rtmp;

    MRTMP_PACKET_CALLBACK packet_cb;
    void *packet_user_data;
    MRTMP_EVENT_CALLBACK event_cb;
    void *event_user_data;

    HANDLE thread;
    HANDLE open_thread;
    const char *open_url;

    MRTMP_Packet *packets;
    MRTMP_Packet *tail;

    int stop_flag, stopped_flag, paused_flag, packets_in_buf, packets_eof;
    int width, height;
} MRTMP_Player;

#ifdef __cplusplus
extern "C" {
#endif

void mrtmp_player_init(MRTMP_Player *p);
void mrtmp_set_packet_callback(MRTMP_Player *p, MRTMP_PACKET_CALLBACK cb, void *user_data);
void mrtmp_set_event_callback(MRTMP_Player *p, MRTMP_EVENT_CALLBACK cb, void *user_data);
int mrtmp_open_url(MRTMP_Player *p, const char *url);
int mrtmp_open_url_async(MRTMP_Player *p, const char *url_str);
void mrtmp_close_url(MRTMP_Player *p);
void mrtmp_play(MRTMP_Player *p);
void mrtmp_pause(MRTMP_Player *p);
void mrtmp_stop(MRTMP_Player *p);

#ifdef __cplusplus
}
#endif
