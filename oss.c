/* 
 * File:   oss.c
 * Author: Michael Beckering
 * Project 3
 * Spring 2018 CS-4760-E01
 * Created on February 28, 2018, 9:28 AM
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <signal.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <time.h>

#define BILLION 1000000000L
#define SHMKEY_sim_s 0420423
#define SHMKEY_sim_ns 0420145
#define BUFF_SZ sizeof (int)

//function prototype declarations
void clearShm();
static void helpmessage();
static int setperiodic(double);
static int setinterrupt();
static void interrupt(int signo, siginfo_t *info, void *context);
static void siginthandler(int sig_num);

pid_t childpids[25];
int maxSlaves = 5; //number of slave processes, default 5
static FILE *log; //master log file pointer
int shmid_sim_s, shmid_sim_ns; //shared memory ID holders for sim clock

/*
 * 
 */
int main(int argc, char** argv) {
    int hflag = 0; int sflag = 0; //getopt flags
    int lflag = 0; int tflag = 0; //getopt flags
    int i; //incrementer
    int sh_status; //status holder for wait
    extern char *optarg; //getopt arguments
    int option; //getopt int
    double runtime = 20.0; //time before master terminates, default 20
    char logfilename[50]; //string for name of log file
    char str_proclimit[10]; //string arg for exec-ing slaves
    char str_slavenum[10]; //string arg for exec-ing slaves
    pid_t childpid, sh_wpid; //pid holders
    
     //getopt loop to parse command line options
    while ((option = getopt(argc, argv, "hs:t:l:")) != -1) {
        switch(option) {
            case 'h':
                hflag = 1;
                helpmessage();
                break;
            case 's':
                sflag = 1;
                maxSlaves = atoi(optarg);
                if ( (maxSlaves < 1) || (maxSlaves > 18) ) {
                    printf("Master: Error: -s range is 1 to 18.\n");
                    exit(0);
                }
                break;
            case 'l':
                lflag = 1;
                sprintf(logfilename, optarg);
                break;
            case 't':
                tflag = 1;
                runtime = atoi(optarg);
                break;
            default:
                break;
        }
    }
    
    if (sflag)
        printf("Master: Slave process limit set to %d.\n", maxSlaves);
    else
        printf("Master: Using default of %d slave processes.\n", maxSlaves);
    
    if (lflag) {
        strcat(logfilename, ".log");
        printf("Master: Log file name set to %s\n", logfilename);
    }
    else {
        sprintf(logfilename, "master.log");
        printf("Master: Using default log file name %s\n", logfilename);
    }
    
    if (tflag)
        printf("Master: Runtime limit set to %2.1f seconds.\n", runtime);
    else
        printf("Master: Using default runtime limit of %2.1f seconds.\n", runtime);
    
    // Set up interrupt handler
    signal (SIGINT, siginthandler);
    if (setinterrupt() == -1) {
        perror("Failed to set up SIGALRM handler");
        return 1;
    }
    // Set up periodic timer
    if (setperiodic(runtime) == -1) {
        perror("Failed to setup periodic interrupt");
        return 1;
    }
    
    // Set up shared memory
    shmid_sim_s = shmget(SHMKEY_sim_s, BUFF_SZ, 0777 | IPC_CREAT);
        if (shmid_sim_s == -1) { //terminate if shmget failed
            perror("Master: error in shmget shmid_sim_s");
            return 1;
        }
    int *sim_s = (int*) shmat(shmid_sim_s, 0, 0);
    
    shmid_sim_ns = shmget(SHMKEY_sim_ns, BUFF_SZ, 0777 | IPC_CREAT);
        if (shmid_sim_ns == -1) { //terminate if shmget failed
            perror("Master: error in shmget shmid_sim_ns");
            return 1;
        }
    int *sim_ns = (int*) shmat(shmid_sim_ns, 0, 0);
    
    //testing shm
    *sim_s = 2;
    printf("Master: sim_s = %d\n", *sim_s);
    
    *sim_ns = 1000000;
    printf("Master: sim_ns = %d\n", *sim_ns);
    
    
    
    //BEGIN MEAT OF PROGRAM*****************************************************
    
    //while (sim_clock_s < 2)
    for (i=0; i<maxSlaves; i++) {
        if ( (childpid = fork()) < 0 ){ //terminate code
                perror("Error forking consumer");
                return 1;
            }
        if (childpid == 0) { //child code
            sprintf(str_slavenum, "%d", i); //build arg2 string
            sprintf(str_proclimit, "%d", maxSlaves); //build arg1 string
            execlp("./user", "./user", str_slavenum, str_proclimit, (char *)NULL);
            perror("execl() failure"); //report & exit if exec fails
            return 0;
        }
        printf("Master: Slave forked.\n");
        //wait for message from terminating child (includes logical slave#?)
        //critical section: increment simulaton clock, write to log, 
    }
    
    
    //END MEAT OF PROGRAM*******************************************************
    
    //wait for children to finish
    printf("Master: Waiting for children to finish...\n");
    while ( (sh_wpid = wait(&sh_status)) > 0);

    //clear shared memory and exit
    clearShm();
    printf("Master: Normal exit.\n");
    return 1;
}

//print usage message and exit
static void helpmessage() {
    printf("Usage: ./oss [ -s <number 1-18> ] [ -l <filename> ] [ -t <number> ] [ -h ]\n");
    printf("s: number of slave processes to run (max 18). l: logfile name (.log extension added automatically).\n");
    printf("t: time limit in seconds before master terminates. h: help\n");
    exit(0);
}

//function to clear shared memory
void clearShm() {
    printf("Master: Clearing shared memory...\n");
    if ( shmctl(shmid_sim_s, IPC_RMID, NULL) == -1) {
        perror("error removing shared memory");
    }
    if ( shmctl(shmid_sim_ns, IPC_RMID, NULL) == -1) {
        perror("error removing shared memory");
    }
}

//this function taken from UNIX text
static int setperiodic(double sec) {
    timer_t timerid;
    struct itimerspec value;
    
    if (timer_create(CLOCK_REALTIME, NULL, &timerid) == -1)
        return -1;
    value.it_interval.tv_sec = (long)sec;
    value.it_interval.tv_nsec = (sec - value.it_interval.tv_sec)*BILLION;
    if (value.it_interval.tv_nsec >= BILLION) {
        value.it_interval.tv_sec++;
        value.it_interval.tv_nsec -= BILLION;
    }
    value.it_value = value.it_interval;
    return timer_settime(timerid, 0, &value, NULL);
}

//this function taken from UNIX text
static int setinterrupt() {
    struct sigaction act;
    
    act.sa_flags = SA_SIGINFO;
    act.sa_sigaction = interrupt;
    if ((sigemptyset(&act.sa_mask) == -1) ||
            (sigaction(SIGALRM, &act, NULL) == -1))
        return -1;
    return 0;
}

static void interrupt(int signo, siginfo_t *info, void *context) {
    int sh_status, i;
    pid_t sh_wpid;
    printf("master: Timer Interrupt Detected! signo = %d\n", signo);
    printf("master: Killing children...\n");

    /*
    for (i=0; i < maxSlaves ; i++) {
        kill(childpids[i], SIGINT);
    }
    */
    
    //wait for all children to finish
    while ( (sh_wpid = wait(&sh_status)) > 0);
    
    printf("master: All children terminated\n");
    clearShm();
    
    //close log file
    //if (fclose(log) != 0);
    //    perror("Master: Error closing log file");
        
    printf("Master: Terminated: Timed Out\n");
    exit(0);
}

static void siginthandler(int sig_num) {
    int sh_status, i;
    pid_t sh_wpid;
    printf("Master: Ctrl+C interrupt detected! signo = %d\n", sig_num);
    printf("Master: Killing children...\n");
    
    /*
    for (i=0; i < maxSlaves; i++) {
        kill(childpids[i], SIGINT);
    } 
    */

    //wait for all children to finish
    while ( (sh_wpid = wait(&sh_status)) > 0);
    
    //if (fclose(log) != 0) ;
    //perror("Master: Error closing log file");
    
    printf("Master: All children terminated\n");
    clearShm();
    
    printf("Master: Terminated: Interrupted\n");
    exit(0);
}