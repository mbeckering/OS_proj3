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
#include <sys/msg.h>

#define SHMKEY_sim_s 0420423
#define SHMKEY_sim_ns 0420145
#define BUFF_SZ sizeof (int)
#define mutexkey 0420323
#define commskey 0420541
#define BILLION 1000000000 //dont want to type the wrong # of zeroes

// Function prototype declarations
void killchildren();
void clearIPC();
static void helpmessage();
static int setperiodic(double);
static int setinterrupt();
static void interrupt(int signo, siginfo_t *info, void *context);
static void siginthandler(int sig_num);

// GLOBALS
pid_t childpids[25]; //pid array for child processes so we can kill them later
int maxSlaves = 5; //number of slave processes, default 5
static FILE *mlog; //master log file pointer
int shmid_sim_s, shmid_sim_ns; //shared memory ID holders for sim clock
int mutex_qid, comms_qid; //message queue id's

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
    int totalforks = 0;
    int localsec;
    int localns;
    int temp;
    
    // Set up ctrl^c interrupt handling
    signal (SIGINT, siginthandler);
    if (setinterrupt() == -1) {
        perror("Failed to set up SIGALRM handler");
        return 1;
    }
    
    printf("Master: mypid=%ld\n", getpid());
    
     //struct for mutex enforcement message queue
    struct mutexbuf {
        long mtype;
        char msgtxt[10];
    };
    struct mutexbuf mutexmsg;
    mutexmsg.mtype = 1;
    strcpy(mutexmsg.msgtxt, "next!");
    
    //struct for communications message queue
    struct commsbuf {
        long mtype;
        pid_t childpid;
        int s, ns, logicnum, lifespan, runtime;
    };
    struct commsbuf childinfo;
    
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
                    printf("Master: Error: -s range is 1 to 18. "
                            "./oss -h for help.\n");
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
                if (runtime < 1) {
                    printf("Master: Input error: -t must be a positive integer."
                            " ./oss -h for help.\n");
                    exit(0);
                }
                break;
            default:
                break;
        }
    }
    
    //output based on options and args
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
    
    // Set up periodic timer (needs to be done AFTER parsing command line args)
    if (setperiodic(runtime) == -1) {
        perror("Failed to setup periodic interrupt");
        return 1;
    }
    
    // Set up message queues
    if ( (mutex_qid = msgget(mutexkey, 0777 | IPC_CREAT)) == -1 ) {
        perror("Error generating mutex message queue");
        exit(0);
    }
    if ( (comms_qid = msgget(commskey, 0777 | IPC_CREAT)) == -1 ) {
        perror("Error generating communication message queue");
        exit(0);
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
    
    // Initialize sim clock
    *sim_s = 0;
    *sim_ns = 0;
    
    //open file stream for logging
    
    mlog = fopen(logfilename, "w");
    if (mlog == NULL) {
        perror("producer: error opening log file");
        return -1;
    }
    
    fprintf(mlog, "Master: Launched, my pid=%ld.\n", getpid());
    
    //BEGIN MEAT OF PROGRAM*****************************************************
    
    // Push first message into queue to get slaves started
    if ( msgsnd(mutex_qid, &mutexmsg, sizeof(mutexmsg), 0) == -1 ) {
        perror("Master: error sending init msg");
        exit(0);
    }
    printf("Master: Initiating OS simulation...\n");
    
    //this for loop execs the first set of children
    for (i=0; i<maxSlaves; i++) {
        if ( (childpid = fork()) < 0 ){ //terminate code
                perror("Error forking consumer");
                return 1;
            }
        if (childpid == 0) { //child code
            sprintf(str_slavenum, "%d", i); //build arg1 string
            sprintf(str_proclimit, "%d", maxSlaves); //build arg2 string
            execlp("./user", "./user", str_slavenum, str_proclimit, (char *)NULL);
            perror("execl() failure"); //report & exit if exec fails
            return 0;
        }
        fprintf(mlog, "Master: Creating initial child pid %ld\n", childpid);
        childpids[i] = childpid; //store child pid to array
        totalforks++;
    }
    
    while (1) {
        //wait for message from terminating child (includes logical slave#)
        if ( msgrcv(comms_qid, &childinfo, sizeof(childinfo), 1, 0) == -1 ) {
            perror("Slave: error in msgrcv");
            exit(0);
        }
        //write child termination to log
        fprintf(mlog,"Master: Child pid %ld is terminating at time %02d:%09d "
                "because it worked for 00:%09d and lived for 00:%09d.\n",
            childinfo.childpid, childinfo.s, childinfo.ns, childinfo.runtime,
            childinfo.lifespan);
        fflush(mlog);
        
        //wait for clock access: critical section barrier
        if ( msgrcv(mutex_qid, &mutexmsg, sizeof(mutexmsg), 1, 0) == -1 ) {
            perror("Master: error in msgrcv from terminating slave");
            exit(0);
        }
        
        //critical section BEGINS
        //read sim clock
        localsec = *sim_s;
        localns = *sim_ns;
        if (localsec < 2) {
            localns = localns + 100; //increment 100ns for master operation
        }
        if ( (localns >= BILLION) && (localsec < 2) ) { //roll ns to s if exceeding 1billion
            localsec++;
            temp = BILLION - localns;
            localns = 100 - temp;
        }
        
        //break and terminate if we've reached 2 total seconds
        if (localsec >= 2) {
            printf("Master: Simulated runtime has reached %02d:%09d after %d forks.\n", 
                    localsec, localns, totalforks);
            break;
        }
        //break and terminate if fork limit is reached
        if ( totalforks >= 100) {
            printf("Master: 100 forks reached, breaking.\n");
            fprintf(mlog, "Master: 100 forks reached.\n");
            break;
        }
        
        //update sim clock in shared memory
        *sim_s = localsec;
        *sim_ns = localns;
        
        //fork new child and assign same logical number as last slave
        //that reported termination
        if ( (childpid = fork()) < 0 ){ //terminate code
                perror("Error forking Slave");
                return 1;
            }
        if (childpid == 0) { //child code
            sprintf(str_slavenum, "%d", childinfo.logicnum); //build arg1 string
            sprintf(str_proclimit, "%d", maxSlaves); //build arg2 string
            execlp("./user", "./user", str_slavenum, str_proclimit, (char *)NULL);
            perror("execl() failure"); //report & exit if exec fails
            return 0;
        }
        fprintf(mlog, "Master pid=%ld: Creating new child pid %ld at my time %02d:%09d\n",
            getpid(), childpid, localsec, localns);
        //store child pid to the array position of the last terminated child
        childpids[childinfo.logicnum] = childpid;
        
        //cede clock access
        if ( msgsnd(mutex_qid, &mutexmsg, sizeof(mutexmsg), 0) == -1 ) {
            perror("Master: error sending init msg");
            exit(0);
        }
        //critical section ENDS
        totalforks++;
    }
    
    //END MEAT OF PROGRAM*******************************************************
    //If this point is reached, total runtime has been met
    //kill children, clear shared memory and message queues, and exit
    printf("Master pid=%ld broke from main loop\n", getpid());
    killchildren();
    clearIPC();
    fprintf(mlog, "Master pid=%ld: Normal exit.\n", getpid());
    fclose(mlog);
    printf("Master pid=%ld: Normal exit.\n", getpid());
    return 1;
}

//print usage message and exit
static void helpmessage() {
    printf("Usage: ./oss [ -s <number 1-18> ] [ -l <filename> ] "
            "[ -t <positive number> ] [ -h ]\n");
    printf("s: number of slave processes to run (max 18). l: logfile name "
            "(.log extension added automatically).\n");
    printf("t: time limit in seconds before master terminates. h: help\n");
    exit(0);
}

//kill the children
void killchildren() {
    int sh_status, status, i;
    pid_t sh_wpid, result;
    for (i=0; i < maxSlaves ; i++) {
        result = waitpid(childpids[i], &status, WNOHANG);
        if (result == 0) {//child is still alive
            printf("Master pid=%ld: killing active child %ld\n", getpid(), childpids[i]);
            kill(childpids[i], SIGINT);
        }
        else if (result == -1) {
            //perror("Master: error getting child status before termination");
            exit(0);
        }
        else {
            printf("Master: Known child %ld has already terminated.\n", childpids[i]);
        }
    }
    printf("Master pid=%ld: Exited kill loop, i=%d\n", getpid(), i);
    //wait for all children to finish
    while ( (sh_wpid = wait(&sh_status)) > 0);
}

//function to clear shared memory
void clearIPC() {
    printf("Master pid=%ld: Clearing IPC resources...\n", getpid());
    //shared memory
    if ( shmctl(shmid_sim_s, IPC_RMID, NULL) == -1) {
        perror("error removing shared memory");
    }
    if ( shmctl(shmid_sim_ns, IPC_RMID, NULL) == -1) {
        perror("error removing shared memory");
    }
    //message queues
    if ( msgctl(mutex_qid, IPC_RMID, NULL) == -1 ) {
        perror("Master: Error removing mutex_qid");
        exit(0);
    }
    if ( msgctl(comms_qid, IPC_RMID, NULL) == -1 ) {
        perror("Master: Error removing comms_qid");
        exit(0);
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
    printf("Master: Timer Interrupt Detected! signo = %d\n", signo);
    killchildren();
    clearIPC();
    //close log file
    fprintf(mlog, "Master: Terminated: Timed Out\n");
    fclose(mlog);
    printf("Master: Terminated: Timed Out\n");
    exit(0);
}

static void siginthandler(int sig_num) {
    //printf("Master pid=%ld: Ctrl+C interrupt detected! signo = %d\n", getpid(), sig_num);
    
    killchildren();
    clearIPC();
    
    fprintf(mlog, "Master pid=%ld: Terminated: Interrupted\n", getpid());
    fclose(mlog);
    
    printf("Master pid=%ld: Terminated: Interrupted\n", getpid());
    exit(0);
}