/* 
 * Server.c - part of client server job sync
 * 10/30/2014, Siddharth S
 */ 

#include <stdio.h>
#include <pthread.h>
#include <stdbool.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <errno.h>
#include <signal.h>
#include <stdlib.h>
#include <unistd.h>
#include <getopt.h>
#include "client_server.h"

int main(int argc, char **argv) 
{
	int broadcast_fd = 0;
	int master_socket = 0, child_socket = 0;
	int comm_port = 0, port_num = 0;
	struct sockaddr_in my_addr;	
	struct sockaddr_in broadcast_addr;
	struct sockaddr_in client_addr;
	msg_st *msg = NULL;
	msg_st dummy_msg;
	int option = 0;
	int reuse_sock = 1;
	int addr_len = 0;
    //char msg_str[MAX_MSG_STR_LEN];
	int rc = 0;

    int broadcast = 1;
    int numbytes = 0;

	rc = rc;

    /* set prog behaviour on recieving below Signals */
    signal(SIGTERM, cleanExit);
    signal(SIGINT, cleanExit);

	memset(&client_addr, 0, sizeof(client_addr));
	memset(&broadcast_addr, 0, sizeof(broadcast_addr));
	memset(&my_addr, 0, sizeof(my_addr));
	memset(&dummy_msg, 0, sizeof(msg_st));

	while ( (option = getopt(argc, argv, "hda:p:b:")) != -1) {

		switch(option) {
			default: 
				ERROR("%s", "Wrong arguments specified..Plz rerun with correct args");
				disp_server_help_msg();
				EXIT;
				break;

			case 'h': 
				/* help for usage */
				disp_server_help_msg();
				EXIT;
				break;

			case 'd':
				/* enable debugging */
				PRINT("%s", "Debugging mode is now enabled\n");
				debug_on = TRUE;
				break;

			case 'p':
				/* override default port number specified in conf.txt*/
				port_num = atoi(optarg);
				PRINT("Port number %d will be used for communication", port_num);
				break;

			case 'b':
				/* fetch broadcast address */
				PRINT("%s %s", "broadcast address to be used is:", optarg);
				if ( (inet_pton(ADDR_FAMILY, optarg, 
								&(broadcast_addr.sin_addr))) == 0) {
					ERROR("broadcast address %s %s", optarg,
						  "coudn't be converted using inet_pton");	
					PRINT("%s", "using default 255.255.255.255 subnet address now for broadcast");
					inet_pton(ADDR_FAMILY, "255.255.255.255", &(broadcast_addr.sin_addr));
				}
				break;
		}
	}

	if ( (argc == 1) || !port_num) {
		PRINT("%s", "None args passed, using default values");
		port_num = get_server_port_frm_file();
		DEBUG("%s %d", "Port numbr from file is:", port_num);
		
		/* also set default broadcast address */
		inet_pton(ADDR_FAMILY, "255.255.255.255", &(broadcast_addr.sin_addr));
	}

	comm_port = port_num+1;

	broadcast_fd = socket(ADDR_FAMILY, SOCK_DGRAM, 0);
	if (RC_NOTOK(broadcast_fd)) {
		ERROR("%s errno: %s", "while creating socket.", strerror(errno));
		exit(0);
	}

    /* now set remaininmg attributes of broadcast address struct */
    broadcast_addr.sin_family = ADDR_FAMILY;
    broadcast_addr.sin_port   = htons(port_num);

    /* this call is what allows broadcast packets to be sent: */
    if (setsockopt(broadcast_fd, SOL_SOCKET, SO_BROADCAST, 
                  &broadcast, sizeof(broadcast)) == -1) {
        ERROR("%s errno: %s", "error setting BROADCAST option for UDP socket", strerror(errno));
    }

    /* Server is UP, now send SERVER_UP to all connected clients */
	msg = calloc(1, sizeof(msg_st));
	if (!msg) {
		ERROR("%s", "calloc failed for msg_st for SERVER_UP msg");
	}

	msg->type = SERVER_UP;
	msg->len = 0;

	numbytes = sendto(broadcast_fd, msg, sizeof(msg_st), 0,
					 (struct sockaddr *) &broadcast_addr,
					 sizeof(broadcast_addr));
	
    if (RC_NOTOK(numbytes)) {
        ERROR("%s errno: %s", "sending BROADCAST message failed.", strerror(errno));
    } else {
		PRINT("%s %s", "Server Up Broadcast msg sent to:", 
							inet_ntoa(broadcast_addr.sin_addr));
		free_msg(msg);
	}

	master_socket = socket(ADDR_FAMILY, SOCK_STREAM, 0);
	if (RC_NOTOK(master_socket)) {
		ERROR("%s errno: %s", "while creating master socket.", strerror(errno));
		exit(0);
	}

	/* set socket to be reused */
	setsockopt(master_socket, SOL_SOCKET, SO_REUSEADDR,
							&reuse_sock, sizeof(reuse_sock));

    /* now set attributes of my address struct */
    my_addr.sin_family = ADDR_FAMILY;
	my_addr.sin_addr.s_addr = INADDR_ANY;
    my_addr.sin_port   = htons(comm_port);

	if (bind(master_socket, (struct sockaddr*)&my_addr,
			sizeof(struct sockaddr)) == -1) {
		ERROR("%s %s", "Bind failure for master_socket. errno.:", 
													strerror(errno));
	}

	if (listen(master_socket, MAX_CLIENTS) == -1) {
		ERROR("%s %s", "listen of master_sock failed. errno. :", 
												strerror(errno));
	}

	addr_len = sizeof(struct sockaddr);
	while(TRUE) {
	
		PRINT("%s", "waiting for any msg from clients");

		child_socket = accept(master_socket,
							 (struct sockaddr *)&client_addr,
							 (socklen_t *)&addr_len);
		if (RC_NOTOK(child_socket)) {
			ERROR("%s %s", "accept failed. errno. ", strerror(errno));
		} else {
			DEBUG("%s", "Server accept is succesfull");
			server_state = SERVER_BROADCAST_SENT;
			rc = action_on_server_state(child_socket, msg, 
										server_state, 
										&client_addr, FALSE);
			if (RC_NOTOK(rc)) {
				ERROR("%s: %s", FUNC, 
					"failed for state SERVER_BROADCAST_SENT");
			}
		}
	}

	/* Though it will never reach here, but just for completion sake */
    return 0;
}
