#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ipc.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/msg.h>
#include <sys/shm.h>
#include <strings.h>
#include <signal.h>
#include "config.h"
#include <limits.h>

char *prog;
char mainLog[256] = "log.mainlog";
static int maxSec = MAXSECONDS;
static int allProcesses[MAXPROCESSES];
static unsigned int next_id = 1;

//System clock
static const struct timespec maxTimeBetweenNewProcs = {.tv_sec = 2, .tv_nsec = INT_MAX};
static struct timespec n_time = {.tv_sec = 0, .tv_nsec = 0};

//Shared memory and message queue id
static int qid=-1;
static int sid=-1;
static struct sharedM *shm = NULL;

//Scheduling queues:  highest priority, second highest priority, third highest priority, and blocked
static struct queue pq[qCOUNT];

//Line number in log file
static unsigned int logLine = 0;

//Report
static struct ossReport reportV;

//Check if log file exceeds 1000 lines
static void checkLog(){
	if(logLine >= MAXLINE){
		printf("OSS: Current log has exceed %d lines, the system will terminate now\n",MAXLINE);
		raise(SIGINT);
	}
}
//Create shared memory, message queue
static int createSharedM(){
	//Shared memory
	sid = shmget(sharedM_key, sizeof(struct sharedM), IPC_CREAT | IPC_EXCL | S_IRWXU);
	if(sid < 0){
		fprintf(stderr,"%s: failed to get id for shared memory. ",prog);
                perror("Error:shmget failed");
                return -1;
	}

	shm = (struct sharedM *)shmat(sid,NULL,0);
	if(shm == (void *)-1){
		fprintf(stderr,"%s: failed to get pointer for shared memory. ",prog);
                perror("Error:shmat failed");
                return -1;
	}

	//Message queue
	qid = msgget(queue_key, IPC_CREAT | IPC_EXCL | 0666);
	if(qid == -1){
                fprintf(stderr,"%s: failed to get id for queue. ",prog);
                perror("Error:msgget failed");
                return -1;
        }

	int i;
	for (i=0;i<=MAXPROCESSES;i++) {
		allProcesses[i]=0;
	}

	return 0;

}

//Deallocate shared memory and message queue
static void deallocatesharedM(){
	if(shm != NULL){
		if(shmdt(shm) == -1){
			fprintf(stderr,"%s: failed to detach shared memory. ",prog);
                	perror("Error: shmdt failed");
		}
	}

	if(sid != -1){
		if(shmctl(sid, IPC_RMID, NULL) == -1){
			fprintf(stderr,"%s: failed to delete shared memory. ",prog);
                        perror("Error: smctl failed");
		}
	}

	if(qid != -1){
		if(msgctl(qid, IPC_RMID, NULL) == -1){
			fprintf(stderr,"%s: failed to delete message queue.",prog);
                        perror("Error: msgctl failed");
		}	
	}
}

static void markSlot(const int u) {
	if (allProcesses[u]==0)
		allProcesses[u]=1;
	else if(allProcesses[u]==1) 
		allProcesses[u]=0;
	
}

//Check free slot in the process control table 
static int getFreeSlot() {
	int i;
	for (i=0;i<=MAXPROCESSES;i++){
		if (allProcesses[i]==0) {
			markSlot(i);
			return i;
		}
	}

	return -1;
}

static int pushQ(const int qid, const int index){
	struct queue *q = &pq[qid];
  	q->ids[q->length++] = index;
  	return qid;

}

static int popQ(struct queue *currentQ, const int pos){
	unsigned int i;
  	unsigned int u = currentQ->ids[pos];

	//Pop the items and then shift the rest to the left
  	for (i = pos; i < currentQ->length - 1; i++){
    		currentQ->ids[i] = currentQ->ids[i + 1];
		
  	}
	//Update queue size
	currentQ->length--;

  	return u;

}

//Move blocked processes to the highest priority
static void unblockUsers(){
	unsigned int i;
  	for (i = 0; i < pq[qBLOCKED].length;i++){
		//Get first block index
		const int u = pq[qBLOCKED].ids[i];
		struct userPCB *usr = &shm->users[u];

		//Check if it is time to unblock
		if ((usr->t_blocked.tv_sec < shm->clock.tv_sec) ||
        		((usr->t_blocked.tv_sec == shm->clock.tv_sec) &&
         		(usr->t_blocked.tv_nsec <= shm->clock.tv_nsec))){
			//Mark user states as ready
			usr->state = sREADY;
      			usr->t_blocked.tv_sec = 0;
      			usr->t_blocked.tv_nsec = 0;
			usr->q_location = qONE; //Put blocked process into first queue
			
			++logLine;
			printf("OSS: Unblocked PID %d at %lu:%lu and put it in the back of queue %d\n", usr->id, shm->clock.tv_sec, shm->clock.tv_nsec, usr->q_location);

			//pop from blocked queue
			popQ(&pq[qBLOCKED], i);
		        //pq[qBLOCKED].length--;

			//push at the end of its ready queue
			pushQ(usr->q_location, u);
		}

	}

}

static int forkProcess(){
	char buf[10];
	const int u = getFreeSlot();
	if(u == -1)
		return EXIT_SUCCESS;

	//Allocate process control block
	struct userPCB *usr = &shm->users[u]; 

	//Generate random to determine if the user process is IO bound or CPU bound
	const int io_bound = ((rand() % 100) <= IO_BOUND_PROB) ? pREAL : pNORM;

	const pid_t pid = fork();

	switch(pid){
		case -1:
			fprintf(stderr,"%s: failed to fork a process.",prog);
                        perror("Error: fork failed");
			return EXIT_FAILURE;
			break;
		case 0: //Child process
			snprintf(buf, sizeof(buf), "%d", io_bound);
			execl("./user", "./user", buf, NULL);
			fprintf(stderr,"%s: failed to execl.",prog);
                        perror("Error: execl failed");
			exit(EXIT_FAILURE);
			break;
		default: //Parent process
			++reportV.usersStarted;
			if(io_bound == pREAL){
				usr->q_location = qONE;
				//reportV.c_highprior++;
			}else{
				usr->q_location = qTWO;
				//eportV.c_lowprior++;
			}

			usr->id = next_id++;
			usr->pid = pid;

			usr->t_started = shm->clock; //save started time to record system time
			usr->state = sREADY; //mark process as ready

			pushQ(qONE, u);
			++logLine;
			printf("OSS: Generating process with PID %u and putting it in queue %d at time %lu:%lu\n",
           			usr->id, qONE, shm->clock.tv_sec, shm->clock.tv_nsec);		
			break;	
	}
	
	return EXIT_SUCCESS;

}

//Add time to system clock 
static void addTime(struct timespec *a, const struct timespec *b){
	static const unsigned int max_ns = 1000000000;

  	a->tv_sec += b->tv_sec;
  	a->tv_nsec += b->tv_nsec;
  	while (a->tv_nsec > max_ns){
    		a->tv_sec++;
    		a->tv_nsec -= max_ns;
  	}
}

//Find time difference 
static void subTime(struct timespec *a, struct timespec *b, struct timespec *c){
	if (b->tv_nsec < a->tv_nsec){
    		c->tv_sec = b->tv_sec - a->tv_sec - 1;
    		c->tv_nsec = a->tv_nsec - b->tv_nsec;
  	}else{
    		c->tv_sec = b->tv_sec - a->tv_sec;
    		c->tv_nsec = b->tv_nsec - a->tv_nsec;
  	}
}

//Advance system clock 
static void advanceTimer(){
	struct timespec t = {.tv_sec = 0, .tv_nsec = 500000000};
	//Check if program has generated more than 50 processes
	if(reportV.usersStarted >= TOTAL_MAXPROC)
		addTime(&shm->clock, &t);
	else{
		if ((shm->clock.tv_sec >= n_time.tv_sec) ||((shm->clock.tv_sec == n_time.tv_sec) &&
       			(shm->clock.tv_nsec >= n_time.tv_nsec))){
    			n_time.tv_sec = rand() % maxTimeBetweenNewProcs.tv_sec;
    			n_time.tv_nsec = rand() % maxTimeBetweenNewProcs.tv_nsec;
			
    			addTime(&n_time, &shm->clock);
      			forkProcess();
  		}else{
				addTime(&shm->clock, &t);
		}
	}
}

//Initialize variables in the report
static void initReport(){
	reportV.usersStarted = 0;
	reportV.usersTerminated = 0;
	reportV.wait_time.tv_sec = 0;
	reportV.wait_time.tv_nsec = 0;
        reportV.cpu_time.tv_sec = 0;
        reportV.cpu_time.tv_nsec = 0;
       	reportV.sys_time.tv_sec = 0;
        reportV.sys_time.tv_nsec = 0;
	reportV.cpu_IdleTime.tv_sec = 0;
        reportV.cpu_IdleTime.tv_nsec = 0;
        reportV.blocked_time[qONE].tv_sec = 0;
        reportV.blocked_time[qONE].tv_nsec = 0;
	reportV.blocked_time[qTWO].tv_sec = 0;
        reportV.blocked_time[qTWO].tv_nsec = 0;
	reportV.blocked_time[qTHREE].tv_sec = 0;
	reportV.blocked_time[qTHREE].tv_nsec = 0;
}

//Print report
static void report() {
	printf("Report:");
	printf("Average wait time");
	printf("Average time in the system");
	printf("Average CPU utilization");
	printf("Average time a process waited in a blocked queue");
	printf("CPU idle time");
}

//Interrupt signal & Alarm signal handler
static void handler(int sig){
	printf("OSS: Signaled with %d\n", sig);

	//Terminate all user processes 
	struct ossMsg m;
	for (int i = 0; i < MAXPROCESSES; i++){
		if (shm->users[i].pid > 0){
      			m.timeslice = 0;
      			m.mtype = shm->users[i].pid;
      			m.from = getpid();
      			if (msgsnd(qid, (void *)&m, MESSAGE_SIZE, 0) == -1){
        			break;
      			}

			waitpid(shm->users[i].pid, NULL, 0);
      			shm->users[i].pid = 0;
		}
	}
	//Report 
	report();
	exit(1);

}

static void scheduler() {
	while(reportV.usersTerminated < TOTAL_MAXPROC){
		advanceTimer();
	        unblockUsers();
	}
}

//Main menu
static int menu (int argc,char** argv){ 
	int opt;
	while ((opt = getopt(argc, argv, "hs:l:")) > 0){
    		switch (opt){
    			case 'h':
      				 printf("%s -h: HELP MENU\n",argv[0]);
                                 printf("\nCommand:\n");
                                 printf("%s -s t       Specify maximum time\n",argv[0]);
                                 printf("%s -l f            Specify a particular name for log file\n",argv[0]);
                                 printf("\nt: Maximum time in seconds before the system terminates\n");
                                 printf("f: Filename(.mainlog)\n");
                                        
				 return -1;

    			case 's':
 	     			maxSec = atoi(optarg);
				break;
    			case 'l':
                                strcpy(mainLog,optarg);
                                strcat(mainLog,".mainlog");
				break;
			case '?':
                                if(optopt == 's' || optopt == 'l')
                                	fprintf(stderr,"%s: ERROR: -%c missing argument\n",prog,optopt);
                                else
                                        fprintf(stderr, "%s: ERROR: Unknown option: -%c\n",prog,optopt);
                                return -1;

    		}
  	}

	stdout = freopen(mainLog, "w", stdout);
	if(stdout == NULL){
		fprintf(stderr,"%s: ",prog);
		perror("Error: freopen failed");
		return -1;
	}
	return 0;
}


int main(int argc, char** argv){
	prog = argv[0];

	signal(SIGINT, handler); 
	signal(SIGALRM, handler);
	alarm(maxSec);

	if(menu(argc, argv) < 0) {
		return EXIT_FAILURE;
	}

	//Initialize variables in the report
	initReport();
	return 0;
}
