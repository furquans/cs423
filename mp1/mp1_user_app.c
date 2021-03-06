/*
 * mp1_user_app.c: User application for mp1
 *
 */

#include<stdio.h>
#include<string.h>
#include<stdlib.h>

/*
 * Func: register_process
 * Desc: Registering the process with kernel module
 *
 */
void register_process(int pid) {
        char command[100];
	/* Write to /proc/mp1/status */
        sprintf(command, "echo %d >/proc/mp1/status", pid);
        system(command);
}

/*
 * Func: read_proc
 * Desc: Reading back from the kernel module
 *
 */
void read_proc()
{
	/* read from /proc/mp1/status */
	char command[] = "cat /proc/mp1/status";
	printf("Printing return value of read from /proc/mp1/status\n");
	printf("PID:CPU Time\n");
	system(command);
}

/*
 * Func: calculate_factorial
 * Desc: Calcuate factorial of a given number n
 *
 */
int calculate_factorial(int n) 
{
	int prod=1, i=1; 
	if (n<0) 
		return -1; 
	if (n==0)
		return 1; 
	for (i=2; i<=n; i++)
		prod=prod*i; 
	return prod; 
}

/*
 * Func: find_factorial_factorial_times
 * Desc: Find factorial of a number for factorial times
 *
 */
void find_factorial_factorial_times(int n, int fact)
{
	int i=0; 

	for (i=0; i<=fact; i++) {
		calculate_factorial(n); 
	}
	printf("Factorial of %d is calculated %d times !\n", n, calculate_factorial(n)); 
}

/*
 * Func: find_factorials
 * Desc: Find factorials of numbers from 1 to n
 *
 */
void find_factorials(int n) 
{
	int i;

	for (i=0; i<=n; i++) {
		printf("\nFactorial of %d is %d\n", i, calculate_factorial(i)); 
		find_factorial_factorial_times(i, calculate_factorial(i)); 
	}
}

/*
 * Func: main
 * Desc: main body of the user space application
 *
 */
void main(int argc, char **argv)
{
	int pid;
	int n = 12;

	/* Obtain the number to calculate factorial upto */
	if (argc==2) {
		n = atoi(argv[1]);
	}

	/* Get pid of the process */
	pid=getpid(); 
	printf("PID of this process is %d", pid); 

	/* Register the process with our module */
	register_process(pid); 

	/* Calculate factorial */
	find_factorials(n); 

	/* Read the entry in /proc/mp1/status */
	read_proc();
}
