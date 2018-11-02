#include <stdio.h>
#include <time.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/select.h>
#include "tools.h"

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
	tools_set_fd_nonblock(inet_fd);
    if(iRet == TOOLS_FAIL)
        return(iRet);

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
    case -1: iRet = EINTR; 				break;
    case  0: iRet = TOOLS_ERR_TMOUT; 	break;
    case  1: iRet = TOOLS_SUCCESS; 		break;
    case  2: iRet = TOOLS_ERR_CONN_ERR; break;
    default: iRet = TOOLS_FAIL; 		break;
    }
    
out:
	//��ԭ��֮ǰ��״̬��־
    fcntl(inet_fd, F_SETFL, old_fd_fl);
    return iRet;
}

//============================================================================================
//								 	common(all type) queue(multi-thread)
//============================================================================================

typedef struct __q
{
	u32 _capacity;
	u32 _sz;
	u32 _r_index;
	u32 _w_index;
	u32 _tmout;		/* ��ȡ_lock�ĵȴ�ʱ�� */
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
	
	pthread_mutex_unlock(&qptr->_lock);
	return TOOLS_SUCCESS;
}

s32 tools_queue_get(void **eleptr, queue *qptr)
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

	pthread_mutex_unlock(&qptr->_lock);
	return TOOLS_SUCCESS;
}

//=====================================================================================
//							common list
//=====================================================================================

//��ʱ5s��ȡ��
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

//================================================================================
//							 thread pool
//================================================================================

typedef s32 (*tools_cb_exec_task)(void *prmptr, u32 sz, void **ackpptr, u32 *lenptr);

typedef struct __job
{
	s32 prm_len;		/* param length */
	s32 result;			/* returned by cb */
	u32 acklen;
	void **ackpptr;
	tools_cb_exec_task cb;
	u8 param[];
} job;

typedef struct __thr_pool
{
	u32 _base_num;		/* thread num when init */
	u32 _now_num;		/* current thread num */
	queue *_job_que; 
	pthread_cond_t _cond;
	pthread_mutex_t _cond_mutex;
//	pthread_mutex_t _mutex;
	u8 remain[8];			/* remain byte */
} thr_pool, *p_thr_pool;

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
	
	return TOOLS_SUCCESS;
error_1:
	pthread_cond_destroy(&ackptr->_cond_mutex);
error_2:
	free(ackptr);
	return TOOLS_FAIL;
}

//���ش����ɹ�������
static s32 tools_thread_pool_create_thr(u32 tnum, void *(*cb)(void *), void *argptr)
{
	s32 iRet;
	u32 cnt = 0;
	pthread_t tid;
	for(s32 i=0; i<tnum; ++i)
	{
		iRet = pthread_create(&tid, cb, argptr);
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
		iRet = pthread_mutex_lock(&tplptr->_cond_mutex);
		if(iRet != TOOLS_SUCCESS)
			continue;
		//���ﲻ��Ҫѭ������ʹ���ܶ���̱߳����ѣ�Ҳ���������ռ��
		//��Ϊ�����������ȡ��������
		iRet = pthread_cond_wait(&tplptr->_cond, &tplptr->_cond_mutex);
		//���󷵻أ�Ӧ��û�м������أ������Ͳ����ͷ���
		if(iRet != TOOLS_SUCCESS)	
			continue;
		iRet = tools_queue_get(&jbptr, tplptr->_job_que);
		if(iRet != TOOLS_SUCCESS)
			continue;
		jbptr->result = (*jbptr->cb)((void *)jbptr->param, jbptr->prm_len, jbptr->ackpptr, &jbptr->acklen);
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
	*tplpptr->_now_num += iRet;
	return TOOLS_SUCCESS;
}

//================================================================================
//
//================================================================================

#define TOOLS_HIGH_CONCURRENCE_NUM 		1024
static s32 g_tools_high_concurrence_serv_fd;

//���̰߳�
void * tools_server_high_concurrence(void *p_arg)
{
	s32 iRet;
	s32 clnt_fd;
	pthread_t clnt_recv_tid;
	s32 poll_fd = 0;
	s32 serv_fd = *(s32 *)p_arg;
	struct epoll_event serv_event, clnt_event;
	struct epoll_event events[TOOLS_HIGH_CONCURRENCE_NUM];

	//��Ϊ������
	iRet = tools_set_fd_nonblock(serv_fd);
	if(iRet == TOOLS_FAIL)
		return iRet;

	//����һ��poll_fd
	serv_event.data.fd = serv_fd;
	serv_event.events = EPOLLIN | EPOLLET;
	poll_fd = epoll_create(TOOLS_HIGH_CONCURRENCE_NUM);

	//�ѷ������׽�����ӵ����������
	iRet = epoll_ctrl(poll_fd, EPOLL_CTL_ADD, serv_fd, &serv_event);
	if(iRet != TOOLS_SUCCESS)
	{
		close(serv_fd);
		return TOOLS_FAIL;
	}
	
	for( ; ; )
	{
		iRet = epoll_wait(poll_fd, events, TOOLS_HIGH_CONCURRENCE_NUM, 100);
		for(s32 i=0; i<iRet; i++)
		{
			if(events[i].data.fd == serv_fd)
			{
				clnt_fd = accept(serv_fd, NULL, 0);
				iRet = tools_set_fd_nonblock(clnt_fd);
				if(iRet != TOOLS_SUCCESS)
				{
					//�ر��׽��֣���ʱ����˵��׽��ֱ�ΪFIN_WAIT2״̬��
					//����˻���ͻ��˷���FIN���ģ��ͻ��˻�ӦACK���ģ����ǰ���λ���
					//֮��ͻ��������������һ�ο���д����ʱ����˻�Ӧһ��RST���ģ�֮��
					//��������д����ʱ������˽�����һ��SIGPIPE�źţ���ֹ���ӣ��Ӷ����
					//�����λ���
					close(clnt_fd);
					continue;
				}

				clnt_event.data.fd = clnt_fd;
				clnt_event.events = EPOLLIN | EPOLLET | EPOLLONESHOT;
				iRet = epoll_ctl(poll_fd, EPOLL_CTL_ADD, clnt_fd, &clnt_event);
				if(iRet != TOOLS_SUCCESS)
					close(clnt_fd);
			}
			else if(events[i].events & EPOLLIN)
			{
				job *jbptr = malloc(sizeof(job));
				jbptr->
			}
			else if(events[i].events & EPOLLOUT)
			{
				
			}
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
