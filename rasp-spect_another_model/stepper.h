/*
 * stepper.h
 *
 * Copyright 2015 Edward V. Emelianov <eddy@sao.ru, edward.emelianoff@gmail.com>
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

#pragma once
#ifndef __STEPPER_H__
#define __STEPPER_H__

/*
 * Pins definition (used BROADCOM GPIO pins numbering)
 */
// lamp motor, l298: GPIO14 (leg8), GPIO15(leg10), GPIO18 (leg12) & GPIO23 (leg18)
#define MOTOR_PIN1	(14)
#define MOTOR_PIN2	(15)
#define MOTOR_PIN3	(18)
#define MOTOR_PIN4	(23)

// End-switches: GPIO24 (leg18) & GPIO25 (leg22)
#define ESW1_PIN	(24)
#define ESW2_PIN	(25)

// two lamps: GPIO8 (leg24) & GPIO7 (leg26)
#define LAMP1_PIN	(8)
#define LAMP2_PIN	(7)

// maximum 200 steps per second
#define MAX_SPEED	(201)

void setup_pins();
void stop_motor();
void *steppers_thread(void *buf);
void Xmove(int dir, unsigned int Nsteps);
void move_motor(int dir);
void set_motors_speed(int steps_per_sec);
int get_motors_speed();

int get_rest_steps();
int get_direction();
int get_endsw();
int getlamp();

void set_lamp(int nlamp, int state);
void switch_lamp(int nlamp);

#endif // __STEPPER_H__
