/*
 * image.c - get images from astrovideoguide_v2 & prepare buffer for websokets
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
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <string.h>

#include "image.h"
#include "dbg.h"

#define BUFSIZE  (204800)

/**
 * Test function to save captured frame to a ppm file
 * @param pFrame - pointer to captured frame
 * @param iFrame - frame number (for filename like frameXXX.png
 * @return 0 if false
 *
 * image format:
 * format\nsize\ndata
 */
unsigned char *getsz(imbuf *buf, size_t *len){
	char *eptr;
	unsigned char *pFrame;
	long L;
	if(strncasecmp("jpg", (char*)buf->data, 3)){
		fprintf(stderr, "Wrong format in answer!\n");
		return NULL;
	}
	pFrame = (unsigned char*)strchr((char*)buf->data, '\n');
	if(!pFrame || !(++pFrame)){
		fprintf(stderr, "bad file format!\n");
		return NULL;
	}
	L = strtol((char*)pFrame, &eptr, 10);
	if(!eptr || *eptr != '\n'){
		fprintf(stderr, "bad file format!\n");
		return NULL;
	}
	++eptr;
	if(len) *len = (size_t) L;
	DBG("length: %ld bytes", L);
#if defined EBUG || defined DEBUG
	int F = open("file.jpg", O_WRONLY | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
	write(F, eptr, L);
	close(F);
#endif
	return (unsigned char*)eptr;
}

/**
 * wait for answer from server
 * @param sock - socket fd
 * @return 0 in case of error or timeout, 1 in case of socket ready
 */
int waittoread(int sock){
	fd_set fds;
	struct timeval timeout;
	int rc;
	timeout.tv_sec = 0; // wait not more than 100 millisecond
	timeout.tv_usec = 100000;
	FD_ZERO(&fds);
	FD_SET(sock, &fds);
	rc = select(sock+1, &fds, NULL, NULL, &timeout);
	if(rc < 0){
		perror("select failed");
		return 0;
	}
	if(FD_ISSET(sock, &fds)){
		DBG("there's data in socket");
		return 1;
	}
	return 0;
}

int sockfd = -2;
int open_socket(){
	struct addrinfo h, *r, *p;
	memset(&h, 0, sizeof(h));
	h.ai_family = AF_INET;
	h.ai_socktype = SOCK_STREAM;
	h.ai_flags = AI_CANONNAME;
	char *host = IMAGE_HOST;
	char *port = IMAGE_PORT;
	if(getaddrinfo(host, port, &h, &r)){perror("getaddrinfo"); return 1;}
	struct sockaddr_in *ia = (struct sockaddr_in*)r->ai_addr;
	char str[INET_ADDRSTRLEN];
	inet_ntop(AF_INET, &(ia->sin_addr), str, INET_ADDRSTRLEN);
	printf("canonname: %s, port: %u, addr: %s\n", r->ai_canonname, ntohs(ia->sin_port), str);
	for(p = r; p; p = p->ai_next) {
		if((sockfd = socket(p->ai_family, p->ai_socktype,
				p->ai_protocol)) == -1){
			perror("socket");
			continue;
		}
		if(connect(sockfd, p->ai_addr, p->ai_addrlen) == -1){
			close(sockfd);
			perror("connect");
			continue;
		}
		break; // if we get here, we must have connected successfully
	}
	if(p == NULL){
		// looped off the end of the list with no connection
		fprintf(stderr, "failed to connect\n");
		return 1;
	}
	freeaddrinfo(r);
	//setnonblocking(sockfd);
	return 0;
}

uint8_t *capture_frame(size_t *sz){
	size_t bufsz = BUFSIZE;
	if(sockfd < 0)if(open_socket()){
		fprintf(stderr, "Can't open socket");
		sockfd = -3;
		return NULL;
	}
	uint8_t *recvBuff = malloc(bufsz);
	if(!recvBuff) return NULL;
	char *msg = IMAGE_FORMAT;
	size_t L = strlen(msg);
	ssize_t LL = write(sockfd, msg, L);
	if((size_t)LL != L){
		perror("send");
		close(sockfd);
		sockfd = -1;
		return NULL;
	}
	DBG("send %s (len=%zd) to fd=%d", msg, L, sockfd);
	if(!waittoread(sockfd)){
		DBG("Nothing to read");
		return NULL;
	}
	size_t offset = 0;
	do{
		if(offset >= bufsz){
			bufsz += BUFSIZE;
			recvBuff = realloc(recvBuff, bufsz);
			assert(recvBuff);
			DBG("Buffer reallocated, new size: %zd\n", bufsz);
		}
		LL = read(sockfd, &recvBuff[offset], bufsz - offset);
		if(!LL) break;
		if(LL < 0){
			perror("read");
			return NULL;
		}
		offset += (size_t)LL;
	}while(waittoread(sockfd));
	if(!offset){
		fprintf(stderr, "Socket closed, try to reopen\n");
		close(sockfd);
		sockfd = -1;
		return NULL;
	}
	DBG("read %zd bytes\n", offset);
	if(sz) *sz = offset;
	return recvBuff;
}




static char encoding_table[] = {'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H',
                                'I', 'J', 'K', 'L', 'M', 'N', 'O', 'P',
                                'Q', 'R', 'S', 'T', 'U', 'V', 'W', 'X',
                                'Y', 'Z', 'a', 'b', 'c', 'd', 'e', 'f',
                                'g', 'h', 'i', 'j', 'k', 'l', 'm', 'n',
                                'o', 'p', 'q', 'r', 's', 't', 'u', 'v',
                                'w', 'x', 'y', 'z', '0', '1', '2', '3',
                                '4', '5', '6', '7', '8', '9', '+', '/'};
static int mod_table[] = {0, 2, 1};


unsigned char *base64_encode(const unsigned char *data,
                    size_t input_length,
                    size_t *output_length) {
	size_t i,j;
	*output_length = 4 * ((input_length + 2) / 3);
	unsigned char *encoded_data = malloc(*output_length);
	if (encoded_data == NULL) return NULL;
	for (i = 0, j = 0; i < input_length;) {
		uint32_t octet_a = i < input_length ? (unsigned char)data[i++] : 0;
		uint32_t octet_b = i < input_length ? (unsigned char)data[i++] : 0;
		uint32_t octet_c = i < input_length ? (unsigned char)data[i++] : 0;
		uint32_t triple = (octet_a << 0x10) + (octet_b << 0x08) + octet_c;
		encoded_data[j++] = encoding_table[(triple >> 3 * 6) & 0x3F];
		encoded_data[j++] = encoding_table[(triple >> 2 * 6) & 0x3F];
		encoded_data[j++] = encoding_table[(triple >> 1 * 6) & 0x3F];
		encoded_data[j++] = encoding_table[(triple >> 0 * 6) & 0x3F];
	}
	for (i = 0; i < (size_t)mod_table[input_length % 3]; i++)
		encoded_data[*output_length - 1 - i] = '=';
	return encoded_data;
}

void prepare_image(imbuf *buf){
	size_t W;
	unsigned char *b64 = NULL, *imdata = NULL;
	free_imbuf(buf);
	buf->data = capture_frame(&(buf->len));
	if(!buf->data){
		return;
	}
	DBG("image captured");
	size_t L = 0;
	imdata = getsz(buf, &L);
	if(!imdata){
		free_imbuf(buf);
		return;
	}

	b64 = base64_encode(imdata, L, &W);
	L = W;
	free_imbuf(buf);

	buf->data = malloc(L+LWS_SEND_BUFFER_PRE_PADDING+LWS_SEND_BUFFER_POST_PADDING);
	if(!buf->data){perror("malloc()"); free(b64); return;}
	memcpy(buf->data+LWS_SEND_BUFFER_PRE_PADDING, b64, L);
	free(b64);
	buf->len = L;
	DBG("image prepared");
}

void send_buffer(struct libwebsocket *wsi, imbuf *buf){
	if(!buf->data || !buf->len) return;
	size_t W = 0, L = buf->len;
	unsigned char *p = buf->data + LWS_SEND_BUFFER_PRE_PADDING;
	do{
		p += W; L -= W;
		W = libwebsocket_write(wsi, p, L, LWS_WRITE_TEXT);
	}while(W > 0 && W < L);
	free_imbuf(buf);
	DBG("image sent");
}

void free_imbuf(imbuf *buf){
	free(buf->data);
	memset(buf, 0, sizeof(imbuf));
}
