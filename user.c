#include <stdio.h>
#include <time.h>
#include <sys/time.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include "config.h"
#include <string.h>
#include <sys/shm.h>
#include <sys/sem.h>
#include <sys/ipc.h>

char *ofile= "cstest";
FILE *file, *lfile;
int shmid;
struct sharedM *shmp;
struct semaphore S;
char logfile[10]="logfile.";
char logNum[3]; //Process number

struct timeval  now;
struct tm* local;

int semID;
int semflg;			/* semflg to pass to semget() */
int nsems;			/* nsems to pass to semget() */
int semid;			// id of semephore set
int i;
int s=1;

void wait_Sem(){
	struct sembuf sop = {.sem_num = 0, .sem_flg = 0, .sem_op = -1};

	if (semop(semID, &sop, 1) == -1){
		perror("user_proc: semop");
		exit(EXIT_FAILURE);
	}
}

void signal_Sem(){
	struct sembuf sop = {.sem_num = 0, .sem_flg = 0, .sem_op = 1};

	if (semop(semID, &sop, 1) == -1){
		perror("user_proc: semop");
		exit(EXIT_FAILURE);
	}

}
void removeSharedMemory() {
        //Detach the process
         if (shmdt(shmp) == -1) {
                perror("Error: shmdt");
                exit(EXIT_FAILURE);
        }

}

//Interrupt signal (^C)
void siginit_handler() {
	removeSharedMemory();
        exit(EXIT_FAILURE);
}

void enterMessage(int n){
	gettimeofday(&now, NULL);
	local = localtime(&now.tv_sec);	

	file=fopen(ofile,"a");
	lfile=fopen(logfile,"a");

	//Write to log file
        fprintf(lfile,"%02d:%02d:%02d Process %d entered the critical section\n",local->tm_hour, local->tm_min, local->tm_sec,n);

	//Write to cstest file
        fprintf(file,"%02d:%02d:%02d File modified by process number %d\n",local->tm_hour, local->tm_min, local->tm_sec,n);
	fclose(lfile);
	fclose(file);
}

void exitMessage(int n){
        gettimeofday(&now, NULL);
        local = localtime(&now.tv_sec);
	
	file=fopen(ofile,"a");
        lfile=fopen(logfile,"a");
        
	//Write to log file
        fprintf(lfile,"%02d:%02d:%02d Process %d exited the critical section\n",local->tm_hour, local->tm_min, local->tm_sec,n);
	
	//Write to cstest file
        fprintf(file,"%02d:%02d:%02d File modified by process number %d\n",local->tm_hour, local->tm_min, local->tm_sec,n);
        fclose(lfile);
	fclose(file);
}

void process (int i) {
        int j;
	srand(time(0)+i);
	int ran=rand() % 3 + 1; //Generate a random number from 1-3
        char numP[2]; //Process number
       
	for(j = 0; j < 5; j++){	
		//Entry Section
		wait_Sem();

		//Critical section
		sleep(ran);
		enterMessage(i);

		sleep(ran);
        	exitMessage(i);

		//Exit section
		signal_Sem();
	}
}


int main(int argc, char *argv[]) {
	//Interrupt signal handler
	signal(SIGINT, siginit_handler);

	int procID=atoi(argv[0]); //Process ID
	int ordNum = atoi(argv[1]); //Process order number
	
	sprintf(logNum,"%d",ordNum);
        strcat(logfile,logNum);	
	//Shared memory ID 
	shmid=shmget(SHM_KEY, sizeof(struct sharedM), 0644);
	if (shmid == -1) {
                perror("Error:shmget");
                exit(EXIT_FAILURE);
        }

	//Attach slave process to shared memory segment
	shmp=(struct sharedM *) shmat(shmid, NULL, 0);
        if (shmp == (struct sharedM *) -1) {
                perror("Error:shmat");
                exit(EXIT_FAILURE);
        }
	
	//Attach slave process to semaphore ID
	semID=semget(key,0,0);
	if(semID < 0){
		perror("Error: failed to get id for semaphore");
		exit(EXIT_FAILURE);
	}
	process(procID);
	
  	removeSharedMemory(); 
	//removeSem();
	return 0;
}
