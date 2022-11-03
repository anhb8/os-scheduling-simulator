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

int main () {
	printf("Hello World");
	return 0;
}
