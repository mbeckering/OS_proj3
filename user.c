/* 
 * File:   user.c
 * Author: Jodicus
 * Project 3
 * Spring 2018 CS-4760-E01
 * Created on February 28, 2018, 10:08 AM
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/msg.h>
#include <sys/types.h>
#include <string.h>

#define SHMKEY_sim_s 0420423
#define SHMKEY_sim_ns 0420145
#define BUFF_SZ sizeof (int)
#define mutexkey 0420323
#define commskey 0420541
#define BILLION 1000000000 //dont want to type the wrong # of zeroes

//prototype function declarations
static void siginthandler(int);

// GLOBALS
int shmid_sim_s, shmid_sim_ns; //shared memory ID holders for sim clock

/*
 * 
 */
int main(int argc, char** argv) {
    int slavenum, slavelimit;
    slavenum = atoi(argv[1]); //logical number of this process
    slavelimit = atoi(argv[2]); //max number of concurrent slaves
    int mutexmsgid, commsqid; //message id for the mutex enforcement queue
    int local_s, local_ns; //local variables for clock values
    int starttime; //time on sim clock at first clock read
    int worktime; //amount of work done each cycle (randomized in critical section)
    int lifetime; //time on sim clock from first read to termination
    int temp; //variable swapper
    int ag_runtime; //aggregate runtime for this slave
    int i = 0; //iterator
    
    //select random runtime_limit
    unsigned long seed = 3*(int)getpid() + 3*slavenum;
    srand(seed);
    unsigned long runtime_limit = rand();
    runtime_limit <<= 15; //next 4 lines taken from stackoverflow for big rands
    runtime_limit ^= rand();
    runtime_limit %= 100000000;
    runtime_limit++;
    
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
        pid_t pid;
        int s, ns, logicnum, lifetime, runtime;
    };
    struct commsbuf myinfo;
    myinfo.mtype = 1;
    myinfo.pid = getpid();
    
    // Set up interrupt handler
    signal (SIGINT, siginthandler);
    
    // Set up shared memory
    shmid_sim_s = shmget(SHMKEY_sim_s, BUFF_SZ, 0777);
        if (shmid_sim_s == -1) { //terminate if shmget failed
            perror("Slave: Error in consumer shmget shmid_sim_s");
            return 1;
        }
    int *sim_s = (int*) shmat(shmid_sim_s, 0, 0);
    
    shmid_sim_ns = shmget(SHMKEY_sim_ns, BUFF_SZ, 0777);
        if (shmid_sim_ns == -1) { //terminate if shmget failed
            perror("Slave: Error in consumer shmget shmid_sim_ns");
            return 1;
        }
    int *sim_ns = (int*) shmat(shmid_sim_ns, 0, 0);
    
    // Set up message queues
    int mutexq_id;
    if ( (mutexq_id = msgget(mutexkey, 0777)) == -1 ) {
        perror("Slave: Error generating mutex message queue");
        exit(0);
    }
    
    if ( (commsqid = msgget(commskey, 0777 | IPC_CREAT)) == -1 ) {
        perror("Error generating communication message queue");
        exit(0);
    }
    
    // The Business Loop********************************************************
    while (1) {
        //barrier to enter critical section
        if ( msgrcv(mutexq_id, &mutexmsg, sizeof(mutexmsg), 1, 0) == -1 ) {
            perror("Slave: error in msgrcv");
            exit(0);
        }
        //critical section: pull clock values first
        local_s = *sim_s;
        local_ns = *sim_ns;
        //cede and break if total master runtime_limit has been reached
        if (local_s >= 2) {
            printf("Slave %02d: Total master runtime limit reached. "
                    "Ceding and terminating.\n", slavenum);
            if ( msgsnd(mutexq_id, &mutexmsg, sizeof(mutexmsg), 0) == -1 ) {
                perror("Slave: error exiting crit section");
                exit(0);
            }
            //pack information into message queue struct
            myinfo.s = local_s;
            myinfo.ns = local_ns;
            myinfo.logicnum = slavenum;
            myinfo.runtime = ag_runtime;
            //send message to master that I'm terminating
            if ( msgsnd(commsqid, &myinfo, sizeof(myinfo), 0) == -1 ) {
                perror("Slave: Error sending termination message");
                exit(0);
            }
            break;
        }
        
        //generate random work time
        seed = seed*( (slavenum + 1) *3);
        srand(seed);
        worktime = rand() %200000 + 1;

        //if this worktime will exceed my slave runtime limit
        if (ag_runtime + worktime >= runtime_limit) {
            temp = runtime_limit - ag_runtime; //remainder of time before my limit
            ag_runtime = ag_runtime + temp; //total time i worked
            local_ns = local_ns + temp; //increment ns by the time until cutoff
            
            if (local_ns >= BILLION) { //roll back ns if exceeding 1billion
                local_s++;
                *sim_s = local_s; //increment seconds on shared sim clock
                temp = BILLION - local_ns;
                local_ns = worktime - temp;
            }
            *sim_ns = local_ns; //increment shared sim clock
            //pack info and send message to master that I'm terminating
            myinfo.s = local_s;
            myinfo.ns = local_ns;
            myinfo.logicnum = slavenum;
            myinfo.runtime = ag_runtime;
            printf("Slave %d: ag_runtime=%d, runtime_limit=%d\n", slavenum, ag_runtime, runtime_limit);
            if ( msgsnd(commsqid, &myinfo, sizeof(myinfo), 0) == -1 ) {
                perror("Slave: Error sending termination message");
                exit(0);
            }
            //cede the crit section before exiting
            if ( msgsnd(mutexq_id, &mutexmsg, sizeof(mutexmsg), 0) == -1 ) {
                perror("Slave: error exiting crit section");
            exit(0);
            }
            exit(1);
        }

        local_ns = local_ns + worktime; //increment local clock variable
        ag_runtime = ag_runtime + worktime; //increment my aggregate work time
        
        if (local_ns >= BILLION) { //roll back ns if exceeding 1billion
            local_s++;
            temp = BILLION - local_ns;
            local_ns = worktime - temp;
        }

        //update sim clock in chared memory
        *sim_s = local_s;
        *sim_ns = local_ns;
        
        //end critical section and cede clock access
        if ( msgsnd(mutexq_id, &mutexmsg, sizeof(mutexmsg), 0) == -1 ) {
            perror("error exiting crit section");
            exit(0);
        }
    }
    
    // End Business Loop********************************************************
    
    return 1;
}

//signal handler
static void siginthandler(int sig_num) {
    int sh_status, i;
    pid_t sh_wpid;
    printf("Slave(pid %ld) Terminating: Interrupted.\n", getpid());
    exit(0);
}

