/*
 * main.c
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



// for clearenv
#define _DEFAULT_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <syslog.h>

#include <signal.h>

#include <libwebsockets.h>

#include <stdint.h>			// int types
#include <sys/stat.h>		// read
#include <fcntl.h>			// read

#include <sys/wait.h>
#include <sys/prctl.h>

#include <pthread.h>

#include <sys/reboot.h>

#include "stepper.h"
#include "image.h"
#include "dbg.h"

#define MESSAGE_QUEUE_SIZE 3
#define MESSAGE_LEN        512

#define NETCONFIG  "/etc/conf.d/net"
#define CMDBUFLEN  (512)
// individual data per session
typedef struct{
	int num;
	int idxwr;
	int idxrd;
	char message[MESSAGE_QUEUE_SIZE][MESSAGE_LEN];
	int already_connected;
}per_session_data;

char *client_IP = NULL; // IP of first connected client

per_session_data global_queue;
pthread_mutex_t command_mutex, ip_mutex;

char cmd_buf[CMDBUFLEN] = {0};
volatile int data_in_buf = 0; // signals that there's some data in cmd_buf to send to motors

void put_message_to_queue(char *msg, per_session_data *dat){
	int L = strlen(msg);
	if(dat->num >= MESSAGE_QUEUE_SIZE) return;
	dat->num++;
	if(L < 1 || L > MESSAGE_LEN - 1) L = MESSAGE_LEN - 1;
	strncpy(dat->message[dat->idxwr], msg, L);
	dat->message[dat->idxwr][L] = 0;
	if((++(dat->idxwr)) >= MESSAGE_QUEUE_SIZE) dat->idxwr = 0;
}

char *get_message_from_queue(per_session_data *dat){
	char *R = dat->message[dat->idxrd];
	if(dat->num <= 0) return NULL;
	if((++dat->idxrd) >= MESSAGE_QUEUE_SIZE) dat->idxrd = 0;
	dat->num--;
	return R;
}

char *getnet(){
	static char buf[256];
	int f = open(NETCONFIG, O_RDONLY);
	if(f < 0){
		perror("open");
		return "ERR";
	}
	size_t S = read(f, buf, 255);
	//DBG("read: %zd", S);
	if(S < 1){
		perror("read");
		close(f);
		return "EMPTY";
	}
	buf[S] = 0;
	close(f);
	//DBG("net: %s\n", buf);
	return buf;
}

/**
 * return 0 if given string is right IP/mask/broad
 */
int check_ip(char *ip){
    long comps;
    char *eptr;
    int i;
    DBG("check IP %s", ip);
    for(i = 0; i < 4; ++i){
        comps = strtol(ip, &eptr, 10);
        if(i != 3){
            if(!eptr || *eptr != '.') return 1;
        }else{
            if(eptr && *eptr) return 3;
        }
        if(comps < 0 || comps > 255) return 2;
        ip = eptr + 1;
    }
    DBG("OK");
    return 0;
}

int change_net(char *cmd){
	DBG("change net to %s\n", cmd);
	char buf[256], *ip, *mask, *gate, *brd, *ptr;
	if(!(ip = strstr(cmd, "ip="))) return 1;
	ip += 3;
	if(!(mask = strstr(cmd, "mask="))) return 2;
	mask += 5;
	if(!(gate = strstr(cmd, "gate="))) return 3;
	gate += 5;
	if(!(brd = strstr(cmd, "brd="))) return 4;
	brd += 4;
	if((ptr = strchr(ip, ' '))) *ptr = 0;
	if((ptr = strchr(mask, ' '))) *ptr = 0;
	if((ptr = strchr(gate, ' '))) *ptr = 0;
	if((ptr = strchr(brd, ' '))) *ptr = 0;
	if(check_ip(ip) || check_ip(mask) || check_ip(brd) || check_ip(gate)) return 5;
	snprintf(buf, 256, "config_eth0=\"%s netmask %s brd %s\"\nroutes_eth0=\"default via %s\"\n",
				ip, mask, brd, gate);
	if(unlink(NETCONFIG)) return 6;
	int f = open(NETCONFIG, O_WRONLY | O_CREAT, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
	if(f < 0) return 7;
	size_t l = strlen(buf);
	if(l != (size_t)write(f, buf, l)){
		close(f);
		return 8;
	}
	close(f);
	printf("%s\n", buf);
	if(system("service net.eth0 restart") < 0) return 9;
	return 0;
}
volatile int force_exit = 0;

//**************************************************************************//
/**
 * Process buffer with incoming commands
 * Protocol:
 * Dcd - button clicked
 * Ucd - button released
 *    where c -- X or Y (moving coordinate)
 *          d -- + or - (moving direction)
 *
 * Sxxx - set steppers speed to xxx
 * G - get steppers speed
 */
void process_buf(char *command){
	char que[512];
	#define GLOB_MESG(...)  do{snprintf(que, 512, __VA_ARGS__); put_message_to_queue(que, &global_queue);}while(0)
	int dir = 0, nlamp = 0;
	long X;
	void (*moveFN)(int, unsigned int) = NULL;
	switch (command[1]){
		case 'X':
			moveFN = Xmove;
		break;
		case 'L':
			if(command[2] == '1') nlamp = 1;
			else if(command[2] == '2') nlamp = 2;
		break;
		case '0': // turn off
			if(command[0] == 'D'){
				move_motor(0);
				set_lamp(1, 0);
				set_lamp(2, 0);
				GLOB_MESG("All off");
				return;
			}
		break;
	}
	if(moveFN) switch (command[2]){
		case '+':
			dir = 1;
		break;
		case '-':
			dir = -1;
		break;
	}
	switch (command[0]){
		case 'S': // change current speed
			X = strtol(&command[1], NULL, 10);
			set_motors_speed((int)X);
		break;
		case 'G': // get speed - send to client the value of current speed
			GLOB_MESG("curspd=%d", get_motors_speed());
		break;
		case 'E': // get end-switches
			GLOB_MESG("esw=%d", get_endsw());
		break;
		case 'L': // get lamp state
			GLOB_MESG("lamps=%d", getlamp());
		break;
		case 'D': // button pressed
			if(moveFN){
				moveFN(dir, 0); // move infinitely when button pressed
				GLOB_MESG("move motor to %s", (dir == 1) ? "+" : "-");
			}else if(nlamp){
				switch_lamp(nlamp);
				GLOB_MESG("lamp %d switched, state: %d", nlamp, getlamp());
			}else if(strcmp(command, "Dgetnet") == 0){ // get network configuration
				GLOB_MESG("net=%s", getnet());
			}else if(strncmp(command, "Dchnet", 6) == 0){
				if(change_net(command+7)) GLOB_MESG("Error! Can't change network settings");
			}else if(strcmp(command, "Dreboot") == 0){
				GLOB_MESG("REBOOT!");
				DBG("\n\nREBOOT\n\n");
				reboot(RB_AUTOBOOT);
			}
		break;
	/*	case 'U': // button released - stop motor
			if(moveFN){
				moveFN(0, 0);
				GLOB_MESG("stop motor");
			}
		break;*/
	}
}

#define MESG(X) do{if(dat) put_message_to_queue(X, dat);}while(0)
void websig(char *command, per_session_data *dat){
	size_t L;
	//DBG("get: %s", command);
	if(!command || !(L = strlen(command))){
		MESG("Empty command!");
		return;
	}
	if(command[0] == 'S'){
		MESG("Change speed");
		goto ret;
	}
	if(command[0] == 'G' || command[0] == 'E' || command[0] == 'L'){ // get speed
		goto ret;
	}
	if(command[0] != 'D' && command[0] != 'U'){
		MESG("Undefined command");
		return;
	}
	if(strcmp(command, "Dgetnet") == 0) goto ret;
	if(strncmp(command, "Dchnet", 6) == 0) goto ret;
	if(strcmp(command, "Dreboot") == 0) goto ret;
	if(L == 1){
		MESG("Broken command!");
		return;
	}
	if(command[1] == '0'){ // all OFF
		if(command[0] == 'D'){
			MESG("Turn off everything");
			goto ret;
		}else
			return;
	}
	if(command[1] != 'X' && command[1] != 'L'){ // error
		MESG("Undefined action required");
		return;
	}
	if(L != 3){
		MESG("Broken command!");
		return;
	}
ret:
	while(data_in_buf); // wait for execution of last command
	pthread_mutex_lock(&command_mutex);
	strncpy(cmd_buf, command, CMDBUFLEN);
	data_in_buf = 1;
	pthread_mutex_unlock(&command_mutex);
}

static void dump_handshake_info(struct libwebsocket *wsi){
	int n;
	static const char *token_names[] = {
		"GET URI",
		"POST URI",
		"OPTIONS URI",
		"Host",
		"Connection",
		"key 1",
		"key 2",
		"Protocol",
		"Upgrade",
		"Origin",
		"Draft",
		"Challenge",

		/* new for 04 */
		"Key",
		"Version",
		"Sworigin",

		/* new for 05 */
		"Extensions",

		/* client receives these */
		"Accept",
		"Nonce",
		"Http",

		/* http-related */
		"Accept:",
		"Ac-Request-Headers:",
		"If-Modified-Since:",
		"If-None-Match:",
		"Accept-Encoding:",
		"Accept-Language:",
		"Pragma:",
		"Cache-Control:",
		"Authorization:",
		"Cookie:",
		"Content-Length:",
		"Content-Type:",
		"Date:",
		"Range:",
		"Referer:",
		"Uri-Args:",


		"MuxURL",

		/* use token storage to stash these */

		"Client sent protocols",
		"Client peer address",
		"Client URI",
		"Client host",
		"Client origin",

		/* always last real token index*/
		"WSI token count"
	};
	char buf[256];
	int L = sizeof(token_names) / sizeof(token_names[0]);
	for (n = 0; n < L; n++) {
		if (!lws_hdr_total_length(wsi, n))
			continue;
		lws_hdr_copy(wsi, buf, sizeof buf, n);
		printf("    %s = %s\n", token_names[n], buf);
	}
}

/*
reasons:
0         LWS_CALLBACK_ESTABLISHED,
1         LWS_CALLBACK_CLIENT_CONNECTION_ERROR,
2         LWS_CALLBACK_CLIENT_FILTER_PRE_ESTABLISH,
3         LWS_CALLBACK_CLIENT_ESTABLISHED,
4         LWS_CALLBACK_CLOSED,
5         LWS_CALLBACK_CLOSED_HTTP,
6         LWS_CALLBACK_RECEIVE,
7         LWS_CALLBACK_CLIENT_RECEIVE,
8         LWS_CALLBACK_CLIENT_RECEIVE_PONG,
9         LWS_CALLBACK_CLIENT_WRITEABLE,
10        LWS_CALLBACK_SERVER_WRITEABLE,
11        LWS_CALLBACK_HTTP,
12        LWS_CALLBACK_HTTP_BODY,
13        LWS_CALLBACK_HTTP_BODY_COMPLETION,
14        LWS_CALLBACK_HTTP_FILE_COMPLETION,
15        LWS_CALLBACK_HTTP_WRITEABLE,
16        LWS_CALLBACK_FILTER_NETWORK_CONNECTION,
17        LWS_CALLBACK_FILTER_HTTP_CONNECTION,
18        LWS_CALLBACK_SERVER_NEW_CLIENT_INSTANTIATED,
19        LWS_CALLBACK_FILTER_PROTOCOL_CONNECTION,
20        LWS_CALLBACK_OPENSSL_LOAD_EXTRA_CLIENT_VERIFY_CERTS,
21        LWS_CALLBACK_OPENSSL_LOAD_EXTRA_SERVER_VERIFY_CERTS,
22        LWS_CALLBACK_OPENSSL_PERFORM_CLIENT_CERT_VERIFICATION,
23        LWS_CALLBACK_CLIENT_APPEND_HANDSHAKE_HEADER,
24        LWS_CALLBACK_CONFIRM_EXTENSION_OKAY,
25        LWS_CALLBACK_CLIENT_CONFIRM_EXTENSION_SUPPORTED,
26        LWS_CALLBACK_PROTOCOL_INIT,
27        LWS_CALLBACK_PROTOCOL_DESTROY,
28        LWS_CALLBACK_WSI_CREATE,
29        LWS_CALLBACK_WSI_DESTROY,
30        LWS_CALLBACK_GET_THREAD_ID,
31        LWS_CALLBACK_ADD_POLL_FD,
32        LWS_CALLBACK_DEL_POLL_FD,
33        LWS_CALLBACK_CHANGE_MODE_POLL_FD,
34        LWS_CALLBACK_LOCK_POLL,
35        LWS_CALLBACK_UNLOCK_POLL,
36        LWS_CALLBACK_OPENSSL_CONTEXT_REQUIRES_PRIVATE_KEY,
    LWS_CALLBACK_USER = 1000, // user code can use any including / above
*/

static int my_protocol_callback(struct libwebsocket_context *context,
			struct libwebsocket *wsi,
			enum libwebsocket_callback_reasons reason,
				void *user, void *in, _U_ size_t len){
	unsigned char buf[LWS_SEND_BUFFER_PRE_PADDING + MESSAGE_LEN +
				  LWS_SEND_BUFFER_POST_PADDING];
	unsigned char *p = &buf[LWS_SEND_BUFFER_PRE_PADDING];
	char client_name[128];
	char client_ip[128];
	char *M, *msg = (char*) in;
	per_session_data *dat = (per_session_data *) user;
	int L, W;
	void sendmsg(char *M){
		L = strlen(M);
		strncpy((char *)p, M, L);
		W = libwebsocket_write(wsi, p, L, LWS_WRITE_TEXT);
		if(L != W){
			lwsl_err("Can't write to socket");
		}
	}
	inline void parse_queue_msg(per_session_data *d){
		if((M = get_message_from_queue(d))){
			sendmsg(M);
		}
	}
	//DBG("my proto. reason: %d\n", reason);
	switch (reason) {
		case LWS_CALLBACK_ESTABLISHED:
			memset(dat, 0, sizeof(per_session_data));
			pthread_mutex_lock(&ip_mutex);
			libwebsockets_get_peer_addresses(context, wsi, libwebsocket_get_socket_fd(wsi),
				client_name, 127, client_ip, 127);
			if(!client_IP)
				client_IP = strdup(client_ip);
			else if(strcmp(client_IP, client_ip)){
				char buf[256];
				snprintf(buf, 255, "Already connected from %s.<br>Please, disconnect.", client_IP);
				DBG("Already connected\n");
				put_message_to_queue(buf, dat);
				snprintf(buf, 255, "Try of connection from %s", client_ip);
				put_message_to_queue(buf, &global_queue);
				dat->already_connected = 1;
			}
			pthread_mutex_unlock(&ip_mutex);
			libwebsocket_callback_on_writable(context, wsi);
		break;
		case LWS_CALLBACK_SERVER_WRITEABLE:
			if(dat->num || global_queue.num){
				parse_queue_msg(dat);
				if(!dat->already_connected)
					parse_queue_msg(&global_queue);
			}
			libwebsocket_callback_on_writable(context, wsi);
		break;
		case LWS_CALLBACK_RECEIVE:
			if(!dat->already_connected)
				websig(msg, dat);
			//else DBG("got message: %s\n", msg);
			//else return -1;
		break;
		case LWS_CALLBACK_FILTER_NETWORK_CONNECTION:
			libwebsockets_get_peer_addresses(context, wsi, (int)(long)in,
				client_name, 127, client_ip, 127);
			printf("Received network connection from %s (%s)\n", client_name, client_ip);
		break;
		case LWS_CALLBACK_FILTER_PROTOCOL_CONNECTION:
			printf("Client asks for %s\n", msg);
			dump_handshake_info(wsi);
		break;
		case LWS_CALLBACK_CLOSED:
			printf("Client disconnected\n");
			if(!dat->already_connected){
				pthread_mutex_lock(&ip_mutex);
				free(client_IP);
				client_IP = NULL;
				pthread_mutex_unlock(&ip_mutex);
			}
		break;
		default:
		//	DBG("XYproto unknown reason: %d\n", reason);
		break;
	}

	return 0;
}

static int improto_callback(_U_ struct libwebsocket_context *context,
			_U_ struct libwebsocket *wsi,
			enum libwebsocket_callback_reasons reason,
				void *user, void *in, _U_ size_t len){
	char client_name[128];
	char client_ip[128];
	char *msg = (char*) in;
	imbuf *buf = (imbuf*) user;
	//struct lws_tokens *tok = (struct lws_tokens *) user;
	switch (reason) {
		case LWS_CALLBACK_ESTABLISHED:
			printf("New Connection\n");
			memset(buf, 0, sizeof(imbuf));
			prepare_image(buf);
			libwebsocket_callback_on_writable(context, wsi);
		break;
		case LWS_CALLBACK_SERVER_WRITEABLE:
			if(buf->data){
				send_buffer(wsi, buf);
				libwebsocket_callback_on_writable(context, wsi);
			}
		break;
		case LWS_CALLBACK_RECEIVE:
			prepare_image(buf);
		break;
		case LWS_CALLBACK_FILTER_NETWORK_CONNECTION:
			libwebsockets_get_peer_addresses(context, wsi, (int)(long)in,
				client_name, 127, client_ip, 127);
			printf("Received network connection from %s (%s)\n",
							client_name, client_ip);
		break;
		case LWS_CALLBACK_FILTER_PROTOCOL_CONNECTION:
			printf("Client asks for %s\n", msg);
			dump_handshake_info(wsi);
		break;
		case LWS_CALLBACK_CLOSED:
			free_imbuf(buf);
			printf("Client disconnected\n");
		break;
	/*	case LWS_CALLBACK_GET_THREAD_ID:
			return pthread_self();
		break;*/
		default:
		//	DBG("Improto unknown reason: %d\n", reason);
		break;
	}

	return 0;
}

//**************************************************************************//
/* list of supported protocols and callbacks */
//**************************************************************************//
static struct libwebsocket_protocols protocols[] = {
	{
		"XY-protocol",				// name
		my_protocol_callback,		// callback
		sizeof(per_session_data),	// per_session_data_size
		MESSAGE_LEN,				// max frame size / rx buffer
		0, NULL, 0, 0
	},
	{
		"image-protocol",
		improto_callback,
		sizeof(imbuf),
		100000,
		0, NULL, 0, 0
	},
	{ NULL, NULL, 0, 0, 0, NULL, 0, 0} /* terminator */
};

//**************************************************************************//
void sighandler(_U_ int sig){
	force_exit = 1;
	printf("Exit\n");
}

void *websock_thread(_U_ void *buf){
	DBG("websock_thread");
	struct libwebsocket_context *context;
	int n = 0;
	int opts = 0;
	const char *iface = NULL;
	int syslog_options = LOG_PID | LOG_PERROR;
	//unsigned int oldus = 0;
	struct lws_context_creation_info info;
	int debug_level = 7;

	if(pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL)){
		force_exit = 1;
		return NULL;
	}

	memset(&info, 0, sizeof info);
	info.port = 9999;

	/* we will only try to log things according to our debug_level */
	setlogmask(LOG_UPTO (LOG_DEBUG));
	openlog("lwsts", syslog_options, LOG_DAEMON);

	/* tell the library what debug level to emit and to send it to syslog */
	lws_set_log_level(debug_level, lwsl_emit_syslog);

	info.iface = iface;
	info.protocols = protocols;
	info.extensions = libwebsocket_get_internal_extensions();
	info.ssl_cert_filepath = NULL;
	info.ssl_private_key_filepath = NULL;

	info.gid = -1;
	info.uid = -1;
	info.options = opts;

	context = libwebsocket_create_context(&info);
	if (context == NULL){
		lwsl_err("libwebsocket init failed\n");
		force_exit = 1;
		return NULL;
	}

	while(n >= 0 && !force_exit){
		n = libwebsocket_service(context, 500);
	}//while n>=0
	libwebsocket_context_destroy(context);
	lwsl_notice("libwebsockets-test-server exited cleanly\n");
	closelog();
	DBG("return webs_thre");
	return NULL;
}

static inline void main_proc(){
	DBG("main proc");
	pthread_t w_thread, s_thread;
	pthread_create(&w_thread, NULL, websock_thread, NULL);
	pthread_create(&s_thread, NULL, steppers_thread, NULL);

	while(!force_exit){
		pthread_mutex_lock(&command_mutex);
		if(data_in_buf) process_buf(cmd_buf);
		data_in_buf = 0;
		pthread_mutex_unlock(&command_mutex);
		usleep(100); // give another treads some time to fill buffer
	}
	DBG("stop threads");
	pthread_cancel(s_thread); // cancel steppers' thread
	pthread_cancel(w_thread);
	stop_motor();
	set_lamp(1, 0);
	set_lamp(2, 0);
	DBG("wait s_tr");
	pthread_join(s_thread, NULL);
	DBG("wait w_tr");
	pthread_join(w_thread, NULL); // wait for closing of libsockets thread
	DBG("return main_proc");
}

//**************************************************************************//
int main(_U_ int argc, _U_ char **argv){
	signal(SIGTERM, sighandler);	// kill (-15)
	signal(SIGINT, sighandler);		// ctrl+C
	signal(SIGQUIT, SIG_IGN);		// ctrl+\  .
	signal(SIGTSTP, SIG_IGN);		// ctrl+Z

	setup_pins();

	while(1){
		if(force_exit) return 0;
		pid_t childpid = fork();
		if(childpid){
			printf("Created child with PID %d\n", childpid);
			wait(NULL);
			printf("Child %d died\n", childpid);
		}else{
			prctl(PR_SET_PDEATHSIG, SIGTERM); // send SIGTERM to child when parent dies
			main_proc();
			return 0;
		}
	}
	return 0;
}
