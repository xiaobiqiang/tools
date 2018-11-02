#ifndef TOOLS_H
#define TOOLS_H

#include <netinet/in.h>
#include <sys/socket.h>

#define TOOLS_FAIL		   	   -1
#define TOOLS_SUCCESS			0
#define TOOLS_ERR_TMOUT			1
#define TOOLS_ERR_CONN_ERR		2
#define TOOLS_ERR_NOT_EXISTED	3
#define TOOLS_ERR_MALLOC		4
#define TOOLS_MUTEX_TIMED_LOCK(plock, tmout, pret)	\
{	\
	struct timespec abs_wait_tm;	\
	clock_gettime(CLOCK_REALTIME, &abs_wait_tm);	\
	abs_wait_tm.tv_sec += (tmout);		\
	*(pret) = pthread_mutex_timedlock((plock), &abs_wait_tm);	\
}

typedef unsigned char 		u8;
typedef signed char 		s8;
typedef unsigned int 		u32;
typedef signed int   		s32;

extern s32 tools_connect_tmout
	(s32 inet_fd, struct sockaddr *ser_addr, u32 addr_len, u32 tmout);

extern void * tools_server_high_concurrence(void *p_arg);

#endif