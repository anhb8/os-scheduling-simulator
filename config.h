#ifndef CONFIG_H
#define CONFIG_H

#define MAXSECONDS 3
#define MAXPROCESSES 18
#define TOTAL_MAXPROC 50

//Time slice for 3 scheduling queues
#define Q1_TIMESLICE 10000000
#define Q2_TIMESLICE 20000000
#define Q3_TIMESLICE 40000000

//Dispatch time
#define MINDISPATCH 100
#define MAXDISPATCH 10000

//Maximum lines in log file
#define MAXLINE 10000
#define IO_BOUND_PROB 30

//Maximum wait time 
#define MAXWAIT_Q2 2000000000
#define MAXWAIT_Q3 4000000000

const key_t sharedM_key = 1234;
const key_t queue_key = 2121;

//Define states for process
enum state{
	sNEW = 0,
	sREADY,
	sBLOCKED,
	sTERMINATED
};

//Define message format to communicate between scheduler and child process
struct ossMsg{
	long mtype;
	pid_t from;
	
	int timeslice;
	
	struct timespec clock;
	struct timespec blockedTime;

};

#define MESSAGE_SIZE (sizeof(struct ossMsg) - sizeof(long))

//Scheduling queue types: highest priority, second highest priority, third highest priority, and blocked
enum qTypes{
	qONE = 0,
	qTWO,
	qTHREE,
	qBLOCKED,
	qCOUNT
};

enum pTypes{
	pNORM=0,
	pREAL,
	pCOUNT
};

//Scheduling queue
struct queue{
	unsigned int ids[MAXPROCESSES];
	unsigned int length;
};

//Process control block
struct userPCB{
	pid_t pid;
	int q_location;
	int priority;
	unsigned int id;
	enum state state;

	struct timespec t_cpu;
	struct timespec t_sys;
	struct timespec t_burst;
	struct timespec t_blocked;
	struct timespec t_started;
};

//Shared memory
struct sharedM{
	struct timespec clock;
	struct userPCB users[MAXPROCESSES];
};

//Report
struct ossReport{
	int c_highprior;
	int c_lowprior;
	unsigned int usersStarted;
	unsigned int usersTerminated;
	struct timespec t_wait;
	struct timespec t_sys;
	struct timespec t_cpu;
	struct timespec t_blocked[pCOUNT];
	struct timespec cpuIdleTime;
}; 


#endif
