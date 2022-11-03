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

static int qREADY = qONE; //initialize current ready queue as real time process

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

	shm->users[index].t_startWait.tv_sec = shm->clock.tv_sec;
	shm->users[index].t_startWait.tv_nsec = shm->clock.tv_nsec;

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
	int qSTART;
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
				usr->priority = pREAL;
				qSTART = qONE;
				reportV.c_highprior++;
			}else{
				usr->q_location = qTWO;
				usr->priority = pNORM;
				qSTART = qTWO;
				reportV.c_lowprior++;
			}

			usr->id = next_id++;
			usr->pid = pid;

			usr->t_started = shm->clock; //save started time to record system time
			usr->state = sREADY; //mark process as ready

			pushQ(qSTART, u);
			++logLine;
			printf("OSS: Generating process with PID %u and putting it in queue %d at time %lu:%lu\n",
           			usr->id, qSTART, shm->clock.tv_sec, shm->clock.tv_nsec);		
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

//Divide time
static void divTime(struct timespec *a, const int d){
  	a->tv_sec /= d;
  	a->tv_nsec /= d;
}
//Advance system clock 
static void advanceTimer(){
	struct timespec t = {.tv_sec = 0, .tv_nsec = 300000000};
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
	reportV.c_highprior = 0;
	reportV.c_lowprior = 0;
	reportV.usersStarted = 0;
	reportV.usersTerminated = 0;
	reportV.t_wait.tv_sec = 0;
	reportV.t_wait.tv_nsec = 0;
        reportV.t_cpu.tv_sec = 0;
        reportV.t_cpu.tv_nsec = 0;
       	reportV.t_sys.tv_sec = 0;
        reportV.t_sys.tv_nsec = 0;
	reportV.cpuIdleTime.tv_sec = 0;
        reportV.cpuIdleTime.tv_nsec = 0;
        reportV.t_blocked[qONE].tv_sec = 0;
        reportV.t_blocked[qONE].tv_nsec = 0;
	reportV.t_blocked[qTWO].tv_sec = 0;
        reportV.t_blocked[qTWO].tv_nsec = 0;
	reportV.t_blocked[qTHREE].tv_sec = 0;
	reportV.t_blocked[qTHREE].tv_nsec = 0;
}

//Print report
static void report() {
	divTime(&reportV.t_cpu, reportV.usersTerminated);
        divTime(&reportV.t_wait, reportV.usersTerminated);
        divTime(&reportV.t_sys, reportV.usersTerminated);
        divTime(&reportV.t_blocked[pREAL], reportV.c_highprior);
        divTime(&reportV.t_blocked[pNORM], reportV.c_lowprior);
        printf("*****************************************************\n");
        printf("\t\tSCHEDULING REPORT\n");
        printf("Processes Statistic:\n");
        printf("\tNumber of started processes: %d\n", reportV.usersStarted);
        printf("\tNumber of terminated processes: %d\n", reportV.usersTerminated);
        printf("\tNumber of IO bound processes: %d\n", reportV.c_highprior);
        printf("\tNumber of CPU bound processes: %d\n", reportV.c_lowprior);
        printf("\tCPU idle time: %lu:%lu\n",reportV.cpuIdleTime.tv_sec,reportV.cpuIdleTime.tv_nsec);
        printf("Average Records:\n");
        printf("\tAverage CPU utilization time: %lu:%lu\n",reportV.t_cpu.tv_sec,reportV.t_cpu.tv_nsec);
        printf("\tAverage wait time: %lu:%lu\n",reportV.t_wait.tv_sec,reportV.t_wait.tv_nsec);
        printf("\tAverage time in the system: %lu:%lu\n",reportV.t_sys.tv_sec,reportV.t_sys.tv_nsec);
        printf("\tAverage blocked time:\n");
        printf("\t\tIO Bound Processes: %lu:%lu\n",reportV.t_blocked[pREAL].tv_sec,reportV.t_blocked[pREAL].tv_nsec);        
        printf("\t\tCPU Bound Processes: %lu:%lu\n",reportV.t_blocked[pNORM].tv_sec,reportV.t_blocked[pNORM].tv_nsec);
        printf("*****************************************************\n");

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

static void clearUserPCB(int u){
	struct timespec res;
  	struct userPCB *usr = &shm->users[u];

  	++reportV.usersTerminated;
	//record the CPU time
	addTime(&reportV.t_cpu,&usr->t_cpu);	

	//get the system time
	subTime(&usr->t_started, &shm->clock, &usr->t_sys);
	addTime(&reportV.t_sys, &usr->t_sys);

	//get wait time
	subTime(&usr->t_cpu, &usr->t_sys, &res);
	addTime(&reportV.t_wait, &res);	

	waitpid(shm->users[u].pid, NULL, 0);
	bzero(usr, sizeof(struct userPCB)); //Clear all data
	
	usr->state = sNEW;
	markSlot(u); //mark the slot as free

}

static int scheduler(){
	struct ossMsg message;
	static struct timespec dispatch = {.tv_sec = 0, .tv_nsec = 0};
	dispatch.tv_nsec = rand() % (MAXDISPATCH - MINDISPATCH + 1) + MINDISPATCH;
	addTime(&shm->clock, &dispatch);

	//qREADY is currently assigned from this function's parent
	const int u = popQ(&pq[qREADY], 0);

	struct userPCB *usr = &shm->users[u];
	bzero(&message, sizeof(struct ossMsg));

	++logLine;
  	printf("OSS: Dispatching process with PID %u from queue %d at time %lu:%lu\n", usr->id, qREADY, shm->clock.tv_sec, shm->clock.tv_nsec);

	++logLine;
  	printf("OSS: total time this dispatch was %lu nanoseconds\n", dispatch.tv_nsec);

	//Evaluate if the popped queue is either high or low priority
	int TIMESLICE;
	if(qREADY == qONE)
		TIMESLICE = Q1_TIMESLICE;
	else if(qREADY == qTWO)
		TIMESLICE = Q2_TIMESLICE;
	else
		TIMESLICE = Q3_TIMESLICE;
	message.timeslice = TIMESLICE;
  	message.mtype = usr->pid;
  	message.from = getpid();

	//Send message to the queue
	if ((msgsnd(qid, (void *)&message, MESSAGE_SIZE, 0) == -1) ||
      		(msgrcv(qid, (void *)&message, MESSAGE_SIZE, getpid(), 0) == -1)){
    		fprintf(stderr,"%s: Couldn't send and receive message. ",prog);
		perror("Error");
    		return -1;
  	}

	const int new_state = message.timeslice;
	switch (new_state){
		case sREADY:
			usr->state = sREADY;

			//Getting burst time
			usr->t_burst.tv_sec = 0;
    			usr->t_burst.tv_nsec = message.clock.tv_nsec;

			//add burst time
			addTime(&shm->clock, &usr->t_burst);

			//update how long user ran on cpu
			addTime(&usr->t_cpu, &usr->t_burst);


			++logLine;
    			printf("OSS: Receiving that process with PID %u ran for %lu nanoseconds\n", usr->id, usr->t_burst.tv_nsec);
			
			if(usr->q_location == qONE)
				usr->q_location = qTWO;
			else if(usr->q_location == qTWO)
				usr->q_location = qTHREE;

			++logLine;
    			printf("OSS: Putting process with PID %u into end of queue %d\n", usr->id, usr->q_location);
    			pushQ(usr->q_location, u);
			break;
		case sBLOCKED:
    			usr->state = sBLOCKED;

			usr->t_burst.tv_sec = 0;
    			usr->t_burst.tv_nsec = message.clock.tv_nsec;
			addTime(&shm->clock, &usr->t_burst);
			addTime(&usr->t_cpu, &usr->t_burst);

			//Get blocked time from the message from the child process
			usr->t_blocked.tv_sec = message.blockedTime.tv_sec;
    			usr->t_blocked.tv_nsec = message.blockedTime.tv_nsec;

			//Update blocked time in the OSS Report
			addTime(&reportV.t_blocked[usr->priority], &message.blockedTime);

			addTime(&usr->t_blocked, &shm->clock); //add clock to wait time

			++logLine;
                        printf("OSS: Receiving that process with PID %u ran for %lu nanoseconds\n", usr->id, usr->t_burst.tv_nsec);

			if (message.clock.tv_nsec != TIMESLICE){
                                ++logLine;
                                printf("OSS: Process with PID %u not using its entire quantum\n",usr->id);
                        }

			++logLine;
                        printf("OSS: Receiving that process with PID %u is blocking for event(%li:%li)\n",
                                usr->id, usr->t_blocked.tv_sec, usr->t_blocked.tv_nsec);

			//insert to blocked queue
			++logLine;
    			printf("OSS: Putting process with PID %u into blocked queue %d\n", usr->id, qBLOCKED);
    			pushQ(qBLOCKED, u);
    			break;
		case sTERMINATED:
			usr->state = sTERMINATED;

			usr->t_burst.tv_sec = 0;
    			usr->t_burst.tv_nsec = message.clock.tv_nsec;
			addTime(&shm->clock, &usr->t_burst);
			addTime(&usr->t_cpu, &usr->t_burst); //add burst to total cpu time


			++logLine;
                        printf("OSS: Receiving that process with PID %u ran for %lu nanoseconds\n", usr->id, usr->t_burst.tv_nsec);

                        if (message.clock.tv_nsec != TIMESLICE){
                                ++logLine;
                                printf("OSS: Process with PID %u not using its entire quantum\n", usr->id);
                        }

			++logLine;
    			printf("OSS: Receiving that process with PID %u has terminated\n", usr->id);

			clearUserPCB(u);
			break;
		default:
			printf("OSS: Invalid response from user\n");
    			break;
	}
	return 0;

}

static int runChildProcess(){
	//static struct timespec t_idle = {.tv_sec = 0, .tv_nsec = 0};
	static struct timespec t_idle;
	static struct timespec diff_idle;
	int flag = 0;

	if(pq[qONE].length == 0){
		if(flag == 0){
			++logLine;
			printf("OSS: No ready process in queue %d at %li:%li\n", qONE, shm->clock.tv_sec, shm->clock.tv_nsec);
			flag = 1;
		}
       	}else{
		qREADY = qONE;
		flag = 0;
		if(t_idle.tv_sec != 0 && t_idle.tv_nsec != 0){
			subTime(&t_idle, &shm->clock, &diff_idle);
			addTime(&reportV.cpuIdleTime, &diff_idle);
			t_idle.tv_sec = 0;
                	t_idle.tv_nsec = 0;
		}
	}

	//which means the queue one is currently empty
	if(flag == 1){
		if(pq[qTWO].length == 0){
			++logLine;
                	printf("OSS: No ready process in queue %d at %li:%li\n", qTWO, shm->clock.tv_sec, shm->clock.tv_nsec);
			flag = 2;
		}else{
			qREADY = qTWO;
			flag = 0;
			if(t_idle.tv_sec != 0 && t_idle.tv_nsec != 0){
	                        subTime(&t_idle, &shm->clock, &diff_idle);
        	                addTime(&reportV.cpuIdleTime, &diff_idle);
                	        t_idle.tv_sec = 0;
                        	t_idle.tv_nsec = 0;
                	}
		}

	}

	//which means the queue two is currently empty
	if(flag == 2){
		if(pq[qTHREE].length == 0){
                        ++logLine;
                        printf("OSS: No ready process in queue %d at %li:%li\n", qTHREE, shm->clock.tv_sec, shm->clock.tv_nsec);
                        flag = 3;
                }else{
                        qREADY = qTHREE;
                        flag = 0;
                        if(t_idle.tv_sec != 0 && t_idle.tv_nsec != 0){
                                subTime(&t_idle, &shm->clock, &diff_idle);
                                addTime(&reportV.cpuIdleTime, &diff_idle);
                                t_idle.tv_sec = 0;
                                t_idle.tv_nsec = 0;
                        }
                }

        }

	//which means all ready queues are empty at the moment
	if(flag == 3){
		if(pq[qBLOCKED].length == 0 && reportV.usersTerminated >= TOTAL_MAXPROC){
                        ++logLine;
                        printf("OSS: No blocked process in queue %d at %li:%li\n", qBLOCKED, shm->clock.tv_sec, shm->clock.tv_nsec);
                        ++logLine;
                        printf("OSS: The operating system will terminate right away\n");
                        return -1; //return -1 to let OSS know there isn't any more process to schedule
		}else{
			if(t_idle.tv_sec == 0 && t_idle.tv_nsec == 0)
				t_idle = shm->clock;
			return 0;
		}
	}

	scheduler();	
	return 0;
}

static int checkStarve(){
	int i,j;
	static struct timespec workTime = {.tv_sec = 0, .tv_nsec = 0};
	for(i = qTWO; i <= qTHREE; i++){
		for(j = 0; j < pq[i].length; j++){
			int id = pq[i].ids[j]; //get process ID in PCB from the queue
			int max_wait = i == qTWO ? MAXWAIT_Q2 : MAXWAIT_Q3;

			struct timespec timediff;
			subTime(&shm->users[id].t_startWait, &shm->clock, &timediff);
			if(timediff.tv_sec >= max_wait){
				++logLine;
				printf("OSS: Process with PID %u has been waiting on queue %d for %lu:%lu\n",
						shm->users[id].id,shm->users[id].q_location, timediff.tv_sec, timediff.tv_nsec);

				//If the wait time is too long, pop this process out of current queue
				popQ(&pq[i],j);
	
				//And add it on the first queue
				pushQ(qONE,id);

				//record time for this operation
				workTime.tv_nsec = rand() % MAXDISPATCH;
				addTime(&shm->clock, &workTime);

				++logLine;
  				printf("OSS: Pop process with PID %u from queue %d and move it to queue %d\n", shm->users[id].id, shm->users[id].q_location, qONE);

				++logLine;
  				printf("OSS: Total time for this operation was %lu nanoseconds\n", workTime.tv_nsec);
			}	
		}	
	}
	
	return 0;
}

static void runningCycle() {
	while(reportV.usersTerminated < TOTAL_MAXPROC){
		advanceTimer();
	        checkStarve(); //check queue 2 and queue 3 if there is any starve process
		unblockUsers();
		if(runChildProcess() == -1) //return -1 when there is no more process to schedule
			break;
		checkLog();
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

	srand(getpid());

	if(menu(argc, argv) < 0) 
		return EXIT_FAILURE;
	
	
	if(createSharedM() < 0)
		return EXIT_FAILURE;

	//clear shared memory, bitmap, and queues
	bzero(shm, sizeof(struct sharedM));
	bzero(pq, sizeof(struct queue)*qCOUNT);

	alarm(maxSec);
	atexit(deallocatesharedM);

	//Initialize variables in the report
	initReport();
	runningCycle();

	report();

	return 0;
}
