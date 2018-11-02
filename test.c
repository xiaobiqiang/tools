#include <sys/socket.h>
#include <sys/types.h>
#include <stdio.h>
#include <netinet/in.h>
#include <unistd.h>
#include <sys/fcntl.h>
#include <sys/ioctl.h>
#include <sys/uio.h>
#include <sys/utsname.h>

int main0(int argc, char **argv)
{
	int iRet;
	int fd;
	char buf_1[128] = "hello world";
	char buf_2[128] = {"hhhhhh"};
	struct iovec vec[2] = 
	{
		{buf_1, 128},
		{buf_2, 128}
	};
	fd = open(argv[1], O_WRONLY, 0);
	iRet = write(fd, buf_2, sizeof(buf_2));
	//会和前面的write共享当前的文件偏移量
	iRet = writev(fd, vec, sizeof(vec)/sizeof(struct iovec));
	close(fd);
	return 0;
}

int main(int argc, char **argv)
{
	struct utsname uts;
	uname(&uts);
	printf("%s,%s", uts.machine, uts.nodename);
	return 0;
}

