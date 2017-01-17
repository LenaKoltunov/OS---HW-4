/*
 * main.c
 *
 *  Created on: Jan 13, 2014
 *      Author: root
 */
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <sched.h>
#include "mastermind.h"

#define NUM_BREAKERS 2

int main(void) {
	printf("start\n");

	sched_yield();

	char buf[4];
	int i=0;
	int j=0;
	int res;
	int fdBreaker[NUM_BREAKERS];

	int fdMaker = open("/dev/codemaker", O_RDWR);
	for (i=0 ; i<NUM_BREAKERS ; i++) {
		fdBreaker[i] = open("/dev/codebreaker", O_RDWR);
	}

	printf("files opened:\n");
	printf("fdMaker = %d\n", fdMaker);

	int err = open("/dev/codemaker", O_RDWR);
	if (err>=0) printf("ERROR! Should not open another maker\n");

	for (i=0 ; i<NUM_BREAKERS ; i++) {
		printf("fdBreaker[%d] = %d\n", i, fdBreaker[i]);
	}

	buf[0]='0';
	buf[1]='1';
	buf[2]='2';
	buf[3]='3';
	write(fdMaker, buf, 4);

	res = write(fdBreaker[0], buf, 4); //Round hasnt started yet, should fail
	if (res>=0) printf("ERROR in breakerWrite before round start\n");

	res = ioctl(fdMaker, ROUND_START, 5);
	if(res>=0) {
		printf("SUCCESS - round initiated\n");
	}

	buf[0]='0';
	buf[1]='1';
	buf[2]='2';
	buf[3]='2';
	res = write(fdBreaker[0], buf, 4);
	if (res<0) printf("ERROR in breakerWrite - should have succeeded\n");

	read(fdMaker, buf, 4);
	printf("makerResultFromRead = ");
	for (i=0 ; i<4 ; i++) {
		printf("%c", buf[i]);
	}
	printf("\n");
	write(fdMaker, buf, 4);

	read(fdBreaker[0], buf, 4);
	printf("feedbackBuffer = ");
	for (i=0 ; i<4 ; i++) {
		printf("%c", buf[i]);
	}
	printf("\n");
	for(i=1 ; i<10 ; i++) {
		write(fdBreaker[0], buf, 4);
		read(fdMaker, buf, 4);
		write(fdMaker, buf, 4);
		read(fdBreaker[0], buf, 4); 	//expending all turns
	}

	res = write(fdBreaker[0], buf, 4);
	if (res>=0) printf("ERROR in breakerRead - should not have turns left\n");

	for(i=1 ; i<NUM_BREAKERS ; i++) { 	//expending everyone else's turns
		for (j=0 ; j<10 ; j++) {
			write(fdBreaker[i], buf, 4);
			read(fdMaker, buf, 4);
			write(fdMaker, buf, 4);
			read(fdBreaker[i], buf, 4);
		}
	}

	res = ioctl(fdMaker, GET_MY_SCORE);
	printf("Maker score is %d\n", res);

	for(i=0 ; i<NUM_BREAKERS ; i++) {
		res = ioctl(fdBreaker[i], GET_MY_SCORE);
		printf("Breaker[%d] score is %d\n", i, res);
	}

	res = ioctl(fdMaker, ROUND_START, 4);
	if(res<0) {
		printf("ERROR in ioctl - maker should be able to start another round\n");
	}

	buf[0]='0';
	buf[1]='1';
	buf[2]='2';
	buf[3]='3';
	write(fdBreaker[0], buf, 4);		//This time we'll let a Breaker win
	read(fdMaker, buf, 4);
	write(fdMaker, buf, 4);
	read(fdBreaker[0], buf, 4);

	res = ioctl(fdMaker, GET_MY_SCORE);
	printf("Maker score is %d\n", res);

	for(i=0 ; i<NUM_BREAKERS ; i++) {
		res = ioctl(fdBreaker[i], GET_MY_SCORE);
		printf("Breaker[%d] score is %d\n", i, res);
	}

	res = close(fdMaker);
	if (res<0) printf("ERROR in close fdMaker\n");
	for (i=0 ; i<NUM_BREAKERS ; i++) {
		res = close(fdBreaker[i]);
		if (res<0) printf("ERROR in close fdBreaker[%d]\n", i);
	}

	return 0;
}
