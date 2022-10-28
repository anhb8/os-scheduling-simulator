#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h> // #define
#include <ctype.h> //isdigit
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/ipc.h> //shared memory
#include <sys/shm.h> //shared memory
#include <time.h> //local time
#include <sys/time.h>
#include <sys/sem.h>
#include <sys/msg.h>
#include "config.h"

pid_t all_cProcess[MAX_PROCESS];

int shmid,shmidPCB,msqid;
struct sharedM *shmp;
struct processControlBlock *shmPCB;
static const struct sharedM clockT = {.maxTimeBetweenNewProcsNS=2000000000,.maxTimeBetweenNewProcsSecs=3};
int runTimeSecs=0,runTimeNS=0;

FILE *file; //Log file
char logfile[10]="logfile.";
char logNum[3]; //Process number
char *mainLog; //Main log file

struct timeval  now;
struct tm* local;

int n_process=-1;
int semID;

//Find empty index
int findIndex() {
	int i;
	for (i=0;i<n_process;i++) {
		if (all_cProcess[i]==0)
			return i;
	}
	return -1;
}
//Create semaphore
void createSem() {
	const unsigned short unlocked = 1;
	semID = semget(key, 1, IPC_CREAT | IPC_EXCL | S_IRUSR | S_IWUSR);
	if(semID < 0) {
    		perror("Error: semget failed");
		exit(EXIT_FAILURE);
	}

	if (semctl(semID, 0, SETVAL, unlocked) == -1){
    		perror("Error: semctl failed");
    		exit(EXIT_FAILURE);
  	}
}

//Allocate shared memory
void createSharedMemory() {
	//Create shared memory segment for system clock
	shmid=shmget(SHM_KEY, sizeof(struct sharedM), 0644|IPC_CREAT);
	if (shmid == -1) {
      		perror("Error: shmget failed");
      		exit(EXIT_FAILURE);
   	}
	
	//Create shared memory segment for process control block
	shmidPCB=shmget(SHM_PCB_KEY,sizeof(struct sharedM), 0644|IPC_CREAT);
	if (shmidPCB == -1) {
                perror("Error: shmget failed");
                exit(EXIT_FAILURE);
        }

	//Create message queue
	if ((msqid = msgget(MSG_KEY, 0666 | IPC_CREAT)) == -1) {
      		perror("Error: msgget");
      		exit(EXIT_FAILURE);
   	}
}

//Attach the process to shared memory segment just created - pointer
void *attachSharedMemory() {
	//System clock
	shmp=(struct sharedM *) shmat(shmid, NULL, 0);
	if (shmp == (struct sharedM *) -1) {
      		perror("Error:shmat");
      		exit(EXIT_FAILURE);
   	}	
	
	//Process control block
	shmPCB=(struct processControlBlock *) shmat(shmidPCB, NULL, 0);
        if (shmPCB == (struct processControlBlock *) -1) {
                perror("Error:shmat");
                exit(EXIT_FAILURE);
	}
}


//Deallocate shared memory
void removeSharedMemory() {
	//Detach the process
	 if (shmdt(shmp) == -1) {
      		perror("Error: shmdt failed");
      		exit(EXIT_FAILURE);
   	}
	
	 if (shmdt(shmPCB) == -1) {
                perror("Error: shmdt failed");
                exit(EXIT_FAILURE);
        } 

	//Remove shared memory segment
	if (shmctl(shmid, IPC_RMID, 0) == -1) {
      		perror("Error: shmctl failed");
     	 	exit(EXIT_FAILURE);
  	}
	
	if (shmctl(shmidPCB, IPC_RMID, 0) == -1) {
                perror("Error: shmctl failed");
                exit(EXIT_FAILURE);
        }

	//Remove semaphore
	if (semctl(semID, 0, IPC_RMID) == -1) {
                perror("Error: semctl failed");
                exit(EXIT_FAILURE);
        }

	//Remove message queue
	if(msgctl(msqid, IPC_RMID, NULL) == -1){
                perror("Error: msgctl failed");
		exit(EXIT_FAILURE);
	}
}

//Create random time for system clock to launch new user process
void createTime () {
	runTimeSecs = rand() % clockT.maxTimeBetweenNewProcsSecs;
	runTimeNS = rand() % clockT.maxTimeBetweenNewProcsNS;
}

void logTermination(pid_t p){ 
        file=fopen(mainLog,"a");
        gettimeofday(&now, NULL);
        local = localtime(&now.tv_sec);
        fprintf(file,"OSS: Generating process with PID %d and putting in in queue at time %d:%d \n",p,runTimeSecs,runTimeNS);
        fclose(file);
}

//Interrupt signal (^C) 
void siginit_handler () {
	for (int i=0; i<n_process; i++) {
		if(all_cProcess[i] != 0){ 
                        kill(all_cProcess[i],SIGTERM); 
                        logTermination(all_cProcess[i]);
                }  
	}
	removeSharedMemory();
	exit(EXIT_FAILURE);
}


//Signal when the program runs more than time limit
void alarm_handler () {
	fprintf(stderr,"Error: Exceed time limit\n");
	
	//Kill all processes if exceeds time limit
	for (int i=0; i<n_process; i++) {
		if(all_cProcess[i] != 0){
			kill(all_cProcess[i],SIGTERM); 
			logTermination(all_cProcess[i]);
		}
        } 
        removeSharedMemory();
	exit(EXIT_FAILURE);	
}

int IDtoIndex(pid_t p){
	int i;
	for(i = 0; i<n_process;i++){
		if(all_cProcess[i] == p)
			return i;
	}
	return -1;
}

void forkProcess(int n_process) {
	int i;
	int availSpot = n_process;
	int totalProc = 0;
	pid_t p;
	
	//Create user process at random interavals of simulated time
	createTime();
        printf("%d",runTimeNS);

	//while(1){
		pid_t pid = fork();
		if (pid<0) {
			perror("Error: Fork failed");
			removeSharedMemory();
			exit(EXIT_FAILURE);
			
		} else if (pid == 0) {  
			//Child process
			totalProc++;
			char procID[20] = {"\0"};
			char ordNum[10] = {"\0"};
			pid_t p = getpid();

			sprintf(procID,"%d",p);
			sprintf(ordNum,"%d",totalProc);
			
			execl("user",procID,ordNum,NULL);
			
			//if execl failed
			perror("Error: execl failed ");
			exit(EXIT_FAILURE);
		} else {			//Parent process
			totalProc++;
			availSpot--;;
			int index=findIndex();
			all_cProcess[index] = pid;
			

			if (availSpot == 0) {
				p = wait(NULL);
				logTermination(p);
				int index = IDtoIndex(p);
			       	if(index == -1){
					fprintf(stderr,"Error: Can't find process ID in the list\n");
					siginit_handler();
				}	
				all_cProcess[index] = 0;
				availSpot++;
			} else {
				if((p = waitpid(-1, NULL, WNOHANG)) != 0){
					logTermination(p);
                                	int index = IDtoIndex(p);
                           	     	if(index == -1){
                                        	fprintf(stderr,"Error: Can't find process ID in the list\n");
                                        	siginit_handler();
                               	 	}
                                	all_cProcess[index] = 0;
                                	availSpot++;
				}	
			}

		}
	//}

	while((p = wait(NULL)) > 0) {
		logTermination(p);
	}	
}

int validNum(char* sec){
	int size = strlen(sec);
	int i = 0;
	while(i < size){
		if(!isdigit(sec[i]))
			return 0;
		i++;
	}
	return 1;
}

int main(int argc, char *argv[])
{
	int maxSec=100;
	int option;
	FILE *file;	
	signal(SIGINT, siginit_handler);
	
	//Allocate memory for main logfile
	mainLog=(char *) malloc(sizeof(char));
	strcpy(mainLog,"logfile"); //Default name

	while(optind < argc){
		if ((option = getopt(argc,argv, "hs:l:")) !=-1) {
			switch(option){
				case 'h':
					printf("%s -h: HELP MENU\n",argv[0]);
                                        printf("\nCommand:\n");
					printf("%s -s t	      Specify maximum time\n",argv[0]);
					printf("%s -l f            Specify a particular name for log file\n",argv[0]);
                                        printf("\nt: Maximum time in seconds before the system terminates\n");
                                        printf("f: Filename(.mainlog)\n");
					exit(EXIT_FAILURE);

				case 's':
					if (strcmp(optarg,"-l")==0) {
                                                fprintf(stderr,"%s: ERROR: %s missing argument\n",argv[0],optarg);
                                                exit(EXIT_FAILURE);
                                        }

					if(validNum(optarg)){
						maxSec = atoi(optarg);
					}else{
						fprintf(stderr,"%s: ERROR: %s is not a valid number\n",argv[0],optarg);
						return EXIT_FAILURE;
					}
					break;

					
				case 'l':
					if (strcmp(optarg,"-s")==0) {
						fprintf(stderr,"%s: ERROR: %s missing argument\n",argv[0],optarg);          
						exit(EXIT_FAILURE);
					}

					strcpy(mainLog,optarg);	
					strcat(mainLog,".mainlog");
					break;
				case '?':
					if(optopt == 's' || optopt == 'l')
						fprintf(stderr,"%s: ERROR: -%c missing argument\n",argv[0],optopt);
					else 
						fprintf(stderr, "%s: ERROR: Unknown option: -%c\n",argv[0],optopt);
					return EXIT_FAILURE;
			}
		} 
	}
	
	signal(SIGALRM, alarm_handler);
        alarm(maxSec);

	
	//MAIN CODE
	createSharedMemory();
        attachSharedMemory(); 
	createSem();	
	
	forkProcess(n_process);
	removeSharedMemory();
	
	//printf("%s",mainLog);
	return 0;
}
