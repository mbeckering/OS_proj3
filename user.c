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

/*
 * 
 */
int main(int argc, char** argv) {
    int slavenum, slavelimit;
    slavenum = atoi(argv[1]);
    slavelimit = atoi(argv[2]);
    
    printf("Slave: %d of %d launched.\n", slavenum+1, slavelimit);
    sleep(1);
    printf("Slave: %d of %d normal exit.\n", slavenum+1, slavelimit);

    return 1;
}

