#ifdef WIN32
#define _CRT_SECURE_NO_WARNINGS
#endif
#include "dmtimermodule.h"

CDMTimerModule::CDMTimerModule()
{
    Init();
}

CDMTimerModule::~CDMTimerModule()
{
    UnInit();
}

void CDMTimerModule::AddTimerElement(CDMTimerElement* pElement)
{
    unsigned long long qwExpires = pElement->m_qwNextTime;

    unsigned long long idx = static_cast<unsigned long long>(qwExpires - m_qwLastTime);
    struct list_head* vec = NULL;

    if (idx < TVR_SIZE)
    {
        int i = qwExpires & TVR_MASK;
        vec = m_tv1.vec + i;
    }
    else if (idx < (1 << (TVR_BITS + TVN_BITS)))
    {
        int i = (qwExpires >> TVR_BITS) & TVN_MASK;
        vec = m_tv2.vec + i;
    }
    else if (idx < (1 << (TVR_BITS + 2 * TVN_BITS)))
    {
        int i = (qwExpires >> (TVR_BITS + TVN_BITS)) & TVN_MASK;
        vec = m_tv3.vec + i;
    }
    else if (idx < (1 << (TVR_BITS + 3 * TVN_BITS)))
    {
        int i = (qwExpires >> (TVR_BITS + 2 * TVN_BITS)) & TVN_MASK;
        vec = m_tv4.vec + i;
    }
    else if ((signed long long)idx < 0)
    {
        vec = m_tv1.vec + (m_qwLastTime & TVR_MASK);
    }
    else
    {
        int i;
        if (idx > 0xffffffffUL)
        {
            idx = 0xffffffffUL;
            qwExpires = idx + m_qwLastTime;
        }

        i = (qwExpires >> (TVR_BITS + 3 * TVN_BITS)) & TVN_MASK;

        vec = m_tv5.vec + i;
    }

    list_add_tail(&pElement->m_stEntry, vec);
}

void CDMTimerModule::__ReleaseElement(struct list_head *head)
{
    CDMTimerElement* timer = NULL;

    struct list_head work_list;
    struct list_head *temp = &work_list;

    list_replace_init(head, &work_list);

    while (!list_empty(temp))
    {
        timer = __GetTimerInfoByEntry(temp->next);
        RemoveTimerElement(timer);
        ReleaseElement(timer);
    }
}

int CDMTimerModule::__Cascade( TVec* tv, int idx )
{
    CDMTimerElement* pTimer = NULL;
    CDMTimerElement* pTemp = NULL;

    struct list_head tv_list;

    list_replace_init(tv->vec + idx, &tv_list);

    for (pTimer = __GetTimerInfoByEntry((&tv_list)->next),
                pTemp = __GetTimerInfoByEntry(pTimer->m_stEntry.next);
                &pTimer->m_stEntry != (&tv_list);
                pTimer = pTemp,
                pTemp = __GetTimerInfoByEntry(pTemp->m_stEntry.next))
    {
        AddTimerElement(pTimer);
    }

    return idx;
}

bool CDMTimerModule::__TimerPending(CDMTimerElement* pElement)
{
    return NULL != pElement->m_stEntry.next;
}

void CDMTimerModule::RemoveTimerElement( CDMTimerElement* pElement)
{
    if (__TimerPending(pElement))
    {
        list_del(&pElement->m_stEntry);
        pElement->m_stEntry.next = NULL;
        pElement->m_stEntry.prev = NULL;
        return;
    }

    DMASSERT(0);
}

CDMTimerElement* CDMTimerModule::__GetTimerInfoByEntry(struct list_head* head )
{
    const struct list_head* ptr = head;
    return (CDMTimerElement*)((char*)ptr - offsetof(CDMTimerElement, m_stEntry));
}

void CDMTimerModule::Init()
{
    for (int j = 0; j < TVR_SIZE; ++j)
    {
        INIT_LIST_HEAD(m_tv1.vec + j);
    }

    for (int i = 0; i < TVN_SIZE; ++i)
    {
        INIT_LIST_HEAD(m_tv2.vec + i);
        INIT_LIST_HEAD(m_tv3.vec + i);
        INIT_LIST_HEAD(m_tv4.vec + i);
        INIT_LIST_HEAD(m_tv5.vec + i);
    }

    m_dwTickTime = GetTickCount32();
    m_qwTickCount =  0;

    m_qwLastTime = GetBootTime();
    m_qwCurTime =  GetBootTime();
}

void CDMTimerModule::UnInit()
{
    for (int j=0; j < TVN_SIZE; ++j)
    {
        __ReleaseElement(m_tv5.vec + j);
        __ReleaseElement(m_tv4.vec + j);
        __ReleaseElement(m_tv3.vec + j);
        __ReleaseElement(m_tv2.vec + j);
    }

    for (int i=0; i < TVR_SIZE; ++i)
    {
        __ReleaseElement(m_tv1.vec + i);
    }
}

int CDMTimerModule::Run()
{
    int nEvents = 0;

    m_qwCurTime =  GetBootTime();

    CDMTimerElement* timer = NULL;

    while (TIME_NOT_EQ(m_qwCurTime, m_qwLastTime))
    {
        struct list_head work_list;
        struct list_head *temp = &work_list;

        int index = m_qwLastTime & TVR_MASK;

        if (!index
                    && (!__Cascade(&m_tv2, INDEX(m_qwLastTime,0)))
                    && (!__Cascade(&m_tv3, INDEX(m_qwLastTime,1)))
                    && (!__Cascade(&m_tv4, INDEX(m_qwLastTime,2))))
        {
            __Cascade(&m_tv5, INDEX(m_qwLastTime,3));
        }

        list_replace_init(m_tv1.vec+index, &work_list);

        while (!list_empty(temp))
        {
            timer = __GetTimerInfoByEntry(temp->next);

            RemoveTimerElement(timer);

            if (timer->m_bErased)
            {
                ReleaseElement(timer);
                continue;
            }

            timer->m_poTimerSink->OnTimer(timer->m_qwID, timer->m_oAny);
            ++nEvents;

            if (timer->m_bErased)
            {
                ReleaseElement(timer);
                continue;
            }

            timer->m_qwNextTime += timer->m_qwElapse;

            if (!timer->m_bExact && m_qwCurTime >= timer->m_qwNextTime)
            {
                timer->m_qwNextTime = m_qwCurTime + timer->m_qwElapse;
            }

            AddTimerElement(timer);
        }

        ++m_qwLastTime;
    }

    return nEvents;
}

unsigned long long CDMTimerModule::GetCurTime()
{
    return m_qwCurTime;
}

#ifdef WIN32

#if defined(_MSC_VER) || defined(_MSC_EXTENSIONS)
#define DELTA_EPOCH_IN_MICROSECS  11644473600000000Ui64
#else
#define DELTA_EPOCH_IN_MICROSECS  11644473600000000ULL
#endif


typedef union {
    unsigned long long ft_scalar;
    FILETIME ft_struct;
} FT;

int gettimeofday(struct timeval *tv, struct timezone *tz)
{
    FT ft;
    static int tzflag = 0;

    if (NULL != tv)
    {
        GetSystemTimeAsFileTime(&ft.ft_struct);
        ft.ft_scalar /= 10;
        ft.ft_scalar -= DELTA_EPOCH_IN_MICROSECS;
        tv->tv_sec = (long)(ft.ft_scalar / 1000000UL);
        tv->tv_usec = (long)(ft.ft_scalar % 1000000UL);
    }

    if (NULL != tz)
    {
        if (!tzflag)
        {
            _tzset();
            tzflag++;
        }

        tz->tz_minuteswest = _timezone / 60;
        tz->tz_dsttime = _daylight;
    }

    return 0;
}

#endif

unsigned long long CDMTimerModule::GetBootTime()
{
    unsigned int dwCurTime = GetTickCount32();
    unsigned int dwPassedTime = dwCurTime - m_dwTickTime;

    m_dwTickTime = dwCurTime;
    m_qwTickCount += dwPassedTime;
    return m_qwTickCount;
}

CDMTimerElement* CDMTimerModule::FetchElement()
{
    return m_oTimerElementPool.FetchObj();
}

void CDMTimerModule::ReleaseElement(CDMTimerElement* pElement)
{
    m_oTimerElementPool.ReleaseObj(pElement);
}
