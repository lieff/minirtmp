#include "system.h"

#ifndef _WIN32

#include <stdlib.h>
#include <time.h>
#include <errno.h>
#include <unistd.h>
#if defined(__linux) || defined(__linux__)
#include <sys/prctl.h>
#endif

#define nullptr 0

typedef struct Event Event;

typedef struct Event
{
    Event * volatile pMultipleCond;
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    volatile bool signaled;
    bool manual_reset;
} Event;

static bool InitEvent(Event *e)
{
#if (defined(ANDROID) && !defined(__LP64__)) || defined(__APPLE__)
    if (pthread_cond_init(&e->cond, NULL))
        return FALSE;
#else
    pthread_condattr_t attr;
    if (pthread_condattr_init(&attr))
        return FALSE;
    if (pthread_condattr_setclock(&attr, CLOCK_MONOTONIC))
    {
        pthread_condattr_destroy(&attr);
        return FALSE;
    }
    if (pthread_cond_init(&e->cond, &attr))
    {
        pthread_condattr_destroy(&attr);
        return FALSE;
    }
    pthread_condattr_destroy(&attr);
#endif
    if (pthread_mutex_init(&e->mutex, NULL))
    {
        pthread_cond_destroy(&e->cond);
        return FALSE;
    }
    e->pMultipleCond = NULL;
    return TRUE;
}

#ifdef __APPLE__
#include <mach/mach_time.h>
static inline uint64_t GetAbsTimeInNanoseconds()
{
    static mach_timebase_info_data_t g_timebase_info;
    if (g_timebase_info.denom == 0)
        mach_timebase_info(&g_timebase_info);
    return mach_absolute_time()*g_timebase_info.numer/g_timebase_info.denom;
}
#endif

static inline void GetAbsTime(struct timespec *ts, uint32_t timeout)
{
#if defined(__APPLE__)
    uint64_t cur_time = GetAbsTimeInNanoseconds();
    ts->tv_sec  = cur_time/1000000000u + timeout/1000u;
    ts->tv_nsec = (cur_time % 1000000000u) + (timeout % 1000u)*1000000u;
#else
    clock_gettime(CLOCK_MONOTONIC, ts);
    ts->tv_sec  += timeout/1000u;
    ts->tv_nsec += (timeout % 1000u)*1000000u;
#endif
    if (ts->tv_nsec >= 1000000000)
    {
        ts->tv_nsec -= 1000000000;
        ts->tv_sec++;
    }
}

static inline int CondTimedWait(pthread_cond_t *cond, pthread_mutex_t *mutex, const struct timespec *abstime)
{
#if defined(ANDROID) && !defined(__LP64__)
    return pthread_cond_timedwait_monotonic_np(cond, mutex, abstime);
#elif defined(__APPLE__)
    struct timespec reltime;
    uint64_t cur_time = GetAbsTimeInNanoseconds();
    reltime.tv_sec  = abstime->tv_sec  - cur_time/1000000000u;
    reltime.tv_nsec = abstime->tv_nsec - (cur_time % 1000000000u);
    if (reltime.tv_nsec < 0)
    {
        reltime.tv_nsec += 1000000000;
        reltime.tv_sec--;
    }
    if ((reltime.tv_sec < 0) || ((reltime.tv_sec == 0) && (reltime.tv_nsec == 0)))
        return ETIMEDOUT;
    return pthread_cond_timedwait_relative_np(cond, mutex, &reltime);
#else
    return pthread_cond_timedwait(cond, mutex, abstime);
#endif
}

static bool WaitForEvent(Event *e, uint32_t timeout, bool *signaled)
{
    if (pthread_mutex_lock(&e->mutex))
        return FALSE;

    if (timeout == INFINITE)
    {
        while (!e->signaled)
            pthread_cond_wait(&e->cond, &e->mutex);
    } else if (timeout != 0)
    {
        struct timespec t;
        GetAbsTime(&t, timeout);
        while (!e->signaled)
        {
            if (CondTimedWait(&e->cond, &e->mutex, &t))
                break;
        }
    }
    *signaled = e->signaled;
    if (!e->manual_reset)
        e->signaled = FALSE;

    if (pthread_mutex_unlock(&e->mutex))
        return FALSE;
    return TRUE;
}

static bool WaitForMultipleEvents(Event **e, uint32_t count, uint32_t timeout, bool waitAll, int *signaled_num)
{
    uint32_t i;
#define PTHR(func, num) for (i = num; i < count; i++)\
        if (func(&e[i]->mutex))\
            return FALSE;
    PTHR(pthread_mutex_lock, 0);

    int sig_num = -1;
    if (timeout == 0)
    {
#define CHECK_SIGNALED \
        if (waitAll)\
        {\
            for (i = 0; i < count; i++)\
                if (!e[i]->signaled)\
                    break;\
            if (i == count)\
                for (i = 0; i < count; i++)\
                {\
                    if (sig_num < 0 && e[i]->signaled)\
                        sig_num = (int)i;\
                    if (!e[i]->manual_reset)\
                        e[i]->signaled = FALSE;\
                }\
        } else\
        {\
            for (i = 0; i < count; i++)\
                if (e[i]->signaled)\
                {\
                    sig_num = (int)i;\
                    if (!e[i]->manual_reset)\
                        e[i]->signaled = FALSE;\
                    break;\
                }\
        }
        CHECK_SIGNALED;
    } else
    if (timeout == INFINITE)
    {
#define SET_MULTIPLE(val) for (i = 1; i < count; i++)\
            e[i]->pMultipleCond = val;
        SET_MULTIPLE(e[0]);
        for (;;)
        {
            CHECK_SIGNALED;
            if (sig_num >= 0)
                break;
            PTHR(pthread_mutex_unlock, 1);
            pthread_cond_wait(&e[0]->cond, &e[0]->mutex);
            PTHR(pthread_mutex_lock, 1);
        }
        SET_MULTIPLE(0);
    } else
    {
        SET_MULTIPLE(e[0]);
        struct timespec t;
        GetAbsTime(&t, timeout);
        for (;;)
        {
            CHECK_SIGNALED;
            if (sig_num >= 0)
                break;
            PTHR(pthread_mutex_unlock, 1);
            int res = CondTimedWait(&e[0]->cond, &e[0]->mutex, &t);
            PTHR(pthread_mutex_lock, 1);
            if (res)
                break;
        }
        SET_MULTIPLE(0);
    }
    PTHR(pthread_mutex_unlock, 0);
    *signaled_num = sig_num;
    return TRUE;
}

HANDLE event_create(bool manualReset, bool initialState)
{
    Event *e = (Event *)malloc(sizeof(*e));
    if (!e)
        return NULL;
    if (!InitEvent(e))
    {
        free(e);
        return NULL;
    }
    e->manual_reset = manualReset;
    e->signaled     = initialState;
    return (HANDLE)e;
}

bool event_destroy(HANDLE event)
{
    Event *e = (Event *)event;
    if (!e)
        return FALSE;
    if (pthread_cond_destroy(&e->cond))
        return FALSE;
    if (pthread_mutex_destroy(&e->mutex))
        return FALSE;
    free((void *)e);
    return TRUE;
}

bool event_set(HANDLE event)
{
    Event *e = (Event *)event;
    if (pthread_mutex_lock(&e->mutex))
        return FALSE;

    Event *pMultipleCond = e->pMultipleCond;
    e->signaled = TRUE;
    if (pthread_cond_signal(&e->cond))
        return FALSE;

    if (pthread_mutex_unlock(&e->mutex))
        return FALSE;

    if (pMultipleCond && pMultipleCond != e)
    {
        if (pthread_mutex_lock(&pMultipleCond->mutex))
            return FALSE;
        if (pthread_cond_signal(&pMultipleCond->cond))
            return FALSE;
        if (pthread_mutex_unlock(&pMultipleCond->mutex))
            return FALSE;
    }
    return TRUE;
}

bool event_reset(HANDLE event)
{
    Event *e = (Event *)event;
    if (pthread_mutex_lock(&e->mutex))
        return FALSE;
    e->signaled = FALSE;
    if (pthread_mutex_unlock(&e->mutex))
        return FALSE;
    return TRUE;
}

int event_wait(HANDLE event, uint32_t milliseconds)
{
    bool signaled;
    if (!WaitForEvent((Event *)event, milliseconds, &signaled))
        return WAIT_FAILED;
    return signaled ? WAIT_OBJECT : WAIT_TIMEOUT;
}

int event_wait_multiple(uint32_t count, const HANDLE *events, bool waitAll, uint32_t milliseconds)
{
    if (count == 1)
        return event_wait(events[0], milliseconds);
    int signaled_num = -1;
    if (!WaitForMultipleEvents((Event **)events, count, milliseconds, waitAll, &signaled_num))
        return WAIT_FAILED;
    return (signaled_num < 0) ? WAIT_TIMEOUT : (WAIT_OBJECT_0 + signaled_num);
}

bool InitializeCriticalSection(LPCRITICAL_SECTION lpCriticalSection)
{
    pthread_mutexattr_t ma;
    if (pthread_mutexattr_init(&ma))
        return FALSE;
    if (pthread_mutexattr_settype(&ma, PTHREAD_MUTEX_RECURSIVE))
    {
        pthread_mutexattr_destroy(&ma);
        return FALSE;
    }
    if (pthread_mutex_init((pthread_mutex_t *)lpCriticalSection, &ma))
    {
        pthread_mutexattr_destroy(&ma);
        return FALSE;
    }
    if (pthread_mutexattr_destroy(&ma))
        return FALSE;
    return TRUE;
}

bool DeleteCriticalSection(LPCRITICAL_SECTION lpCriticalSection)
{
    if (pthread_mutex_destroy((pthread_mutex_t *)lpCriticalSection))
        return FALSE;
    return TRUE;
}

bool EnterCriticalSection(LPCRITICAL_SECTION lpCriticalSection)
{
    if (pthread_mutex_lock((pthread_mutex_t *)lpCriticalSection))
        return FALSE;
    return TRUE;
}

bool LeaveCriticalSection(LPCRITICAL_SECTION lpCriticalSection) 
{
    if (pthread_mutex_unlock((pthread_mutex_t *)lpCriticalSection))
        return FALSE;
    return TRUE;
}

HANDLE thread_create(LPTHREAD_START_ROUTINE lpStartAddress, void *lpParameter)
{
    pthread_t *t = (pthread_t *)malloc(sizeof(*t));
    if (!t)
        return nullptr;
    if (pthread_create(t, 0, lpStartAddress, lpParameter))
    {
        free(t);
        return nullptr;
    }
    //if (lpThreadId)
    //    *lpThreadId = (uint32_t)*t;
    return (HANDLE)t;
}

bool thread_close(HANDLE thread)
{
    if (!thread)
        return FALSE;
    pthread_t *t = (pthread_t *)thread;
    if (*t)
        pthread_detach(*t);
    free(t);
    return TRUE;
}

void *thread_wait(HANDLE thread)
{
    if (!thread)
        return (void*)-1;
    void *ret = 0;
    pthread_t *t = (pthread_t *)thread;
    if (!*t)
        return ret;
    int res = pthread_join(*t, &ret);
    if (res)
        return (void*)-1;
#if 0
    if (timeout == 0)
    {
        int res = pthread_tryjoin_np(*t, &ret);
        if (res)
            return FALSE;
    } else
    if (timeout == INFINITE)
    {
        int res = pthread_join(*t, &ret);
        if (res)
            return FALSE;
    } else
    {
        struct timespec ts;
        GetAbsTime(&ts, timeout);
        int res = pthread_timedjoin_np(*t, &ret, &ts);
        if (res)
            return FALSE;
    }
#endif
    *t = 0; // thread joined - no need to detach
    return ret;
}

#else  //_WIN32

HANDLE thread_create(LPTHREAD_START_ROUTINE lpStartAddress, void *lpParameter)
{
    DWORD tid;
    return CreateThread(0, 0, lpStartAddress, lpParameter, 0, &tid);
}

HANDLE event_create(bool manualReset, bool initialState)
{
    return CreateEvent(0, manualReset, initialState, 0);
}

bool event_destroy(HANDLE event)
{
    CloseHandle(event);
    return TRUE;
}

bool thread_close(HANDLE thread)
{
    CloseHandle(thread);
    return TRUE;
}

void *thread_wait(HANDLE thread)
{
    if (WaitForSingleObject(thread, INFINITE) == WAIT_OBJECT_0)
    {
        DWORD ExitCode;
        GetExitCodeThread(thread, &ExitCode);
        return (void *)(intptr_t)ExitCode;
    }
    return (void *)(intptr_t)-1;
}

#endif //_WIN32

bool thread_name(const char *name)
{
#ifdef _WIN32
#ifdef _MSC_VER
    struct tagTHREADNAME_INFO
    {
        DWORD dwType;
        LPCSTR szName;
        DWORD dwThreadID;
        DWORD dwFlags;
    } info = { 0x1000, name, (DWORD)-1, 0 };
    __try
    {
        RaiseException(0x406D1388, 0, sizeof(info)/sizeof(ULONG_PTR), (ULONG_PTR*)&info);
    }
    __except(EXCEPTION_EXECUTE_HANDLER)
    {
    }
#endif
    return TRUE;
#elif defined(__linux) || defined(__linux__)
    return (0 == prctl(PR_SET_NAME, name, 0, 0, 0));
    //return (0 == pthread_setname_np(pthread_self(), name));
#else // macos, ios
    return (0 == pthread_setname_np(name));
#endif
}

void thread_sleep(uint32_t milliseconds)
{
#ifdef _WIN32
    Sleep(milliseconds);
#else
    usleep((useconds_t)milliseconds*1000);
#endif
}

uint64_t GetTime()
{
    uint64_t time;
#ifdef _WIN32
    GetSystemTimeAsFileTime((FILETIME*)&time);
    time = time/10 - 11644473600000000;
#elif defined(__APPLE__)
    time = GetAbsTimeInNanoseconds() / 1000u;
#else
    struct timespec ts;
    // CLOCK_PROCESS_CPUTIME_ID CLOCK_THREAD_CPUTIME_ID
    clock_gettime(CLOCK_MONOTONIC, &ts);
    time = (uint64_t)ts.tv_sec * 1000000u + ts.tv_nsec / 1000u;
#endif
    return time;
}
