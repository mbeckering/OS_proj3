/*
 * To change this license header, choose License Headers in Project Properties.
 * To change this template file, choose Tools | Templates
 * and open the template in the editor.
 */

/* 
 * File:   user.c
 * Author: Jodicus
 *
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

static void siginthandler(int);

static FILE *log; //master log file pointer
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
    int worktime; //amount of work done each cycle (randomized in critical section)
    int temp; //variable swapper
    int ag_runtime; //aggregate runtime for this slave
    
    //select random runtime_limit 1 - 1,000,000
    unsigned long seed = 3*(int)getpid() + 3*slavenum;
    srand(seed);
    unsigned long runtime_limit = rand();
    runtime_limit <<= 15; //next 4 lines taken from stackoverflow
    runtime_limit ^= rand();
    runtime_limit %= 1000000;
    runtime_limit++;
    printf("Slave %d: random runtime_limit = %ld\n", slavenum, runtime_limit);
    
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
        int s, ns;
    };
    struct commsbuf myinfo;
    myinfo.mtype = 1;
    myinfo.pid = getpid();
    
    printf("Slave %d: Launched.\n", slavenum+1, slavelimit);
    
    //interrupt handler
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
    
    //testing shm
    //printf("Slave %d: sim_s = %d\n", slavenum, *sim_s);
    //printf("Slave %d: sim_ns = %d\n", slavenum, *sim_ns);
    
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
    // (change to while counter < runtim)
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
            printf("Slave %d: total master runtime limit reached, ceding...\n", slavenum);
            if ( msgsnd(mutexq_id, &mutexmsg, sizeof(mutexmsg), 0) == -1 ) {
                perror("Slave: error exiting crit section");
            exit(0);
            }
            break;
        }
        //generate random work time
        srand(seed);
        worktime = rand() %20000 + 1;
        printf("Slave %d: Tryna work %d ns.\n", slavenum, worktime);
        //if this worktime will exceed my slave runtime limit
        if (ag_runtime + worktime >= runtime_limit) {
            temp = runtime_limit - ag_runtime; //remainder of time before my limit
            ag_runtime = ag_runtime + temp; //total time i worked
            local_ns = local_ns + temp; //increment ns by the time I was allowed to work before cutoff
            //send message to master that I've finished
            myinfo.s = local_s;
            myinfo.ns = local_ns;
            if ( msgsnd(commsqid, &myinfo, sizeof(myinfo), 0) == -1 ) {
                perror("Slave: Error sending termination message");
                exit(0);
            }
            //cede the crit section before exiting
            if ( msgsnd(mutexq_id, &mutexmsg, sizeof(mutexmsg), 0) == -1 ) {
                printf("Slave %d:", slavenum);
                perror("error exiting crit section");
            exit(0);
        }
            break;//break out and terminate this process
        }
        
        local_ns = local_ns + worktime; //increment local clock variable
        if (local_ns >= 1000000) { //roll back ns if exceeding 1million
            local_s++;
            temp = 1000000 - local_ns;
            local_ns = worktime - temp;
        }
        printf("Slave %d: clock now at %d : %d\n", slavenum, local_s, local_ns);
        //update sim clock in chared memory
        *sim_s = local_s;
        *sim_ns = local_ns;
        
        //end critical section and cede clock access
        if ( msgsnd(mutexq_id, &mutexmsg, sizeof(mutexmsg), 0) == -1 ) {
            printf("Slave %d:", slavenum);
            perror("error exiting crit section");
            exit(0);
        }
        sleep(1);
    }
    
    // End Business Loop********************************************************
    
    printf("Slave: %d of %d normal exit.\n", slavenum+1, slavelimit);
    return 1;
}

//signal handler
static void siginthandler(int sig_num) {
    int sh_status, i;
    pid_t sh_wpid;
    printf("Slave(pid %ld) Terminating: Interrupted.\n", getpid());
    exit(0);
}

