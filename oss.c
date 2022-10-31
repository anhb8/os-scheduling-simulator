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

char *prog;
char mainLog[256] = "log.mainlog";
static int maxSec = MAXSECONDS;

//System clock
static const struct timespec maxTimeBetweenNewProcs = {.tv_sec = 2, .tv_nsec = 1000000000};
static struct timespec n_time = {.tv_sec = 0, .tv_nsec = 0};

//Shared memory and message queue id
static int qid=-1;
static int sid=-1;
static struct sharedM *shm = NULL;

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

	return 0;

}

//Initialize variables in the report
struct ossReport reportV;
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
		//Write 
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
