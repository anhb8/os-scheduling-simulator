#ifndef CONFIG_H
#define MAX_PROCESS 20
enum state{idle,want_in,in_cs};
struct sharedM {
	int turn;
	int flag[MAX_PROCESS];
	//System clock
	int maxTimeBetweenNewProcsNS,maxTimeBetweenNewProcsSecs;
	int CPUTime;
	int totalTime;
	int burstTime;
	int priority;

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
#endif
