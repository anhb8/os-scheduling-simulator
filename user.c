#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/ipc.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/msg.h>
#include <sys/shm.h>
#include <strings.h>
#include <signal.h>
#include "config.h"

//File name
char *prog;

//Shared memory and message queue id
static int qid=-1;
static int sid=-1;
static struct sharedM *shm = NULL;

//Create shared memory, message queue
static int createSharedM(){
	//Attach shared memory id to the user process
	sid = shmget(sharedM_key, sizeof(struct sharedM), 0);
        if(sid < 0){
                fprintf(stderr,"%s: failed to get id for shared memory. ",prog);
                perror("Error: shmget failed");
                return -1;
        }

        shm = (struct sharedM *)shmat(sid,NULL,0);
        if(shm == (void *)-1){
                fprintf(stderr,"%s: failed to get pointer for shared memory. ",prog);
                perror("Error: shmat failed");
                return -1;
        }

        qid = msgget(queue_key,0);
        if(qid == -1){
                fprintf(stderr,"%s: failed to get id for queue. ",prog);
                perror("Error: msgget failed");
                return -1;
        }
	return 0;
}

//Deallocate shared memory 
static int deallocatesharedM(){
	if(shm != NULL){
                if(shmdt(shm) == -1){
                        fprintf(stderr,"%s: failed to detach shared memory. ",prog);
                        perror("Error: shmdt failed");
                }
        }
	return 0;
}

static void handler(){
	deallocatesharedM();
	exit(1);
}

static void userProc(int io_bound){
	int alive = 1;
	const int io_block_prob = (io_bound) ? IO_IO_BLOCK_PROB : CPU_IO_BLOCK_PROB;
	
	while(alive){
		struct ossMsg message;
		
		if (msgrcv(qid, (void *)&message, MESSAGE_SIZE, getpid(), 0) == -1){
			fprintf(stderr,"%s: failed to receive message. ",prog);
                	perror("Error");
			break;
		}	

		int timeslice = message.timeslice;
		if(timeslice == 0){
			break; // if it has use up its time slice, then exit
		}

		bzero(&message, sizeof(struct ossMsg));

		int ifTerminate = ((rand() % 100) <= TERM_PROB) ? 1 : 0;

		if(ifTerminate){
			message.timeslice = sTERMINATED;
			message.clock.tv_nsec = rand() % timeslice;
			alive = 0;
		}else{
			int ifInterrupt = ((rand() % 100) < io_block_prob) ? 1 : 0;
			if (ifInterrupt){
				message.timeslice = sBLOCKED;
				message.clock.tv_nsec = rand() % timeslice;
				message.blockedTime.tv_sec = rand() % BLOCKED_SEC;
				message.blockedTime.tv_nsec = rand() % BLOCKED_NSEC;
			}else{
				message.timeslice = sREADY;
				message.clock.tv_nsec = timeslice;
			}
		}
		
		message.mtype = getppid();
		message.from = getpid();
		if (msgsnd(qid, (void *)&message, MESSAGE_SIZE, 0) == -1){
			fprintf(stderr,"%s: failed to send message. ",prog);
                        perror("Error");
			break;
		}
	}

}
int main (int argc, char** argv) {
	prog = argv[0];

	if (argc != 2)
	{
		fprintf(stderr, "%s: Please passed in IO bound arguments.\n",prog);
		return EXIT_FAILURE;
	}
	
	signal(SIGINT, handler);
		
	const int io_bound = atoi(argv[1]);
	srand(getpid() + io_bound); //seeding off

	if(createSharedM() == -1)
		return EXIT_FAILURE;

	userProc(io_bound);

	if(deallocatesharedM() == -1)
		return EXIT_FAILURE;

	return 0;
}
