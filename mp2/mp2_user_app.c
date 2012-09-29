/*
 * mp2_user_app.c: User application for mp2
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <fcntl.h>

int fd = -1;

/*
 * Func: register_process
 * Desc: Registering the process with kernel module
 *
 */
void register_process(unsigned int pid,
		      unsigned int P,
		      unsigned int C)
{
	char command[100];

	sprintf(command, "R, %d, %d, %d", pid,P,C);
	write(fd,command,strlen(command));
}

void deregister_process(unsigned int pid)
{
	char command[100];
	sprintf(command, "D, %d",pid);
	write(fd,command,strlen(command));
}

void yield_process(unsigned int pid)
{
	char command[100];
	sprintf(command, "Y, %d",pid);
	write(fd,command,strlen(command));
}

/*
 * Func: read_proc
 * Desc: Reading back from the kernel module
 *
 */
int read_proc(unsigned int pid)
{
	int len = 0;
        char command[4096];
	char *ptr;
	char pid_str[20];
	int ret = 0;

	len = read(fd,command,4096);
	command[len] = '\0';

        printf("Printing return value of read from /proc/mp2/status\n");
	printf("%s",command);

	sprintf(pid_str, "%u", pid);

	ptr = command;

	while (1) {
		ptr = strstr(ptr, "PID:");

		if (ptr == NULL) {
			printf("Entry not found\n");
			break;
		}

		if (strncmp(pid_str,
			    ptr+4,
			    strlen(pid_str)) == 0) {
			printf("Found entry\n");
			ret = 1;
			break;
		}
		ptr += 4;
	}
	return ret;
}

double time_diff(struct timeval *prev,
		 struct timeval *curr)
{
	double diff;

	if(prev->tv_sec > curr->tv_sec) {
		printf("here\n");
	}

        diff = (curr->tv_sec*1000.0 + curr->tv_usec/1000.0) -
                (prev->tv_sec*1000.0 + prev->tv_usec/1000.0);

        return diff;
}

unsigned long long fact(unsigned long long n)
{
        if((n==1) || (n==0))
                return 1;
        return(n*fact(n-1));
}

void calc_fact(int n)
{
	unsigned long long p = fact(n);
	int i;

        for(i=1;i<p;i++) {
                p = fact(n);
        }
	printf("done\n");
}

int main(int argc, char **argv)
{
	unsigned int pid;
	struct timeval curr,t0;
	int i = 0,n;

	if (argc != 4) {
		printf("Usage:%s <P> <C> <n>\n",argv[0]);
		exit(1);
	}

	/* Get PID of the process */
	pid = getpid();
	printf("PID of process is %u\n",pid);

	fd = open("/proc/mp2/status", O_RDWR);

	/* Register process with kernel module */
	register_process(pid, atoi(argv[1]), atoi(argv[2]));

	n = atoi(argv[3]);

	/* Read proc entry and check if we are registered */
	if (read_proc(pid) == 0) {
		exit(1);
	}

	gettimeofday(&t0,NULL);

	while(i<10) {
		yield_process(pid);
		gettimeofday(&curr,NULL);
		printf("time=  %f msecs since start\n",time_diff(&t0,&curr));
		calc_fact(n);
		i++;
	}

	/* Deregister process */
	deregister_process(pid);
}