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

#define SHMKEY_sim_s 0420423
#define SHMKEY_sim_ns 0420145
#define BUFF_SZ sizeof (int)

static FILE *log; //master log file pointer
int shmid_sim_s, shmid_sim_ns; //shared memory ID holders for sim clock

/*
 * 
 */
int main(int argc, char** argv) {
    int slavenum, slavelimit;
    slavenum = atoi(argv[1]);
    slavelimit = atoi(argv[2]);
    
    printf("Slave: %d of %d launched.\n", slavenum+1, slavelimit);
    
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
    printf("Master: sim_s = %d\n", *sim_s);
    
    printf("Master: sim_ns = %d\n", *sim_ns);
    
    
    
    sleep(1);
    printf("Slave: %d of %d normal exit.\n", slavenum+1, slavelimit);

    return 1;
}

