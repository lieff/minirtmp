#include "minirtmp_player.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#ifdef _DEBUG
#define dbglog(...) fprintf(stderr, __VA_ARGS__);
#else
#define dbglog(...)
#endif

void mrtmp_set_packet_callback(MRTMP_Player *p, MRTMP_PACKET_CALLBACK cb, void *user_data)
{
    p->packet_cb = cb;
    p->packet_user_data = user_data;
}

void mrtmp_set_event_callback(MRTMP_Player *p, MRTMP_EVENT_CALLBACK cb, void *user_data)
{
    p->event_cb = cb;
    p->event_user_data = user_data;
}

void mrtmp_player_init(MRTMP_Player *p)
{
    memset(p, 0, sizeof(*p));
}

void mrtmp_close_url(MRTMP_Player *p)
{
    mrtmp_stop(p);
    if (p->open_url)
        free((void*)p->open_url);
    if (p->rtmp.rtmp)
        minirtmp_close(&p->rtmp);
    mrtmp_player_init(p);
}

int mrtmp_open_url(MRTMP_Player *p, const char *url)
{
    if (minirtmp_init(&p->rtmp, url, 0))
    {
        dbglog("error: can't open RTMP url\n");
        goto error;
    }
    return 0;
error:
    mrtmp_close_url(p);
    return MRTMP_FAIL;
}

static THREAD_RET THRAPI mrtmp_open_thread(void *lpThreadParameter)
{
    MRTMP_Player *p = (MRTMP_Player *)lpThreadParameter;
    thread_name("mrtmp_open");
    int ret = mrtmp_open_url(p, p->open_url);
    if (p->event_cb)
        p->event_cb(p->event_user_data, MRTMP_EVENT_OPEN, ret);
    return 0;
}

int mrtmp_open_url_async(MRTMP_Player *p, const char *url_str)
{
    p->open_url = strdup(url_str);
    if (!p->open_url)
        return MRTMP_FAIL;
    p->open_thread = thread_create(mrtmp_open_thread, p);
    if (!p->open_thread)
    {
        free((void*)p->open_url);
        p->open_url = 0;
        return MRTMP_FAIL;
    }
    return MRTMP_OK;
}

static THREAD_RET THRAPI mrtmp_reader_thread(void *lpThreadParameter)
{
    MRTMP_Player *p = (MRTMP_Player *)lpThreadParameter;
    thread_name("mrtmp_jb");
    while (!p->stop_flag)
    {
        EnterCriticalSection(&p->reader_lock);
        int ret = minirtmp_read(&p->rtmp);
        EnterCriticalSection(&p->pkt_lock);
        if (MINIRTMP_OK == ret)
        {
            RTMPPacket *rpkt = &p->rtmp.rtmpPacket;
            MRTMP_Packet *pkt = calloc(1, sizeof(MRTMP_Packet));
            pkt->data = rpkt->m_body;
            pkt->size = rpkt->m_nBodySize;
            pkt->type = rpkt->m_packetType;
            pkt->pts  = rpkt->m_nTimeStamp;
            if (p->packets)
            {
                p->tail->next = pkt;
            } else
                p->packets = pkt;
            p->tail = pkt;
            p->packets_in_buf++;
            if (rpkt->m_body)
            {
                rpkt->m_body = NULL;
                rpkt->m_nBodySize  = 0;
                rpkt->m_nBytesRead = 0;
            }
        } else
        if (MINIRTMP_EOF == ret)
            p->packets_eof = 1;
        LeaveCriticalSection(&p->pkt_lock);
        LeaveCriticalSection(&p->reader_lock);
        if (p->packets_eof)
            break;
        while (!p->stop_flag && p->packets_in_buf > 200)
            thread_sleep(100);
    }
    return 0;
}

static MRTMP_Packet *read_packet(MRTMP_Player *p)
{
    while (!p->stop_flag && !p->packets && !p->packets_eof)
        thread_sleep(100);
    if (p->stop_flag || (p->packets_eof && !p->packets))
        return 0;
    EnterCriticalSection(&p->pkt_lock);
    if (p->packets_in_buf > 0)
        p->packets_in_buf--;
    MRTMP_Packet *pkt = p->packets;
    if (p->packets)
        p->packets = p->packets->next;
    LeaveCriticalSection(&p->pkt_lock);
    return pkt;
}

static THREAD_RET THRAPI mrtmp_player_thread(void *lpThreadParameter)
{
    MRTMP_Player *p = (MRTMP_Player *)lpThreadParameter;
    int ret = 0;
    HANDLE reader_thread = NULL;
    thread_name("mrtmp_player");
    if (p->open_thread)
    {
        thread_wait(p->open_thread);
        thread_close(p->open_thread);
        p->open_thread = 0;
    }
    ret = (!p->rtmp.rtmp) ? MRTMP_FAIL : MRTMP_OK;
    if (MRTMP_OK != ret)
        goto exit;
    p->packets_in_buf = 0;
    InitializeCriticalSection(&p->reader_lock);
    InitializeCriticalSection(&p->pkt_lock);
    reader_thread = thread_create(mrtmp_reader_thread, p);
    if (!reader_thread)
    {
        ret = MRTMP_FAIL;
        goto exit_cleanup;
    }

    MRTMP_Packet *pkt;
    while (!p->stop_flag && (pkt = read_packet(p)))
    {
        p->packet_cb(p->packet_user_data, pkt);
        if (pkt->data)
            free(pkt->data - RTMP_MAX_HEADER_SIZE);
        free(pkt);
        if (p->paused_flag)
        {
            EnterCriticalSection(&p->reader_lock);
            while (!p->stop_flag && p->paused_flag)
                thread_sleep(100);
            LeaveCriticalSection(&p->reader_lock);
        }
    }
    if (reader_thread)
    {
        p->stop_flag = 1;
        thread_wait(reader_thread);
        thread_close(reader_thread);
    }
exit_cleanup:
    DeleteCriticalSection(&p->reader_lock);
    DeleteCriticalSection(&p->pkt_lock);
exit:
    if (p->event_cb)
        p->event_cb(p->event_user_data, MRTMP_EVENT_STOP, ret);
    while (p->packets)
    {
        if (p->packets->data)
            free(p->packets->data - RTMP_MAX_HEADER_SIZE);
        pkt = p->packets->next;
        free(p->packets);
        p->packets = pkt;
    }
    p->stopped_flag = 1;
    return 0;
}

void mrtmp_play(MRTMP_Player *p)
{
    p->paused_flag = 0;
    if (p->thread)
        return;
    p->stopped_flag = 0;
    p->thread = thread_create(mrtmp_player_thread, p);
    if (!p->thread)
        p->stopped_flag = 1;
}

void mrtmp_pause(MRTMP_Player *p)
{
    p->paused_flag = 1;
}

void mrtmp_stop(MRTMP_Player *p)
{
    if (!p->thread)
    {
        if (p->open_thread)
        {
            thread_wait(p->open_thread);
            thread_close(p->open_thread);
            p->open_thread = 0;
        }
        return;
    }
    p->paused_flag = 0;
    p->stop_flag = 1;
    thread_wait(p->thread);
    thread_close(p->thread);
    p->thread = 0;
    p->stop_flag = 0;
}
