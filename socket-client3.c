#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <netdb.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <arpa/inet.h>
#include <time.h>
#include <pthread.h>
#include <signal.h>

int sockfd = 0;
int n = 0;
char recvBuff[1024];

// script
char * script_to_run;

int idle_pause_time = 10; // 10 secs
int isSameCycle = 0;
int gcEnd = 0;
int gcCount = 0;

#define GC_START "GCStart:"
#define GC_END "GCEnd:"

pthread_mutex_t mtex=PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t cvar=PTHREAD_COND_INITIALIZER;

void* threadHandler(void* arg) 
{
    struct timespec wtime;
    int rc;
    pid_t pid;

    printf("Thread started. Monitoring gc pause time\n");
    while(gcCount == 1) {
	clock_gettime(CLOCK_REALTIME, &wtime);
	wtime.tv_sec += 2; /* wait for 2 seconds */

	rc = pthread_cond_timedwait(&cvar, &mtex, &wtime);
	if ( rc == ETIMEDOUT && gcCount == 1 ) {
		fprintf(stderr, "Pause time exceed our limit : probable high pause GC cycle\n");
                isSameCycle = 1;
		/* fork & exec(command) */ 
                pid = fork();
                if ( pid == 0 ) { 
                    execvp(script_to_run, NULL);
                }
	} else if ( rc == 0 && gcCount == 0 ) {
		if(isSameCycle) {
                    isSameCycle = 0;
                    fprintf(stderr, "HighGC Pause. Logs Has been collected for High GC Pause.\n");
                } else {
                    fprintf(stderr, "GC completed within time\n");
                }
		break;
	} else {
		fprintf(stderr, "GC start pthread_cond_timedwait failed, err:%d\n", rc);
		break;
	}
    }
    pthread_mutex_unlock(&mtex);
}

void readSocket() 
{
    int rc = 0; 
    pthread_t tid;

    gcCount = 0;
    while((n = read(sockfd, recvBuff, sizeof(recvBuff)-1)) > 0)
    {
        recvBuff[n] = 0;
        printf("%s", recvBuff);
        if(strncmp(recvBuff, GC_START, strlen(GC_START)) == 0) {
	    pthread_mutex_lock(&mtex);
            printf("GC Cycle[%d] started\n", gcCount);
	    if (gcCount == 0) {
		gcCount++;
                if ((rc = pthread_create(&tid, NULL, &threadHandler, NULL)) != 0) {
	        	pthread_mutex_unlock(&mtex);
			fprintf(stderr, "Failed to create GC monitor thread, err:%d\n", rc);
		}
	    } else {
		pthread_mutex_unlock(&mtex);
		fprintf(stderr, "WARNING: Possible mismatch of GC Start & End\n");
	    }
        } else if(strncmp(recvBuff, GC_END, strlen(GC_END)) == 0) {
	    pthread_mutex_lock(&mtex);
            printf("GC Cycle[%d] Ended\n", gcCount);
	    if (gcCount == 1) {
		gcCount--;
		pthread_cond_signal(&cvar);
	    } else {
		fprintf(stderr, "WARNING: Possible mismatch of GC End & Start\n");
	    }
	    pthread_mutex_unlock(&mtex);
        }
    }
    fprintf(stderr, "WARNING: readSocket exits\n");
}

// interrupt handler
void intHandler(int dummy) 
{
    printf("Exiting.. Closing all the connections\n");
    if(sockfd > 0)
        close(sockfd);
    exit(0);
}

// Main Method
int main(int argc, char ** argv) 
{
    struct sockaddr_in serv_addr;
    int retry_count = 0;
    int isConnected = 0;

    if(argc < 3) {
        printf("Usage: ./gcpausemonitor [idle_pause_time] [script_to_run]\n\n");
        printf("For example: \n");
        printf("If idle_pause_time is 10 secs & script_to_run is collectLogs.sh\n");
        printf("./gcpausemonitor 10 collectLogs.sh \n\n");
        exit(-1);
    }

    // process command line args
    idle_pause_time = atoi(argv[1]);
    script_to_run = argv[2];    
    printf("Idle pause time %d\n", idle_pause_time); 
    printf("Script to run: %s\n\n", script_to_run); 

    // install signal handler
    signal(SIGINT, intHandler);

    if((sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0)
    {
        printf("Error : Could not create socket\n");
        return 1;
    }

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(5250);
    serv_addr.sin_addr.s_addr = inet_addr("127.0.0.1");

    while(retry_count < 10) {
        if(connect(sockfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
            printf("Error : Connection Failed. Agent not running. Retrying\n");
        } else {
            isConnected = 1;
            break;
        }
        retry_count++;
        sleep(3);
    }
    readSocket();
    return 0;
}
