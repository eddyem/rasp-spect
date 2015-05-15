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

#include <pthread.h>

#include "stepper.h"

#ifndef _U_
	#define _U_    __attribute__((__unused__))
#endif

#define MESSAGE_QUEUE_SIZE 3
#define MESSAGE_LEN        128
// individual data per session
typedef struct{
	int num;
	int idxwr;
	int idxrd;
	char message[MESSAGE_QUEUE_SIZE][MESSAGE_LEN];
}per_session_data;

per_session_data global_queue;
pthread_mutex_t command_mutex;

char cmd_buf[5] = {0};
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

int force_exit = 0;

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
	char que[33];
	int dir = 0;
	long X;
	void (*moveFN)(int, unsigned int) = NULL;
	switch (command[1]){
		case 'X':
			moveFN = Xmove;
		break;
		case 'Y':
			moveFN = Ymove;
		break;
	}
	switch (command[2]){
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
			snprintf(que, 32, "curspd=%d", get_motors_speed());
			put_message_to_queue(que, &global_queue);
		break;
		case 'D': // button pressed
			if(command[1] == '0') // go to start point for further moving to middle
				XY_gotocenter();
			else if(moveFN)
				moveFN(dir, 0); // move infinitely when button pressed
		break;
		case 'U': // button released - stop motor
			if(moveFN) moveFN(0, 0);
		break;
	}
}

#define MESG(X) do{if(dat) put_message_to_queue(X, dat);}while(0)
void websig(char *command, per_session_data *dat){
	size_t L;
	if(!command || !(L = strlen(command))){
		MESG("Empty command!");
		return;
	}
	if(command[0] == 'S'){
		MESG("Change speed");
		goto ret;
	}
	if(command[0] == 'G'){ // get speed
		goto ret;
	}
	if(command[0] != 'D' && command[0] != 'U'){
		MESG("Undefined command");
		return;
	}
	if(L == 1){
		MESG("Broken command!");
		return;
	}
	if(command[1] == '0'){
		if(command[0] == 'D'){
			MESG("Go to the middle. Please, wait!");
			goto ret;
		}else
			return;
	}
	if(command[1] != 'X' && command[1] != 'Y'){ // error
		MESG("Undefined coordinate");
		return;
	}
	if(L != 3){
		MESG("Broken command!");
		return;
	}
ret:
	while(data_in_buf); // wait for execution of last command
	pthread_mutex_lock(&command_mutex);
	strncpy(cmd_buf, command, 4);
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

static int my_protocol_callback(_U_ struct libwebsocket_context *context,
			_U_ struct libwebsocket *wsi,
			enum libwebsocket_callback_reasons reason,
				_U_ void *user, void *in, _U_ size_t len){
	unsigned char buf[LWS_SEND_BUFFER_PRE_PADDING + MESSAGE_LEN +
				  LWS_SEND_BUFFER_POST_PADDING];
	unsigned char *p = &buf[LWS_SEND_BUFFER_PRE_PADDING];
	char client_name[128];
	char client_ip[128];
	char *M, *msg = (char*) in;
	per_session_data *dat = (per_session_data *) user;
	int L, W;
	void parse_queue_msg(per_session_data *d){
		if((M = get_message_from_queue(d))){
			L = strlen(M);
			strncpy((char *)p, M, L);
			W = libwebsocket_write(wsi, p, L, LWS_WRITE_TEXT);
			if(L != W){
				lwsl_err("Can't write to socket");
			}
		}
	}
	//struct lws_tokens *tok = (struct lws_tokens *) user;
	switch (reason) {
		case LWS_CALLBACK_ESTABLISHED:
			printf("New Connection\n");
			memset(dat, 0, sizeof(per_session_data));
			libwebsocket_callback_on_writable(context, wsi);
		break;
		case LWS_CALLBACK_SERVER_WRITEABLE:
			if(dat->num == 0 && global_queue.num == 0){
				libwebsocket_callback_on_writable(context, wsi);
				return 0;
			}else{
				parse_queue_msg(dat);
				parse_queue_msg(&global_queue);
				libwebsocket_callback_on_writable(context, wsi);
			}
		break;
		case LWS_CALLBACK_RECEIVE:
			websig(msg, dat);
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
			printf("Client disconnected\n");
		break;
		default:
		break;
	}

	return 0;
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




static int improto_callback(_U_ struct libwebsocket_context *context,
			_U_ struct libwebsocket *wsi,
			enum libwebsocket_callback_reasons reason,
				_U_ void *user, void *in, _U_ size_t len){
	char client_name[128];
	char client_ip[128];
	char *msg = (char*) in;
	void send_image(){
		static int c = 0;
		struct stat stat_buf;
		unsigned char *buffer = NULL, *b64 = NULL;
		unsigned char *p = NULL;
		int fd;
		size_t L, W;
		if(c) fd = open("leaf.jpg", O_RDONLY);
		else  fd = open("leaf1.jpg", O_RDONLY);
		c = !c;
		if(fd < 0){
			lwsl_err("Can't open image file");
			return;
		}
		fstat(fd, &stat_buf);
		L = stat_buf.st_size;
		//printf("image size (c=%d): %zd\n", c, L);
		buffer = malloc(L);
		if(!buffer) return;
		if(L != (size_t)read(fd, buffer, L)){printf("err\n"); goto ret;}
		b64 = base64_encode(buffer, L, &W);
		L = W; free(buffer);
		buffer = malloc(L+LWS_SEND_BUFFER_PRE_PADDING+LWS_SEND_BUFFER_POST_PADDING);
		if(!buffer){printf("malloc\n"); free(b64); return;}
		memcpy(buffer+LWS_SEND_BUFFER_PRE_PADDING,b64, L);
		free(b64);
		p = buffer + LWS_SEND_BUFFER_PRE_PADDING;
		W = 0;
		do{
			p += W;
			L -= W;
			W = libwebsocket_write(wsi, p, L, LWS_WRITE_TEXT);
			//printf("write: %zd (L=%zd)\n", W, L);
		}while(W>0 && W!=L);
		if(W<=0) printf("<0\n");
		//W = libwebsocket_write(wsi, p, L, LWS_WRITE_BINARY);
		if(L != W){
			printf("err: needed: %zd, writed %zd\n", L, W);
			//lwsl_err("Can't write image to socket");
		}
	ret:
		free(buffer);
		close(fd);
	}
	//struct lws_tokens *tok = (struct lws_tokens *) user;
	switch (reason) {
		case LWS_CALLBACK_ESTABLISHED:
			printf("New Connection\n");
			send_image();
			libwebsocket_callback_on_writable(context, wsi);
		break;
		case LWS_CALLBACK_SERVER_WRITEABLE:
			libwebsocket_callback_on_writable(context, wsi);
		break;
		case LWS_CALLBACK_RECEIVE:
			send_image();
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
			printf("Client disconnected\n");
		break;
		default:
		break;
	}

	return 0;
}

//**************************************************************************//
/* list of supported protocols and callbacks */
//**************************************************************************//
static struct libwebsocket_protocols protocols[] = {
	{
		"image-protocol",
		improto_callback,
		0,
		100,
		0, NULL, 0, 0
	},
	{
		"XY-protocol",				// name
		my_protocol_callback,		// callback
		sizeof(per_session_data),	// per_session_data_size
		10,							// max frame size / rx buffer
		0, NULL, 0, 0
	},
	{ NULL, NULL, 0, 0, 0, NULL, 0, 0} /* terminator */
};

//**************************************************************************//
void sighandler(_U_ int sig){
	force_exit = 1;
}

void *websock_thread(_U_ void *buf){
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
	return NULL;
}

//**************************************************************************//
int main(_U_ int argc, _U_ char **argv){
	pthread_t w_thread, s_thread;
	//size_t L;

	signal(SIGTERM, sighandler);	// kill (-15)
	signal(SIGINT, sighandler);		// ctrl+C
	signal(SIGQUIT, SIG_IGN);		// ctrl+\  .
	signal(SIGTSTP, SIG_IGN);		// ctrl+Z

	pthread_create(&w_thread, NULL, websock_thread, NULL);
	pthread_create(&s_thread, NULL, steppers_thread, NULL);

	while(!force_exit){
		pthread_mutex_lock(&command_mutex);
		if(data_in_buf) process_buf(cmd_buf);
		data_in_buf = 0;
		pthread_mutex_unlock(&command_mutex);
		usleep(100); // give another treads some time to fill buffer
	}
	pthread_cancel(s_thread); // cancel steppers' thread
	pthread_join(s_thread, NULL);
	steppers_relax();
	pthread_join(w_thread, NULL); // wait for closing of libsockets thread
	return 0;
}
