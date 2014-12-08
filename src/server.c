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
#include <pthread.h>
#include <time.h>
#include <ncurses.h>
#include <sys/poll.h>
#include "client_server.h"

extern struct pollfd readfds[MAX_CLIENTS];

int main(int argc, char *argv[])
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
	int index = 0;
	int rc = 0;
	pthread_t send_t;
	pthread_t recv_t;
	pthread_t verify_thread;
	pthread_attr_t attr;
	int i = 0;
    int broadcast = 1;
    int numbytes = 0;
	int num_connection = 0;

	/* initialize structs */
	memset(&client_addr, 0, sizeof(client_addr));
	memset(&broadcast_addr, 0, sizeof(broadcast_addr));
	memset(&my_addr, 0, sizeof(my_addr));
	memset(&dummy_msg, 0, sizeof(msg_st));
	
	for (i = 0; i < MAX_CLIENTS; i++) {
		grp_data[i] = NULL;
		client_entry[i] = NULL;
	}

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

    /* set prog behaviour on recieving below Signals */
    set_signal_handler(cleanExit);

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
		free_msg(msg);
    } else {
		PRINT("%s %s", "Server Up.. Broadcast msg sent to subnet:", 
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
	my_addr.sin_addr.s_addr = htonl(INADDR_ANY);
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

	/* set thread to be used to detached state */
	rc = pthread_attr_init(&attr);
	if (rc) {
		ERROR("%s %s", "Thread's attribute init failed. errno. :", 
						strerror(errno));
	}

	rc = pthread_attr_setdetachstate(&attr, 
						PTHREAD_CREATE_DETACHED);
	if (rc) {
		ERROR("%s errno.: %s", "Thread coudnt be set as DETACHED thread", 
								strerror(errno));
	}

	/* create a thread that will chk if client is ALIVE via heartbeat */
	rc = pthread_create(&verify_thread, &attr,
						verify_client_hbeat, NULL);
	if (rc) {
		ERROR("verify_client_hbeat thread creation failed errno. :%s", 
			 strerror(errno));
	}

	/* create a send thread, it will communicate pkts to clients */
	rc = pthread_create(&send_t, &attr,
						send_thread, NULL);
	if (rc) {
		ERROR("send_thread creation failed errno. :%s", strerror(errno));
	}

	/* create a recv_thread that will recv if client has sent something*/
	rc = pthread_create(&recv_t, &attr,
						recv_thread, NULL);
	if (rc) {
		ERROR("recv_thread creation failed errno. :%s", 
			 strerror(errno));
	}

	/* 
	 * Destroy the attr created, as we are done with its use
	 */
	rc = pthread_attr_destroy(&attr);
	if (rc) {
		ERROR("%s %s", "attr of thread cudn't be destroyed", 
											strerror(errno));
	}

	/* reset i to ZERO */
	i = 0;

	while(TRUE) {

		addr_len = sizeof(struct sockaddr);
		memset(&client_addr, 0, sizeof(client_addr));
	
		child_socket = accept(master_socket,
						 (struct sockaddr *)&client_addr,
						 (socklen_t *)&addr_len);
		if (RC_NOTOK(child_socket)) {
			ERROR("%s %s", "accept failed. errno. ", 
							strerror(errno));
		} else {

			DEBUG("%s", "Server accept is succesfull");

			/* This would be a client registration request */
			msg = calloc(1, sizeof(msg_st));
			if (!msg) {
				ERROR("%s: %s %d", FUNC, "msg alloc failure at line", 
								 __LINE__);
				continue;
			}
            numbytes = recv(child_socket, msg, sizeof(msg_st), 0);              
            if (RC_NOTOK(numbytes)) {                             
            	ERROR("%s %s", "Registration recv() failed. errno.",       
                		       strerror(errno)); 
				free_msg(msg);
				continue;
            } else {
				/* 
				 * We need to check if its new registration req or
				 * A client simply went down and Up and thus requesting
				 * for re-registration 
				 */
				if (is_client_entry_exists(&client_addr, &index)) {
					/* update new info for existing client entry in db */
					upd_client_db_info(index, child_socket, 
										client_addr.sin_port);
					free_msg(msg);
					continue;
				} else {
					i = num_connection;
					num_connection++;
				}
			}
		}

		if (num_connection <= MAX_CLIENTS) {

            PRINT("%s %s %s", get_msg_type_str(msg->type),
                           "recieved from client: ", 
							inet_ntoa(client_addr.sin_addr)); 

			/* Save the recvd details to client_db */
			add_client_db_info(i, child_socket, msg->group_id, &client_addr);
			
			/* free the msg mem*/
			free_msg(msg);

			/* Also add this client_id to group_data db */
			rc = add_hash_id_to_grp(i, client_entry[i]->group_id, 
						  &(grp_data[client_entry[i]->group_id]));
			if (RC_NOTOK(rc)) {
				ERROR("%s", "malloc failure while adding hash_id to grp");
						continue;
			}

			ALERT("%s", "Press Enter to proceed to Job execution");
		} else {

			ALERT("%s", "Server has reached max connections permitted");
		}
	}

    return 0;
}
