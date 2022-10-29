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

//Interrupt signal & Alarm signal handler
static void handler(int sig){
	printf("OSS: Signaled with %d\n", sig);
	//Terminate all user processes (Incomplete)
	//Report (Incomplete)
	exit(1);

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

	if(menu(argc, argv) < 0) {
		return EXIT_FAILURE;
	}

	return 0;
}
