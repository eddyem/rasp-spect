/*
 * image.h
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
#ifndef __IMAGE_H__
#define __IMAGE_H__

#include <libwebsockets.h>

#define IMAGE_HOST   "localhost"
#define IMAGE_PORT   "54321"
#define IMAGE_FORMAT "jpg"

typedef struct{
	unsigned char *data;
	size_t len;
} imbuf;

void prepare_image(imbuf *buf);
void free_imbuf(imbuf *buf);
void send_buffer(struct libwebsocket *wsi, imbuf *buf);

#endif // __IMAGE_H__
