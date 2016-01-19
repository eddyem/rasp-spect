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
#include "dbg.h"
#include "que.h"

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


#ifdef __arm__
// arrays for full & half stepping (signal level for corresponding pin)
// {MOTOR_PIN1, MOTOR_PIN2, MOTOR_PIN3, MOTOR_PIN4}
/*
const int steps_full[][4] = {
	{1, 0, 0, 1},
	{1, 0, 1, 0},
	{0, 1, 1, 0},
	{0, 1, 0, 1}
};
*/

const int steps_full[][4] = {
	{1, 0, 0, 1},
	{1, 0, 1, 0},
	{0, 1, 1, 0},
	{0, 1, 0, 1}
};

/*
const int steps_half[][4] = {
	{0, 1, 0, 1},
	{0, 1, 0, 0},
	{0, 1, 1, 0},
	{0, 0, 1, 0},
	{1, 0, 1, 0},
	{1, 0, 0, 0},
	{1, 0, 0, 1},
	{0, 0, 0, 1}
};
*/

const int steps_half[][4] = {
	{1, 0, 0, 1},
	{1, 0, 1, 1},
	{1, 0, 1, 0},
	{1, 1, 1, 0},
	{0, 1, 1, 0},
	{0, 1, 1, 1},
	{0, 1, 0, 1},
	{1, 1, 0, 1}
};

/*
const int steps_half[][4] = {
	{0, 1, 1, 0},
	{0, 1, 1, 1},
	{0, 1, 0, 1},
	{1, 1, 0, 1},
	{1, 0, 0, 1},
	{1, 0, 1, 1},
	{1, 0, 1, 0},
	{1, 1, 1, 0}
};
*/
#endif // __arm__

void setup_pins(){
#ifdef __arm__
	wiringPiSetupGpio();
	pinMode(MOTOR_PIN1, OUTPUT);
	pinMode(MOTOR_PIN2, OUTPUT);
	pinMode(MOTOR_PIN3, OUTPUT);
	pinMode(MOTOR_PIN4, OUTPUT);
	pinMode(LAMP1_PIN, OUTPUT);
	pinMode(LAMP2_PIN, OUTPUT);
	pinMode(ESW1_PIN, INPUT);
	pinMode(ESW2_PIN, INPUT);

	stop_motor();
	Write(LAMP1_PIN, 1);
	Write(LAMP2_PIN, 1);
#else // __arm__
	printf("Setup GPIO\n");
#endif // __arm__
}

#ifdef __arm__
volatile int glob_dir = 0, stepspersec = 150;
volatile unsigned int steps = 0, stopat = 0;
volatile double halfsteptime;
#endif // __arm__

/**
 * Stop stepper motor
 */
void stop_motor(){
#ifdef __arm__
	// disable motor & all other
	Write(MOTOR_PIN1, 0);
	Write(MOTOR_PIN2, 0);
	Write(MOTOR_PIN3, 0);
	Write(MOTOR_PIN4, 0);
	DBG("STOPPED");
#else
	printf("Stop Stepper\n");
#endif // __arm__
}

int steppart = 0;
/**
 * Rotate motors X,Y to direction dir (CW > 0)
 * Stop motor if dir == 0
 */
void move_motor(int dir){
	if(dir == 0){ // stop
#ifdef __arm__
		glob_dir = 0;
		stopat = 0;
		stop_motor();
	//	exit(0);
#else // __arm__
		printf("Stop motor\n");
#endif // __arm__
	}else{
#ifdef __arm__
	if((digitalRead(ESW1_PIN) == 0  && dir == -1) || 
		(digitalRead(ESW2_PIN) == 0 && dir == 1)){
		DBG("already on ESW");
	//	exit(0);
		glob_dir = 0;
	}else{
		glob_dir = dir;
		if(dir > 0) steppart = 0;
		else steppart = 7;
		DBG("move to %d", dir);
	}
#else // __arm__
		printf("Move X to %s\n", (dir > 0) ? "++" : "--");
#endif // __arm__
	}
}

/**
 * Move motors in direction dir to Nsteps
 * (if Nsteps == 0 then move infinitely)
 */
void Xmove(int dir, unsigned int Nsteps){
#ifdef __arm__
	steps = 0;
	stopat = Nsteps;
	move_motor(dir);
#else
	printf("Move x axis to %u in dir %d\n", Nsteps, dir);
#endif // __arm__
}

void set_motors_speed(int steps_per_sec){
	if(steps_per_sec > 0 && steps_per_sec < MAX_SPEED){
#ifdef __arm__
		stepspersec = steps_per_sec;
		halfsteptime = 1. / (stepspersec * 8.);
		GLOB_MESG("curspd=%d", get_motors_speed());

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


extern volatile int force_exit;
/**
 * Main thread for steppers management
 */
void *steppers_thread(_U_ void *buf){
	DBG("steppers_thr");
#ifdef __arm__
	double laststeptime, curtime;
	halfsteptime = 1. / (stepspersec * 8.);
	DBG("halfsteptime: %g", halfsteptime);
	laststeptime = dtime();
	int eswsteps = 0;
	while(!force_exit){
		usleep(10); // garanteed pause == 10us
		if(glob_dir){
			//DBG("cur-last=%g, half: %g", dtime() - laststeptime, halfsteptime);
			if((curtime = dtime()) - laststeptime > halfsteptime){
				//DBG("1/8step");
				
				Write(MOTOR_PIN1, steps_half[steppart][0]);
				Write(MOTOR_PIN2, steps_half[steppart][1]);
				Write(MOTOR_PIN3, steps_half[steppart][2]);
				Write(MOTOR_PIN4, steps_half[steppart][3]);
				/*
				Write(MOTOR_PIN1, steps_full[steppart][0]);
				Write(MOTOR_PIN2, steps_full[steppart][1]);
				Write(MOTOR_PIN3, steps_full[steppart][2]);
				Write(MOTOR_PIN4, steps_full[steppart][3]);
				*/
				//DBG("steppart: %d", steppart);
				laststeptime = curtime;
				int fullstep = 0;
				if(glob_dir > 0){
					if(++steppart == 8){
					//if(++steppart == 4){
						steppart = 0;
						fullstep = 1;
					}
				}else{
					if(--steppart < 0){
						steppart = 7;
						//steppart = 3;
						fullstep = 1;
					}
				}
				if(fullstep){ // full step processed
					++steps;
					if(steps % 10 == 0) 
						GLOB_MESG("nsteps=%d", steps);
					// check end-switches, if motor reach esw, stop it
					if((digitalRead(ESW1_PIN) == 0  && glob_dir == -1) || 
							(digitalRead(ESW2_PIN) == 0 && glob_dir == 1)){
						if(++eswsteps == 5){
							int Nsw = get_endsw();
							if(Nsw){
								DBG("Reach end-switch %d @%u steps", Nsw, steps);
								GLOB_MESG("esw=%d", Nsw);
								move_motor(0);
								GLOB_MESG("end-switch %d reached at %u steps", Nsw, steps);
							}else eswsteps = 0;
						}
					}else eswsteps = 0;
					//DBG("step");
					if(stopat && stopat == steps){ // finite move for stopat steps
						DBG("Reach position of %d steps", steps);
						GLOB_MESG("position %d steps reached", steps);
						move_motor(0);
					}
				}
			}
		}else{
			steppart = 0;
		}
	}
#else // __arm__
	printf("Main steppers' thread\n");
	while(1) usleep(1000);
#endif // __arm__
	DBG("return motors_thr");
	return NULL;
}

int get_rest_steps(){
#ifdef __arm__
	if(glob_dir){
		if(stopat){
			return stopat - steps;
		}else
			return 1; // always return on infinite move
	}else return 0;
#else
	printf("get_rest_steps\n");
	return 0;
#endif // __arm__
}

int get_direction(){
#ifdef __arm__
	return glob_dir;
#else // __arm__
	printf("get_direction\n");
	return 0;
#endif // __arm__
}

// 0 - none, 1 - ESW1, 2 - ESW2, 3 - both
int get_endsw(){
#ifdef __arm__
	return (1 - digitalRead(ESW1_PIN)) + 2 * (1 - digitalRead(ESW2_PIN));
#else // __arm__
	printf("get_endsw\n");
	return 0;
#endif // __arm__
}

/**
 * turn on/off (state == 1/0 1 - on, 0 - off) lamp with number nlamp
 * nlamp = {1, 2}
 */
void set_lamp(int nlamp, int state){
#ifdef __arm__
	if(nlamp > 2 || nlamp < 1) return;
	if(nlamp == 1)
		Write(LAMP1_PIN, !state);
	else
		Write(LAMP2_PIN, !state);
#else // __arm__
	printf("set_lamp(%d, %d)\n", nlamp, state);
#endif // __arm__
}

void switch_lamp(int nlamp){
#ifdef __arm__
	if(nlamp > 2 || nlamp < 1) return;
	if(nlamp == 1){
		Write(LAMP1_PIN, !digitalRead(LAMP1_PIN));
	}else{
		Write(LAMP2_PIN, !digitalRead(LAMP2_PIN));
	}
#else // __arm__
	printf("switch_lamp(%d)\n", nlamp);
#endif // __arm__
}

int getlamp(){
#ifdef __arm__
	return (1 - digitalRead(LAMP1_PIN)) + 2*(1 - digitalRead(LAMP2_PIN));
#else // __arm__
	printf("getlamp()\n");
	return 0;
#endif // __arm__
}

/*
#ifdef __arm__
#else // __arm__
#endif // __arm__
*/
