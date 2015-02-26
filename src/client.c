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
	FILE *fp = NULL;
	char str_chr[6];

	/* initialize structures */
	memset(&msg_dummy, 0, sizeof(msg_st));
	memset(&server_addr, 0, sizeof(server_addr));
	memset(&my_addr, 0, sizeof(my_addr));

	while ((option = getopt(argc, argv, "hda:p:g:")) != -1) {

		switch(option) {
			default: 
				fprintf(stderr,
				"Wrong argument :%c specified..Plz rerun with correct args\n", 
																	option);
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
				fprintf(stdout, "   debugging is enabled\n");
				clnt_debug_on = TRUE;
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
				req_grp_id = atoi(optarg);
				if (req_grp_id >= JOB_MAX) {
					ERROR("Group Id : %d is not supported. reenter correct Group Id", 
						  req_grp_id);
					EXIT;
				}
				break;
		}
	}

	if (port_addr_counter == 1) {
		ERROR("%s", "server prog is missing either port or addr argument..!");
		EXIT;
	}

	if ((argc == 1) || use_def_addr) {
		
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

	/* Find out in which mode server is operating */
	fp = fopen("op_mode.txt", "r");
	if (fp) {
		if (fgets(str_chr, 6, fp)!= NULL) {
			if (strncmp(str_chr, "TRUE", 4) == 0) {
				is_multicast_supp = TRUE;
			} else {
				is_multicast_supp = FALSE;
			}
		}
		DEBUG("%s %s", "Server is operating in multicast:", 
					 is_multicast_supp?"TRUE":"FALSE");
	} else {
		fprintf(stderr, "Error opening op_mode.txt, exiting program");
		exit(0);
	}

	comm_port = port_num+1;
	server_addr.sin_family = ADDR_FAMILY; 
	server_addr.sin_port = htons(comm_port);
	memset(&server_addr.sin_zero, 0, 8); //zero the rest of the struct
	
	/* 
	 * Copy server address to global copy vars.
	 * These will be used to send EXIT signals to server for 
	 * cleanup
  	 */
	//inet_pton(AF_INET, &server_addr.sin_addr, 
	//		  &server_addr_copy.sin_addr);
	server_addr_copy.sin_addr.s_addr= inet_addr("173.39.53.172");
	server_addr_copy.sin_family = ADDR_FAMILY;
	server_addr_copy.sin_port   = htons(comm_port);
	memset(&server_addr_copy.sin_zero, 0, 8);

	if (is_multicast_supp) {
		comm_socket = socket(AF_INET, SOCK_DGRAM, 0);
		if (RC_NOTOK(comm_socket)) {
			ERROR("%s %s", "UDP communication socket creation failed. errno:",
															strerror(errno));
			goto err_exit;
		}

		setsockopt(comm_socket, SOL_SOCKET, SO_REUSEADDR,
	               &reuse, sizeof(reuse));
		comm_sock_copy = comm_socket;	

	} else {
		comm_socket = socket(AF_INET, SOCK_STREAM, 0);
		if (RC_NOTOK(comm_socket)) {
			ERROR("%s %s", "comm socket creation failed. errno:",
							strerror(errno));
			goto err_exit;
		}

		comm_sock_copy = comm_socket;
		addr_len = sizeof(struct sockaddr);
		if(connect(comm_socket, (struct sockaddr*)&server_addr,
					addr_len) == -1) {
			DEBUG("%s %s", "SERVER is not UP, client exiting with errno.:",
						   strerror(errno));
		}
	}
	
	/* Register signal events */
	set_signal_handler(cleanExit_client);


	/* Start the Client FSM */
	client_state = CLIENT_INIT;
	
	while (client_state != CLIENT_EXIT) {
			
		rc = action_on_client_state(comm_socket, 
									client_state, &server_addr); 
		if (RC_NOTOK(rc)) {
			ERROR("%s %s", "action_on_client_state() failed for", 
								get_client_state_str(client_state));
			DIE_ON_PIPE(errno);
		}
	}

    /* 
	 * Recieving is done, close the communication socket
	 * Though ihis is nvr going to happen, until someone kills the program 
	 */
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
