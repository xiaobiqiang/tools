#include <stdio.h>
#include <time.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/select.h>
#include <stdlib.h>
#include <sys/epoll.h>
#include <string.h>
#include <errno.h>
#include <pthread.h>
#include <sys/types.h>
#include <netinet/tcp.h>
#include <signal.h>
#include "tools.h"

//�����ϵ��ļ�״̬��־
s32 tools_set_fd_nonblock(s32 fd)
{
    s32 ofl;
    s32 iRet;
    ofl = fcntl(fd, F_GETFL, 0);
    iRet = ofl | O_NONBLOCK;
    iRet = fcntl(fd, F_SETFL, iRet);
    if(iRet != TOOLS_SUCCESS)
        return TOOLS_FAIL;
    return ofl;
}


s32 tools_connect_tmout
(s32 inet_fd, struct sockaddr *ser_addr, u32 addr_len, u32 tmout)
{
    s32 iRet;
    //�ϵ�inet_fd�ļ�״̬��־
    s32 old_fd_fl = 0;
    fd_set wr_set, rd_set;
    struct timeval tm;

    //��inet_fd���ļ�״̬��־��Ϊ������ģʽ
    old_fd_fl = tools_set_fd_nonblock(inet_fd);
    if(old_fd_fl == TOOLS_FAIL)
        return(TOOLS_FAIL);

    //���errno����EINPROGRESS,
    //��ô������Ϊ��������û����ɶ����µ�����ʧ��
    iRet = connect(inet_fd, ser_addr, addr_len);
    if(iRet == TOOLS_SUCCESS || errno != EINPROGRESS)
        goto out;

    tm.tv_sec = tmout;
    tm.tv_usec = 0;
    FD_ZERO(&wr_set);
    FD_SET(inet_fd, &wr_set);
    FD_ZERO(&rd_set);
    FD_SET(inet_fd, &rd_set);
    iRet = select(inet_fd+1, &rd_set, &wr_set, NULL, &tm);
    switch(iRet)
    {
    //��������������ʱ,inet_fd��Ϊ���ɶ��ֿ�д������select�᷵��2
    //ǰ����select������rd_set
    //����ͨ��getsockopt(inet_fd, SOL_SOCKET, SO_ERROR)����ȡ����
    case -1:
        iRet = EINTR;
        break;
    case  0:
        iRet = TOOLS_ERR_TMOUT;
        break;
    case  1:
        iRet = TOOLS_SUCCESS;
        break;
    case  2:
        iRet = TOOLS_ERR_CONN_ERR;
        break;
    default:
        iRet = TOOLS_ERR_UNKNOWN;
        break;
    }

out:
    //��ԭ��֮ǰ��״̬��־
    fcntl(inet_fd, F_SETFL, old_fd_fl);
    return iRet;
}

//============================================================================================
//                                  common(all type) queue(multi-thread)
//============================================================================================

typedef struct __q
{
    u32 _capacity;
    u32 _sz;
    u32 _r_index;
    u32 _w_index;
    u32 _tmout;     /* ��ȡ_lock�ĵȴ�ʱ�� */
    pthread_mutex_t _lock;
    void *_eleptr[];
} queue, *p_que;

s32 tools_init_queue(u32 cap, u32 wtm, queue **ackpptr)
{
    s32 iRet;
    u32 ptr_sz = sizeof(void *);
    queue *pAck = malloc(sizeof(queue) + ptr_sz*cap);
    if(pAck == NULL)
        return TOOLS_ERR_MALLOC;

    pAck->_capacity = cap;
    pAck->_sz = 0;
    pAck->_r_index = 0;
    pAck->_w_index = 0;
    pAck->_tmout = wtm;
    iRet = pthread_mutex_init(&pAck->_lock, NULL);
    if(iRet != TOOLS_SUCCESS)
    {
        free(pAck);
        return iRet;
    }
    memset(&pAck->_eleptr, 0, sizeof(cap * ptr_sz));

    *ackpptr = pAck;
    return TOOLS_SUCCESS;
}

s32 tools_queue_add(void *eleptr, u32 len, queue *qptr)
{
    s32 iRet;

    TOOLS_MUTEX_TIMED_LOCK(&qptr->_lock, qptr->_tmout, &iRet);
    if(iRet != TOOLS_SUCCESS)
        return TOOLS_ERR_TMOUT;

    if(qptr->_sz >= qptr->_capacity)
    {
        pthread_mutex_unlock(&qptr->_lock);
        return TOOLS_FAIL;
    }

    ++qptr->_sz;
    qptr->_eleptr[qptr->_w_index++] = eleptr;
    if(qptr->_w_index >= qptr->_capacity)
        qptr->_w_index = 0;
    printf("addqueuesz:%u\n", qptr->_sz);
    pthread_mutex_unlock(&qptr->_lock);
    return TOOLS_SUCCESS;
}

s32 tools_queue_get(void **elepptr, queue *qptr)
{
    s32 iRet;
    void *rtptr = NULL;

    TOOLS_MUTEX_TIMED_LOCK(&qptr->_lock, qptr->_tmout, &iRet);
    if(iRet != TOOLS_SUCCESS)
        return TOOLS_ERR_TMOUT;

    if(qptr->_sz <= 0)
    {
        pthread_mutex_unlock(&qptr->_lock);
        return TOOLS_ERR_NOT_EXISTED;
    }

    --qptr->_sz;
    rtptr = qptr->_eleptr[qptr->_r_index++];
    //Ҫ�ǵöϵ����link
    qptr->_eleptr[qptr->_r_index-1] = NULL;
    if(qptr->_r_index >= qptr->_capacity)
        qptr->_r_index = 0;
    printf("getqueuesz:%u\n", qptr->_sz);
    pthread_mutex_unlock(&qptr->_lock);
    *elepptr = rtptr;
    return TOOLS_SUCCESS;
}

//=====================================================================================
//                          common list
//=====================================================================================

//��ʱ5s��ȡ��
/*
s32 tools_task_list_get_task(ptask_list list, task **pptask)
{
    s32 iRet;
    task *pAck = NULL;
    struct timespec tmout;

    TOOLS_MUTEX_TIMED_LOCK(&list->lock, 5, &iRet);
    if(iRet != TOOLS_SUCCESS)
        return iRet;

    if(list->sz <= 0)
    {
        pthread_mutex_unlock(&list->lock);
        return TOOLS_ERR_NOT_EXISTED;
    }

    pAck = list->p_head;
    list->p_head = pAck->p_next;
    pAck->p_next = NULL;

    if(list->sz == 1)
        list->p_tail = NULL;
    --list->sz;

    pthread_mutex_unlock(&list->lock);
    *pptask = pAck;
    return TOOLS_SUCCESS;
}

s32 tools_task_list_add_task(ptask_list list, task *ptask)
{
    s32 iRet;

    TOOLS_MUTEX_TIMED_LOCK(&list->lock, 5, &iRet);
    if(iRet != TOOLS_SUCCESS)
        return iRet;

    if(list->sz >= list->capacity)
    {
        pthread_mutex_unlock(&list->lock);
        return TOOLS_ERR_NOT_EXISTED;
    }

    if(list->sz <= 0)
        list->p_head = list->p_tail = ptask;
    else
    {
        list->p_tail->p_next = ptask;
        list->p_tail = ptask;
    }

    ptask->p_next = NULL;
    ++list->sz;

    pthread_mutex_unlock(&list->lock);
    return TOOLS_SUCCESS;
}
*/
//================================================================================
//                           thread pool
//================================================================================

typedef s32 (*tools_cb_exec_task)(void *prmptr, u32 sz, void **ackpptr, u32 *lenptr);

typedef struct __job
{
    s32 prm_len;        /* param length */
    s32 result;         /* returned by cb */
    u32 acklen;
    void *ackptr;
    tools_cb_exec_task cb;
    u8 param[];
} job;

typedef struct __thr_pool
{
    u32 _base_num;      /* thread num when init */
    u32 _now_num;       /* current thread num */
    queue *_job_que;
    pthread_cond_t _cond;
    pthread_mutex_t _cond_mutex;
//  pthread_mutex_t _mutex;
    u8 remain[8];           /* remain byte */
} thr_pool, *p_thr_pool;

s32 tools_thread_pool_new_job(void *prm, u32 len, tools_cb_exec_task cb, job **jbpptr)
{
    job *jbptr = malloc(sizeof(job)+len);
    if(jbptr == NULL)
        return TOOLS_ERR_MALLOC;
    jbptr->acklen = 0;
    jbptr->ackptr = NULL;
    jbptr->cb = cb;
    jbptr->result = 0;
    jbptr->prm_len = len;
    memcpy(&jbptr->param, prm, len);

    *jbpptr = jbptr;
    return TOOLS_SUCCESS;
}

static s32 tools_thread_pool_init(u32 tnum, u32 wtm, u32 jbnum, thr_pool **tplpptr)
{
    s32 iRet;
    thr_pool *ackptr = malloc(sizeof(thr_pool));
    if(ackptr == NULL)
        return TOOLS_ERR_MALLOC;

    ackptr->_base_num = tnum;
    ackptr->_now_num = 0;
    iRet = tools_init_queue(jbnum, wtm, &ackptr->_job_que);
    if(iRet != TOOLS_SUCCESS)
        goto error_2;

    iRet = pthread_cond_init(&ackptr->_cond, NULL);
    if(iRet != TOOLS_SUCCESS)
        goto error_2;

    iRet = pthread_mutex_init(&ackptr->_cond_mutex, NULL);
    if(iRet != TOOLS_SUCCESS)
        goto error_1;
    memset(&ackptr->remain, 0, sizeof(ackptr->remain));
    *tplpptr = ackptr;
    return TOOLS_SUCCESS;
error_1:
    pthread_mutex_destroy(&ackptr->_cond_mutex);
error_2:
    free(ackptr);
    return TOOLS_FAIL;
}

//���ش����ɹ�������
static s32 tools_thread_pool_create_thr(u32 tnum, void *(*cb)(void *), void *argptr)
{
    s32 iRet;
    s32 i = 0;
    u32 cnt = 0;
    pthread_t tid;
    for(i=0; i<tnum; ++i)
    {
        iRet = pthread_create(&tid, NULL, cb, argptr);
        if(iRet == TOOLS_SUCCESS)
        {
            ++cnt;
            pthread_detach(tid);
        }
    }
    return cnt;
}

static void * tools_cb_thread_pool(void *argptr)
{
    s32 iRet;
    job *jbptr = NULL;
    thr_pool *tplptr = argptr;
    for( ; ; )
    {
        /*  ֮���԰��ⲿ���Ƶ��������к���Ҫ��ԭ��ġ�
            ����һ�£���һ�����߳��ڴ�������е�job��ֻ�м����߳�
            ��������״̬����ʱ�¼ӽ���һ�����񣬻���˯�ߵ��̣߳�
            ��Щ�̴߳����job���������¼ӽ�����job�������������̴߳������?
            ���������ڶ����ж��ò�������ֻ�еȵ���һ��������ӽ���������һ����
            �õ���������ͣ���ڶ����е�������ܻ�Խ��Խ�࣬���ֻ���ö�����û����
            �����ʱ���̲߳���ȥ���ߡ�
        iRet = pthread_mutex_lock(&tplptr->_cond_mutex);
        if(iRet != TOOLS_SUCCESS)
            continue;
        //���ﲻ��Ҫѭ������ʹ���ܶ���̱߳����ѣ�Ҳ���������ռ��
        //��Ϊ�����������ȡ��������
        iRet = pthread_cond_wait(&tplptr->_cond, &tplptr->_cond_mutex, );
        //���󷵻أ�Ӧ��û�м������أ������Ͳ����ͷ���
        if(iRet != TOOLS_SUCCESS)
            continue;
        */
        iRet = tools_queue_get(&jbptr, tplptr->_job_que);
        //job������û��job��Ҫ������ô���߳�����
        //�ȴ���һ�δ���(��������������)
        if(iRet != TOOLS_SUCCESS)
        {
            iRet = pthread_mutex_lock(&tplptr->_cond_mutex);
            if(iRet != TOOLS_SUCCESS)
                continue;
            //���ﲻ��Ҫѭ������ʹ���ܶ���̱߳����ѣ�Ҳ���������ռ��
            //��Ϊ�����������ȡ��������
            pthread_cond_wait(&tplptr->_cond, &tplptr->_cond_mutex);
            //���󷵻أ�Ӧ��û�м������أ������Ͳ����ͷ���
            //pthread_cond_wait�ɹ����غ����ס_cond_mutex
            (void)pthread_mutex_unlock(&tplptr->_cond_mutex);
            continue;
        }
        if(jbptr->cb != NULL)
        {
            jbptr->result = (*jbptr->cb)((void *)jbptr->param, jbptr->prm_len,
                                         &jbptr->ackptr, &jbptr->acklen);
        }
        free(jbptr);
        jbptr = NULL;
    }
}

//���������߳�����tnum,��ô���涯̬����
s32 tools_thread_pool_create(u32 tnum, u32 wtm, u32 jbnum, thr_pool **tplpptr)
{
    s32 iRet;
    pthread_t tid;

    iRet = tools_thread_pool_init(tnum, wtm, jbnum, tplpptr);
    if(iRet != TOOLS_SUCCESS)
        return iRet;

    iRet = tools_thread_pool_create_thr(tnum, tools_cb_thread_pool, *tplpptr);
    (*tplpptr)->_now_num += iRet;
    return TOOLS_SUCCESS;
}

s32 tools_thread_pool_add_job(job *jbptr, thr_pool *tplptr)
{
    s32 iRet;
    queue *qptr = tplptr->_job_que;
    iRet = tools_queue_add(jbptr, sizeof(job)+jbptr->prm_len, qptr);
    if(iRet != TOOLS_SUCCESS)
        return iRet;
    (void)pthread_cond_broadcast(&tplptr->_cond);
    return TOOLS_SUCCESS;
}

//================================================================================
//                              server high-concurrence
//================================================================================

#define TOOLS_HIGH_CONCURRENCE_NUM          1024
#define TOOLS_HOGH_CONCURRENCE_MAX_MSG_LEN  512
#define TOOLS_HIGH_CONCURRENCE_RPC_MAGIC    0x12345678
#define TOOLS_HIGH_CONCURRENCE_TRANS_TIMEO  5

//��������ÿͻ��˵������ǳ����Ӿ�ʹ��undef��
//�����ӵ��׽���ֻ����recv�����м�⵽�ͻ��˹ر�֮��Ż�ر�
#define TOOLS_HIGH_CONCURRENCE_LONG_CONN    1

#ifdef TOOLS_HIGH_CONCURRENCE_LONG_CONN
#define TOOLS_HIGH_CONCURRENCE_KEEP_IDLE    10
#define TOOLS_HIGH_CONCURRENCE_KEEP_INTR    5
#define TOOLS_HIGH_CONCURRENCE_KEEP_CNT     2
#endif

typedef s32 (*tools_high_concurrence_cb_rpc)
(void * prmptr,u32 len,void **ackpptr,u32 *lenptr);

typedef enum __rpc_msg_type
{
    _RPC_MSG_BUTT
} _rpc_type;

typedef struct __rpc_msg
{
    u32 _magic;
    u32 _prm_len;
    u32 _head_len;
    u32 _result;
    s32 _clnt;
    _rpc_type _type;
    u8 _param[];
} rpc_msg;

typedef struct __rpc_type_cfg
{
    _rpc_type _type;
    tools_high_concurrence_cb_rpc _cb;
} rpc_type_cfg;

const rpc_type_cfg g_tools_high_concurrence_rpc_cfg[] =
{

};

static s32 g_tools_high_concurrence_serv_fd;
static s32 g_tools_high_concurrence_poll_fd;
static thr_pool *gp_tools_high_concurrence_recv_thr_pool;
static thr_pool *gp_tools_high_concurrence_send_thr_pool;
static struct epoll_event events[TOOLS_HIGH_CONCURRENCE_NUM];

static s32 tools_high_concurrence_new_rpc_msg
(s32 result, u32 type, void *prmptr, u32 prmlen, s32 _clnt, rpc_msg **msgpptr)
{
    rpc_msg *msgptr = malloc(sizeof(rpc_msg) + prmlen);
    if(msgptr == NULL)
        return TOOLS_FAIL;
    msgptr->_clnt = _clnt;
    msgptr->_head_len = sizeof(rpc_msg);
    msgptr->_magic = TOOLS_HIGH_CONCURRENCE_RPC_MAGIC;
    msgptr->_result = result;
    msgptr->_type = type;
    msgptr->_prm_len = prmlen;
    memcpy(&msgptr->_param, prmptr, prmlen);
    *msgpptr = msgptr;
    return TOOLS_SUCCESS;
}

static void tools_high_concurrence_set_clnt_sockopt(s32 clnt)
{
    //���պͷ��͵ĳ�ʱֵ
    u32 sndtmout = TOOLS_HIGH_CONCURRENCE_TRANS_TIMEO;
    u32 rcvtmout = TOOLS_HIGH_CONCURRENCE_TRANS_TIMEO;
#ifdef TOOLS_HIGH_CONCURRENCE_LONG_CONN
    u32 keepalive = 1;
    u32 keepidle = TOOLS_HIGH_CONCURRENCE_KEEP_IDLE;
    u32 keepinterval = TOOLS_HIGH_CONCURRENCE_KEEP_INTR;
    u32 keepcnt = TOOLS_HIGH_CONCURRENCE_KEEP_CNT;
    //���Զ�̽��
    setsockopt(clnt, SOL_SOCKET, SO_KEEPALIVE, &keepalive, sizeof(u32));
    //���ö��û�����ݽ�����ʼ���
    setsockopt(clnt, IPPROTO_TCP, TCP_KEEPIDLE, &keepidle, sizeof(u32));
    //���÷������
    setsockopt(clnt, IPPROTO_TCP, TCP_KEEPINTVL, &keepinterval, sizeof(u32));
    //���÷�̽����ĸ���
    setsockopt(clnt, IPPROTO_TCP, TCP_KEEPCNT, &keepcnt, sizeof(u32));
    //��������̽������ղ���һ����Ӧʱ,�ͻ��clnt���׽��ֱ�Ϊ�ɶ�д������
    //��recvʱ�᷵��-1,����errnoΪETIMEOUT
#endif 
    setsockopt(clnt, SOL_SOCKET, SO_SNDTIMEO, &sndtmout, sizeof(u32));
    return ;
}

static s32 tools_high_concurrence_deal_send_clnt
(void * prmptr,u32 len,void **ackpptr,u32 * lenptr)
{
    s32 iRet;
    void *ackptr = NULL;
    u32 acklen = 0;
    rpc_type_cfg *pCfg = NULL;
    rpc_msg *_msgptr = prmptr;

    if(_msgptr->_type >= _RPC_MSG_BUTT)
        return TOOLS_ERR_NOT_SUPPORTED;

    pCfg = &g_tools_high_concurrence_rpc_cfg[_msgptr->_type];
    iRet = pCfg->_cb(_msgptr->_param, _msgptr->_prm_len, &ackptr, &acklen);
    //���ɹ����������ص�����,���Ѵ���Ľ�����͹�ȥ
    if(iRet != TOOLS_SUCCESS)
    {
        if(ackptr != NULL)
            free(ackptr);
        ackptr = NULL;
        acklen = 0;
    }

    iRet = tools_high_concurrence_new_rpc_msg
           (iRet, _msgptr->_type, ackptr, acklen, _msgptr->_clnt, &_msgptr);
    if(iRet == TOOLS_SUCCESS)
    {
        send(_msgptr->_clnt, _msgptr, _msgptr->_head_len+_msgptr->_prm_len, 0);
        free(_msgptr);
    }
#ifndef TOOLS_HIGH_CONCURRENCE_LONG_CONN
    close(_msgptr->_clnt);
#endif
}

static s32 tools_high_concurrence_recv_from_clnt
(void *prmptr, u32 len, void **ackpptr, u32 *lenptr)
{
    s32 iRet;
    rpc_msg *msgptr = NULL;
    job *jbptr = NULL;
    u32 recv_len= 0;
    u8 recvbuf[TOOLS_HOGH_CONCURRENCE_MAX_MSG_LEN] = {0};
    //�׽���״̬�Ƿ�������
    s32 clnt = *(s32 *)prmptr;
    printf("coming into recv\n");
    tools_high_concurrence_set_clnt_sockopt(clnt);
    while((iRet = recv(clnt, recvbuf+recv_len, sizeof(recvbuf)-recv_len, 0)) > 0)
        recv_len+=iRet;
    //������socket�����ݶ�ȡ����errno����ΪEAGAIN
    //��clnt��epoll�ļ����¼�������EPOLLONTSHOT,
    //��ôӦ�øϿ���뵽��������
    printf("iRet:%d, no:%d, %s\n", iRet, errno, strerror(errno));
    if(iRet == -1 && errno == EAGAIN)
    {
       //���յ������ݴ���
        if(recv_len < sizeof(rpc_msg) ||
          ((rpc_msg *)recvbuf)->_magic != TOOLS_HIGH_CONCURRENCE_RPC_MAGIC ||
          ((rpc_msg *)recvbuf)->_type >= _RPC_MSG_BUTT)
           goto error;
           
        ((rpc_msg *)recvbuf)->_clnt = clnt;
        iRet = tools_thread_pool_new_job(recvbuf, recv_len,
                                         tools_high_concurrence_deal_send_clnt, &jbptr);
        if(iRet != TOOLS_SUCCESS)
            goto error;
        iRet = tools_thread_pool_add_job(jbptr, gp_tools_high_concurrence_send_thr_pool);
        if(iRet != TOOLS_SUCCESS)
        {
            free(jbptr);
            goto error;
        } 
        return TOOLS_SUCCESS;
    }
    //��accept���׽��ֿͻ�����һ�˶Ͽ����ᴥ��EPOLLIN��EPOLLOUT�¼���
    //��ʱ��д������clnt�׽��ֻ����SIGPIPE�źŲ���recv����0
    //��ʱ����Ҫ�ر�clnt����ֹ����˵����clnt�׽�����TIME_WAIT2״̬.
    //���Է����Ҫ����SIGPIPE�źţ���ֹ��Ϊ����źŶ����½�����ֹ
    else if(iRet == 0)
    {
        printf("===============\n");
        close(clnt);
        return TOOLS_SUCCESS;
    }
error:
    //ֻҪ�����ر��ԭ����ô�ͻ��˶�Ӧ���յ���Ϣ��
    //����ͳһ���ͷ������쳣��Ϣ���ͻ���
    iRet = tools_high_concurrence_new_rpc_msg(clnt, 0, NULL, 0, clnt, &msgptr);
    if(iRet == TOOLS_SUCCESS)
    {
        send(clnt, msgptr, msgptr->_head_len+msgptr->_prm_len, 0);
        free(msgptr);
    }
#ifndef TOOLS_HIGH_CONCURRENCE_LONG_CONN
    close(clnt);
#endif
    return TOOLS_FAIL;

}

//���̰߳�
void * tools_server_high_concurrence(void *p_arg)
{
    s32 iRet;
    s32 clnt_fd;
    s32 poll_fd = 0;
    u32 clnt_addr_len = 0;
    s32 serv_fd = *(s32 *)p_arg;
    struct sockaddr_in clnt_addr;
    struct epoll_event serv_event, clnt_event;

    //��Ϊ������
    iRet = tools_set_fd_nonblock(serv_fd);
    if(iRet == TOOLS_FAIL)
        return NULL;

    //����һ��poll_fd
    serv_event.data.fd = serv_fd;
    serv_event.events = EPOLLIN | EPOLLET;
    poll_fd = epoll_create(TOOLS_HIGH_CONCURRENCE_NUM);
    g_tools_high_concurrence_poll_fd = poll_fd;
    //�ѷ������׽�����ӵ����������
    iRet = epoll_ctl(poll_fd, EPOLL_CTL_ADD, serv_fd, &serv_event);
    if(iRet != TOOLS_SUCCESS)
    {
        close(serv_fd);
        return NULL;
    }

    printf("server_fd: %d\n", serv_fd);
    u32 test = 0;
    for( ; ; )
    {
        u32 i=0;
        iRet = epoll_wait(poll_fd, events, TOOLS_HIGH_CONCURRENCE_NUM, -1);
        for(i=0; i<iRet; i++)
        {
            if(events[i].data.fd == serv_fd)
            {
                printf("xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx\n");
                clnt_fd = accept(serv_fd, &clnt_addr, &clnt_addr_len);
                printf("clnt_fd: %d\n", clnt_fd);
                iRet = tools_set_fd_nonblock(clnt_fd);
                if(iRet == TOOLS_FAIL)
                {
                    //�ر��׽��֣���ʱ����˵��׽��ֱ�ΪFIN_WAIT2״̬��
                    //����˻���ͻ��˷���FIN���ģ��ͻ��˻�ӦACK���ģ����ǰ���λ���
                    //֮��ͻ��������������һ�ο���д����ʱ����˻�Ӧһ��RST���ģ�֮��
                    //��������д����ʱ���ͻ��˽�����һ��SIGPIPE�źţ�Ĭ�϶�������ֹ���̣�
                    //�Ӷ�ǿ����ɺ����λ���
                    close(clnt_fd);
                    continue;
                }

                clnt_event.data.fd = clnt_fd;
                clnt_event.events = EPOLLIN | EPOLLET;
                iRet = epoll_ctl(poll_fd, EPOLL_CTL_ADD, clnt_fd, &clnt_event);
                if(iRet != TOOLS_SUCCESS)
                    close(clnt_fd);
            }
            else if(events[i].events & EPOLLIN)
            {
                job *jbptr = NULL;
                clnt_fd = events[i].data.fd;
                printf("link clnt:%d\n", clnt_fd);
                s32 iRet = tools_thread_pool_new_job(&clnt_fd, sizeof(s32),
                                                     tools_high_concurrence_recv_from_clnt, &jbptr);
                if(iRet != TOOLS_SUCCESS)
                    goto error;
                iRet = tools_thread_pool_add_job(jbptr, gp_tools_high_concurrence_recv_thr_pool);
                if(iRet != TOOLS_SUCCESS)
                {
                    free(jbptr);
                    goto error;
                }
error:
#ifndef TOOLS_HIGH_CONCURRENCE_LONG_CONN
                epoll_ctl(poll_fd, EPOLL_CTL_DEL, clnt_fd, &events[i]);
                close(clnt_fd);
#endif
                continue; 

            }
            //ETģʽ�´���EPOLLOUT�¼�:
            //1.��������->����
            //2.����EPOLLIN�¼��ḽ������EPOLLOUT
            //3.accept�ɹ�֮��ᴥ��һ��EPOLLOUT
        }
    }
}


int main_tools_connect_tmout(int argc, char **argv)
{
    s32 iRet;
    s32 inetfd;
    struct sockaddr_in addr =
    {
        .sin_family = AF_INET,
        .sin_port = 8800
    };
    inet_pton(AF_INET, "192.168.149.130", &addr.sin_addr.s_addr);
    inetfd = socket(AF_INET, SOCK_STREAM, 0);
    iRet = tools_connect_tmout(inetfd, &addr, sizeof(struct sockaddr_in), 10);
    printf("connect iRet: %d\n", iRet);
    close(inetfd);
    return 0;
}

void sig_handler_of_pipe(int signo)
{
    static s32 i=0;
    printf("i:%d\n", i++);
}

int main_server(int argc, char **argv)
{
    u32 baddr = 0;
    s32 reuseaddr = 1;
    pthread_t tid;
    struct sockaddr_in addr;
    g_tools_high_concurrence_serv_fd = socket(AF_INET, SOCK_STREAM, 0);
    setsockopt(g_tools_high_concurrence_serv_fd, 
        SOL_SOCKET, SO_REUSEADDR, &reuseaddr, sizeof(reuseaddr));
    inet_pton(AF_INET, argv[1], &baddr);
    addr.sin_family = AF_INET;
    addr.sin_port = htons(8800);
    addr.sin_addr.s_addr = baddr;
    bind(g_tools_high_concurrence_serv_fd, &addr, sizeof(addr));
    listen(g_tools_high_concurrence_serv_fd, 200);
    
    tools_thread_pool_create(3, 5, 200, &gp_tools_high_concurrence_recv_thr_pool);
    tools_thread_pool_create(3, 5, 200, &gp_tools_high_concurrence_send_thr_pool);
    signal(SIGPIPE, sig_handler_of_pipe);
    pthread_create(&tid, NULL, tools_server_high_concurrence, &g_tools_high_concurrence_serv_fd);
    pthread_detach(tid);
    while(1)
    {
        sleep(100);
    }
    return 0;
}

int main_client(int argc, char **argv)
{
    s32 iRet;
    u8 buf[32] = {0};
    u32 baddr = 0;
    s32 clnt_fd = 0;
    s32 reuseaddr = 1;
    struct sockaddr_in addr;
    clnt_fd = socket(AF_INET, SOCK_STREAM, 0);
    setsockopt(clnt_fd, SOL_SOCKET, SO_REUSEADDR, &reuseaddr, sizeof(reuseaddr));
    inet_pton(AF_INET, "127.0.0.1", &baddr);
    addr.sin_family = AF_INET;
    addr.sin_port = htons(8800);
    addr.sin_addr.s_addr = baddr;
    iRet = tools_connect_tmout(clnt_fd, &addr, sizeof(addr), 5);
    printf("conn: %d\n", iRet);
    
    while(1)
    {
        printf("clnt:%d send_len: %d\n", clnt_fd, send(clnt_fd, "hello world", sizeof("hello world"), 0));
        iRet = recv(clnt_fd, buf, 32, 0);
        printf("clnt:%d iRet:%d\n", clnt_fd, ((rpc_msg *)buf)->_result);
        sleep(10);
    }
}

#define TEST_CLIENT
//#undef TEST_CLIENT
int main(int argc, char **argv)
{
#ifdef TEST_CLIENT
    return main_client(argc, argv);
#else
    return main_server(argc, argv);
#endif
}

