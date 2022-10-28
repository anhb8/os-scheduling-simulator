#ifndef CONFIG_H
#define MAX_PROCESS 20
enum state{idle,want_in,in_cs};
struct sharedM {
	int turn;
	int flag[MAX_PROCESS];
	//System clock
	int maxTimeBetweenNewProcsNS;
	int maxTimeBetweenNewProcsSecs;

};

struct processControlBlock {
        struct sharedM CPUTime;
        struct sharedM totalTime;
        struct sharedM burstTime;
	pid_t pid;
        struct sharedM priority;
};

struct semaphore {
	int count; //Number of available resources
	//int wait; //Sleeping processes that are waiting for resources
	//int sleepers; //Processes sleeping on semaphore
	struct semaphore *wait;
};


key_t key=4321;
key_t SHM_KEY=1234;
key_t SHM_PCB_KEY=3999;
key_t MSG_KEY=2121;
#endif
