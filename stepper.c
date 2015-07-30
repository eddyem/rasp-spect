/*
 * stepper.c - functions for working with stepper motors by wiringPi
 *
 * Copyright 2015 Edward V. Emelianoff <eddy@sao.ru>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
 * MA 02110-1301, USA.
 */
#include <stdio.h>			// printf, getchar, fopen, perror
#include <stdlib.h>			// exit
#include <signal.h>			// signal
#include <time.h>			// time
#include <string.h>			// memcpy
#include <stdint.h>			// int types
#include <sys/time.h>		// gettimeofday
#define X_OPEN_SOURCE 999
#include <unistd.h>			// usleep
// use wiringPi on ARM & simple echo on PC (for tests)
#ifdef __arm__
	#include <wiringPi.h>
#endif

#include "stepper.h"

/*
 * Pins definition (used BROADCOM GPIO pins numbering)
 */
// X motor: pins GPIO18 (leg12), GPIO23(leg16), GPIO24(leg18)
#define X_EN_PIN	(18)
#define X_DIR_PIN	(23)
#define X_CLK_PIN	(24)
// Y motor: pins GPIO25 (leg22), GPIO8 (leg24), GPIO7 (leg26)
#define Y_EN_PIN	(25)
#define Y_DIR_PIN	(8)
#define Y_CLK_PIN	(7)

// amount of steps to zero (max inclination, dir = -1) and center (dir = 1)
//#define X_TOZERO_STEPS   (4500)
#define X_TOZERO_STEPS   (3800)
//#define Y_TOZERO_STEPS   (7300)
#define Y_TOZERO_STEPS   (7500)
//#define X_TOCENTER_STEPS (2000)
#define X_TOCENTER_STEPS (1365)
//#define Y_TOCENTER_STEPS (3450)
#define Y_TOCENTER_STEPS (2800)

// max speed in steps per second
#define MAX_SPEED  (500)
#ifndef _U_
	#define _U_  __attribute__((__unused__))
#endif

int center_reached = 0;

#ifdef __arm__
/**
 * function for different purposes that need to know time intervals
 * @return double value: time in seconds
 */
static double dtime(){
	double t;
	struct timeval tv;
	gettimeofday(&tv, NULL);
	t = tv.tv_sec + ((double)tv.tv_usec)/1e6;
	return t;
}

void Write(int pin, int val){
	if(val) val = 1;
	digitalWrite(pin, val);
	while(digitalRead(pin) != val);
}
#endif // __arm__

/**
 * Exit & return terminal to old state
 * @param ex_stat - status (return code)
 */
void steppers_relax(){
	printf("Stop Steppers\n");
#ifdef __arm__
	// disable motor & all other
	Write(X_EN_PIN, 0);Write(X_DIR_PIN, 1);Write(X_CLK_PIN, 1);
	Write(Y_EN_PIN, 0);Write(Y_DIR_PIN, 1);Write(Y_CLK_PIN, 1);
#endif // __arm__
}


void setup_motors(){
#ifdef __arm__
	wiringPiSetupGpio();
	pinMode(X_EN_PIN, OUTPUT);
	pinMode(X_DIR_PIN, OUTPUT);
	pinMode(X_CLK_PIN, OUTPUT);
	pinMode(Y_EN_PIN, OUTPUT);
	pinMode(Y_DIR_PIN, OUTPUT);
	pinMode(Y_CLK_PIN, OUTPUT);
	Write(X_EN_PIN, 0);
	Write(Y_EN_PIN, 0);
	Write(X_CLK_PIN, 1);
	Write(Y_CLK_PIN, 1);
	Write(X_DIR_PIN, 1);
	Write(Y_DIR_PIN, 1);
#else // __arm__
	printf("Setup GPIO\n");
#endif // __arm__
}

#ifdef __arm__
int running[2] = {0,0}, stepspersec = 50;
int gotocenter[2] = {0,0};
unsigned int steps[2] = {0,0}, stopat[2] = {0,0};
volatile double halfsteptime;
#endif // __arm__

/**
 * Rotate motors X,Y to direction dir (CW > 0)
 * Stop motor if dir == 0
 */
void move_X(int dir){
	if(dir == 0){ // stop
#ifdef __arm__
		Write(X_EN_PIN, 0); // disable motor
		running[0] = 0;
		if(gotocenter[0]){
			switch(gotocenter[0]){
				case 1: // first stage of going to center -> turn it to second
					gotocenter[0] = 2;
					Xmove(1, X_TOCENTER_STEPS);
				break;
				case 2: // second stage -> all OK
				default:
					gotocenter[0] = 0;
					if(gotocenter[1] == 0){ // restore speed when all stopt
						halfsteptime = 1. / (stepspersec * 8. * 2.);
						center_reached = 1;
					}
			}
		}
#else // __arm__
		printf("Stop X motor\n");
#endif // __arm__
	}else{
#ifdef __arm__
		Write(X_EN_PIN, 1); // enable motor
		Write(X_DIR_PIN, (dir>0)? 0:1); // CW - >0, CCW - <0
		running[0] = 1;
#else // __arm__
		printf("Move X to %s\n", (dir > 0) ? "++" : "--");
#endif // __arm__
	}
}
void move_Y(int dir){
	if(dir == 0){ // stop
#ifdef __arm__
		Write(Y_EN_PIN, 0); // disable motor
		running[1] = 0;
		if(gotocenter[1]){
			switch(gotocenter[1]){
				case 1: // first stage of going to center -> turn it to second
					gotocenter[1] = 2;
					Ymove(1, Y_TOCENTER_STEPS);
				break;
				case 2: // second stage -> all OK
				default:
					gotocenter[1] = 0;
					if(gotocenter[0] == 0){
						halfsteptime = 1. / (stepspersec * 8. * 2.);
						center_reached = 1;
					}
			}
		}
#else // __arm__
		printf("Stop Y motor\n");
#endif // __arm__
	}else{
#ifdef __arm__
		Write(Y_EN_PIN, 1); // enable motor
		Write(Y_DIR_PIN, (dir>0)? 0:1); // CW - >0, CCW - <0
		running[1] = 1;
#else // __arm__
		printf("Move Y to %s\n", (dir > 0) ? "++" : "--");
#endif // __arm__
	}
}

/**
 * Move motors in direction dir to Nsteps
 * (if Nsteps == 0 then move infinitely)
 */
void Xmove(int dir, unsigned int Nsteps){
#ifdef __arm__
	if(dir == 0) printf("X stops at %d steps\n", steps[0]);
	steps[0] = 0;
#endif // __arm__
	if(Nsteps == 0){
#ifdef __arm__
		stopat[0] = 0;
#endif // __arm__
		move_X(dir);
	}else{
#ifdef __arm__
		stopat[0] = Nsteps;
		move_X(dir);
#endif // __arm__
	}
}
void Ymove(int dir, unsigned int Nsteps){
#ifdef __arm__
	if(dir == 0) printf("Y stops at %d steps\n", steps[1]);
	steps[1] = 0;
#endif // __arm__
	if(Nsteps == 0){
#ifdef __arm__
		stopat[1] = 0;
#endif // __arm__
		move_Y(dir);
	}else{
#ifdef __arm__
		stopat[1] = Nsteps;
		move_Y(dir);
#endif // __arm__
	}
}

void XY_gotocenter(){
#ifdef __arm__
	gotocenter[0] = gotocenter[1] = 1;
	halfsteptime = 1. / (MAX_SPEED * 8. * 2.);
	Xmove(-1, X_TOZERO_STEPS);
	Ymove(-1, Y_TOZERO_STEPS);
#else // __arm__
	printf("Go to center\n");
#endif // __arm__
}

void set_motors_speed(int steps_per_sec){
	if(steps_per_sec > 0 && steps_per_sec < MAX_SPEED){
#ifdef __arm__
		stepspersec = steps_per_sec;
		halfsteptime = 1. / (stepspersec * 8. * 2.);
#else // __arm__
		printf("Set speed to %d\n", steps_per_sec);
#endif // __arm__
	}
}

int get_motors_speed(){
#ifdef __arm__
	return stepspersec;
#else // __arm__
	return 111;
#endif // __arm__
}

/*
#ifdef __arm__
#else // __arm__
#endif // __arm__
*/

/**
 * Main thread for steppers management
 */
void *steppers_thread(_U_ void *buf){
#ifdef __arm__
	int i = 1;
	double laststeptime, curtime;
	halfsteptime = 1. / (stepspersec * 8. * 2.); // 300 steps & 8 usteps per second
	setup_motors();
	laststeptime = dtime();
	while(1){
		usleep(10); // garanteed pause == 10us
		if(running[0] || running[1]){
			if((curtime = dtime()) - laststeptime > halfsteptime){
				i ^= 1;
				if(running[0]){ // X motor
					Write(X_CLK_PIN, i);
					if(i){
						if(stopat[0] && stopat[0] == steps[0])
							move_X(0); // stop motor at destination
						else
							steps[0]++;
					}
				}
				if(running[1]){ // Y motor
					Write(Y_CLK_PIN, i);
					if(i){
						if(stopat[1] && stopat[1] == steps[1])
							move_Y(0); // stop motor at destination
						else
							steps[1]++;
					}
				}
				laststeptime = curtime;
			}
		}
	}
#else // __arm__
	printf("Main steppers' thread\n");
	while(1) usleep(1000);
#endif // __arm__
}
