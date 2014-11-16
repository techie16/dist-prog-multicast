/*
 * Client.c - part of client server job sync
 * 10/31/2014, Siddharth S
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdbool.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <signal.h>
#include <sys/time.h>
#include <getopt.h>
#include "client_server.h"

int main(int argc, char *argv[])
{
	
	msg_st *msg = NULL;
	msg_st msg_dummy;
	int option = 0, port_num = 0, comm_port = 0;
    struct sockaddr_in server_addr; // connectors address information
    struct sockaddr_in my_addr; // my address information
	char addr_str[20];
	int broadcast_socket = 0, comm_socket = 0;
    int reuse = 1;
	int port_addr_counter = 0 ;
	bool use_def_addr = TRUE;
	bool is_server_up = TRUE;
    int addr_len = 0, numbytes = 0;
	int rc = 0;	
	int group_id = 0;

	rc = rc;
    /* set prog behaviour on recieving below Signals */
    signal(SIGTERM, cleanExit);
    signal(SIGINT, cleanExit);

	/* initialize structures */
	memset(&msg_dummy, 0, sizeof(msg_st));
	memset(&server_addr, 0, sizeof(server_addr));
	memset(&my_addr, 0, sizeof(my_addr));

	while ((option = getopt(argc, argv, "hda:p:g:")) != -1) {

		switch(option) {
			default: 
				fprintf(stderr, "   Wrong arguments specified..Plz rerun with correct args\n");
				disp_client_help_msg();
				goto err_exit;
				break;

			case 'h': 
				/* help for usage */
				disp_client_help_msg();
				goto err_exit;
				break;

			case 'd':
				/* enable debugging */
				fprintf(stdout, "   debugging enabled\n");
				debug_on = TRUE;
				break;

			case 'p':
				/* override default port number specified in conf.txt*/
				port_num = atoi(optarg);
				port_addr_counter++;
				fprintf(stdout, "   Port number %d will be used for communication\n", port_num);
				break;

			case 'a':
				/* fetch server address */
				DEBUG("%s %s", "server address to be used is:", optarg);
				port_addr_counter++;
				#if 0
				server_addr.sin_addr.s_addr = inet_addr(optarg);
				if (server_addr.sin_addr.s_addr == (in_addr_t)(-1)) {
					ERROR("%s %s", "converting IPaddress into in_addr. errno:", 
														strerror(errno));
					use_def_addr = TRUE;
				} else {
					use_def_addr = FALSE;
				}
				#endif
				if ( (inet_pton(ADDR_FAMILY, optarg, 
									&(server_addr.sin_addr))) == 0) {
					ERROR("Server address %s %s", optarg, 
							"coudnt be converted using inet_pton");	
					use_def_addr = TRUE;
				} else {
					use_def_addr = FALSE;
				}
				break;

			case 'g':
				DEBUG("%s %s", "Client will request for group:", optarg);
				group_id = atoi(optarg);
				break;
		}
	}

	if (port_addr_counter == 1) {
		ERROR("%s", "server prog is missing either port or addr argument..!");
		EXIT;
	}

	if ( (argc == 1) || use_def_addr) {
		
		PRINT("%s", "None args passed, using default values");
		if (get_server_info_frm_file(addr_str, &port_num) == -1) {
			ERROR("%s: %s", __FUNCTION__, "get_server_info_frm_file failed");
			EXIT;
		} else {
			if ((inet_pton(ADDR_FAMILY, addr_str,
										 &(server_addr.sin_addr))) == 0) {
				ERROR("Server address %s %s", addr_str,
						"coudnt be converted using inet_pton");
			} else {
				DEBUG("default address: %s and port: %d will be used", 
														addr_str, port_num);
			}
		}
	}

	comm_port = port_num+1;

	comm_socket = socket(AF_INET, SOCK_STREAM, 0);
	if (RC_NOTOK(comm_socket)) {
		ERROR("%s %s", "TCP communication socket creation failed. errno:",
															strerror(errno));
		goto err_exit;
	}

	server_addr.sin_family = ADDR_FAMILY; 
	server_addr.sin_port = htons(comm_port);
	memset(&server_addr.sin_zero, 0, 8); //zero the rest of the struct

	setsockopt(comm_socket, SOL_SOCKET, SO_REUSEADDR,
			               &reuse, sizeof(reuse));

	//check if server is up and available now
	addr_len = sizeof(struct sockaddr);
	if(connect(comm_socket, (struct sockaddr*)&server_addr, 
							addr_len) == -1) {
		PRINT("%s %s", 
			 "SERVER is not UP, moving client to listening state. err:",
	         strerror(errno));
		is_server_up = FALSE;
	} else {
		is_server_up = TRUE;
	}

	if (!is_server_up) {
		broadcast_socket = socket(ADDR_FAMILY, SOCK_DGRAM, 0);
	    if (RC_NOTOK(broadcast_socket)) {
			ERROR("%s %s", "socket creation failed. errno:", strerror(errno));
			goto err_exit;
    	}
  
		/* 
		 * get rid of "address already in use" error message by 
    	 * setting the socket option to REUSE 
		 */
    	setsockopt(broadcast_socket, SOL_SOCKET, SO_REUSEADDR,
        	       &reuse, sizeof(reuse));

	    my_addr.sin_family = ADDR_FAMILY; // host byte order
    	my_addr.sin_port = htons(port_num);
	    my_addr.sin_addr.s_addr = htonl(INADDR_ANY); // automatically fill with my IP
    	memset(&(my_addr.sin_zero), 0, 8); // zero the rest of the struct

    	if (bind(broadcast_socket, (struct sockaddr *)&my_addr,
					            sizeof(struct sockaddr)) == -1) {
			ERROR("%s %s", "error while binding to broadcast socket. errno:",
															strerror(errno));
    		goto err_exit;
    	}

	    addr_len = sizeof(struct sockaddr);
    	PRINT("%s %d", "listening for any broadcast msg from server on port:",
			 														port_num);
		msg = calloc(1, sizeof(msg_st));
		if (!msg) {
			ERROR("%s", "couln't alloc memory to msg");
			goto err_exit;
		}
		numbytes = recvfrom(broadcast_socket, msg, MAX_BROADCAST_PKT_LEN,
							0, (struct sockaddr *)&server_addr,
							(socklen_t *)&addr_len);
    	if (RC_NOTOK(numbytes)) {
			ERROR("%s %s", "Broadcast msg recv failure. errno:", 
												strerror(errno));
        	goto err_exit;
		}

    	PRINT("Recieved %s and msg_len: %d from: %s",
										get_msg_type_str(msg->type),
										msg->len,
										inet_ntoa(server_addr.sin_addr));
		if (broadcast_socket) {
			/* we are done, lets close it down */
			close(broadcast_socket);
			broadcast_socket = 0;
		}
		
		/* Sleep for 2 seconds, so that server shall b ready for listening */
		sleep(2);
		
		/* reset connecting port of server_addr */
		server_addr.sin_port = htons(comm_port);
		addr_len = sizeof(struct sockaddr);
		if(connect(comm_socket, (struct sockaddr*)&server_addr, 
											addr_len) == -1) {
			ERROR("%s%s", 
			 		"Unable to connect to Server, exiting..! errno.: ",
	         		strerror(errno));
			EXIT;
		} else {
			DEBUG("%s", "Connection to server is successfull");
		}
	}

    my_addr.sin_family = ADDR_FAMILY; // host byte order
    my_addr.sin_port = htons(comm_port); 
    my_addr.sin_addr.s_addr = htonl(INADDR_ANY); // automatically fill with my IP
    memset(&(my_addr.sin_zero), 0, 8); // zero the rest of the struct

	msg = calloc(1, sizeof(msg_st));
	msg->type = REGISTER_CLIENT;
	msg->len = 0;
	msg->group_id = group_id;
	msg->hash_id = 0;
	client_state = CLIENT_INIT;

	rc = action_on_client_state(comm_socket, msg, client_state, &server_addr, 
								TRUE); 
	if (RC_NOTOK(rc)) {
		ERROR("%s %s", "action_on_client_state() failed for", 
							get_client_state_str(client_state));
	}

    /* recieving is done, close the broadcast listener socket */
	/* This is never going to happen, until someone kills the program */
	if (broadcast_socket) {
		close(broadcast_socket);
	} 
	if (comm_socket) {
		close(comm_socket);
	}
    return 0;

	err_exit:
		if (broadcast_socket) {
			close(broadcast_socket);
		} 
		if (comm_socket) {
			close(comm_socket);
		}
		return 0;
}
