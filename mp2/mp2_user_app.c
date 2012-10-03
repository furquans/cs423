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
#include <time.h>

/* Command structure for giving different params */
struct command {
	unsigned int P;
	unsigned int C;
	unsigned int n;
};

/* Different P, C and n values */
struct command cmd[] = {
	{450, 300, 10},
	{600, 300, 10},
	{1000, 300, 10},
	{340, 300, 10},
	{900, 300, 10},
	{5, 1, 3},
	{1500, 800, 10},
	{320, 200, 10},
};

int fd = -1;

/*
 * Func: get_random_number
 * Desc: Gives a random number
 *
 */
int get_random_number()
{
	int n = sizeof(cmd)/sizeof(struct command);
	unsigned int iseed = (unsigned int)time(NULL);

	srand(iseed);

	return(rand() % n);
}

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

	sprintf(command, "R, %d, %d, %d.", pid,P,C);
	write(fd,command,strlen(command));
}

/*
 * Func: deregister_process
 * Desc: Deregistering the process with kernel module
 *
 */
void deregister_process(unsigned int pid)
{
	char command[100];
	sprintf(command, "D, %d",pid);
	write(fd,command,strlen(command));
}

/*
 * Func: yield_process
 * Desc: Yield the process after current execution is over
 *
 */
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

	sprintf(pid_str, "%u", pid);

	ptr = command;

	while (1) {
		ptr = strstr(ptr, "PID:");

		if (ptr == NULL) {
			printf("Registration failed...exiting\n");
			break;
		}

		if (strncmp(pid_str,
			    ptr+4,
			    strlen(pid_str)) == 0) {
			printf("Registration successful\n");
			ret = 1;
			break;
		}
		ptr += 4;
	}
	return ret;
}

/*
 * Func: time_diff
 * Desc: Calculate difference in time
 *
 */
double time_diff(struct timeval *prev,
		 struct timeval *curr)
{
	double diff;

        diff = (curr->tv_sec*1000.0 + curr->tv_usec/1000.0) -
                (prev->tv_sec*1000.0 + prev->tv_usec/1000.0);

        return diff;
}

/*
 * Func: fact
 * Desc: Returns factorial of a number
 *
 */
unsigned long long fact(unsigned long long n)
{
        if((n==1) || (n==0))
                return 1;
        return(n*fact(n-1));
}

/*
 * Func: do_job
 * Desc: Function to be executed in loop
 *
 */
void do_job(int n)
{
	unsigned long long i,p = fact(n);

        for(i=1;i<p;i++) {
                fact(n);
        }
	printf("fact=%d\n",p);
	printf("done\n");
}

int main(int argc, char **argv)
{
	unsigned int pid;
	struct timeval curr,t0;
	int i = 0,n;
	unsigned int P,C;

	if (argc != 4) {
		int id;
		id = get_random_number();
		P = cmd[id].P;
		C = cmd[id].C;
		n = cmd[id].n;
	} else {
		P = atoi(argv[1]);
		C = atoi(argv[2]);
		n = atoi(argv[3]);
	}

	/* Get PID of the process */
	pid = getpid();
	printf("PID of process is %u,P=%u,C=%u,n=%d\n",pid,P,C,n);

	fd = open("/proc/mp2/status", O_RDWR);

	/* Register process with kernel module */
	register_process(pid, P, C);

	/* Read proc entry and check if we are registered */
	if (read_proc(pid) == 0) {
		exit(1);
	}

	/* get current time */
	gettimeofday(&t0,NULL);

	/* real time loop */
	while(i<10) {
		printf("Process doing a yield\n");
		/* yield control */
		yield_process(pid);
		/* Get current time and calculate difference in time */
		gettimeofday(&curr,NULL);
		printf("time=  %lf msecs since start\n",time_diff(&t0,&curr));
		/* do job */
		do_job(n);
		i++;
	}

	/* Deregister process */
	deregister_process(pid);
	printf("Process deregistered\n");
	return 0;
}
