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

extern int center_reached;

void steppers_relax();
void *steppers_thread(void *buf);
void Xmove(int dir, unsigned int Nsteps);
void Ymove(int dir, unsigned int Nsteps);
void set_motors_speed(int steps_per_sec);
int get_motors_speed();
void XY_gotocenter();

#endif // __STEPPER_H__
