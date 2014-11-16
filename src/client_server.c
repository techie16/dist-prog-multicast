/*
 * Client_server.c - 
 * File contains function definition required in both 
 * client and server programs
 * 10/30/2014, Siddharth S
 */ 

#include <stdio.h>
#include <pthread.h>
#include <stdbool.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <errno.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>
#include "client_server.h"

client_db_st *head = NULL, *current = NULL;
int debug_on = FALSE;
client_state_en client_state = CLIENT_RES;
server_state_en server_state = SERVER_RES;


void cleanExit(int signum){
    printf("Program exiting with signum: %d\n", signum);
    exit(0);
}

char * get_msg_type_str (msg_type_en msg_type) 
{
    switch(msg_type) {

        case MSG_RES:
            return "MSG_RES";

        case SERVER_UP:
            return "SERVER_UP";

        case REGISTER_CLIENT:
			return "REGISTER_CLIENT";

        case ACK_FRM_SERVER:
			return "ACK_FRM_SERVER";    

        case CLIENT_DOWN:
			return "CLIENT_DOWN";

        default:
			return "UNKNOWN_TYPE";
    }
	return "UNKNOWN_TYPE";
}

char * get_client_state_str(client_state_en client_state_arg) 
{
	
	switch(client_state_arg) {
	 	case CLIENT_RES:
			return "CLIENT_RES";

		case CLIENT_INIT:
			return "CLIENT_INIT";

		case CLIENT_REG_SENT:
			return "CLIENT_REG_SENT";

		case CLIENT_ACK_WAIT:
			return "CLIENT_ACK_WAIT";

		case CLIENT_ACK_OK:
			return "CLIENT_ACK_OK";

		case CLIENT_EXIT:
			return "CLIENT_EXIT";
	
		default:
			return "UNKNOWN_CLIENT_STATE";
	}	
}

char * get_server_state_str(server_state_en server_state_arg) 
{
	
	switch(server_state_arg) {
	 	case SERVER_RES:
			return "CLIENT_RES";

		case SERVER_INIT:
			return "CLIENT_INIT";

		case SERVER_BROADCAST_SENT:
			return "SERVER_BROADCAST_SENT";

		case SERVER_REG_WAIT:
			return "SERVER_REG_WAIT";

		case SERVER_REG_RECV:
			return "SERVER_REG_RECV";

		case SERVER_ACK_SENT:
			return "SERVER_ACK_SENT";

		case SERVER_EXIT_WAIT:
			return "SERVER_EXIT_WAIT";

		case SERVER_EXIT_RECV:
			return "SERVER_EXIT_RECV";

		default:
			return "SERVER_UNKNOWN_STATE";
	}	

	return "SERVER_UNKNOWN_STATE";
}

void initialize_addr_struct (struct sockaddr_in *addr, int port_num) 
{

    addr->sin_family = ADDR_FAMILY;
    addr->sin_addr.s_addr = htonl(INADDR_ANY);
    addr->sin_port = htons(port_num);
}

void add_entry_to_db (struct in_addr client_addr, int port_num, int grp) 
{

	client_db_st *entry = NULL;
	entry = (client_db_st *) malloc(sizeof(client_db_st));

	printf("Sidd: entry to add.. size to alloc: %zd\n", sizeof(client_db_st));	
	if (!entry) {
		fprintf(stderr, "malloc failure, while creating entry \n");
	}
	memset(entry, 0, sizeof(client_db_st));

	entry->port = port_num;
	printf("Sidd: entry ..port: %d\n", port_num); // addr: %ld, grp: %d\n", port_num, client_addr, grp);
	entry->client_address = client_addr;
	entry->group = grp;
	entry->next = NULL;

	if (head != NULL) {
		current->next = entry;
		current = entry;
	} else {
		/* this is the 1st entry */
		head = entry;
		current = head;
	}

	printf("Sidd: entry addition success\n");
}

int count_total (void) 
{
	int count = 0;
	client_db_st *temp = head;

	if (!temp) {
		return 0;
	}

	while (temp != NULL) {
		count++;
		temp = temp->next;
	}
	return count;
}

void print_out(const char* format, ... )
{
    va_list args;
	fprintf(stdout, "***INFO: ");
    va_start(args, format);
    vfprintf(stdout, format, args );
    va_end(args);
	fprintf(stdout, "\n");
}

void print_debug(const char* format, ... ) 
{
	if (debug_on) {
	    va_list args;
		fprintf(stdout, "***DEBUG: ");
	    va_start(args, format);
    	vfprintf(stdout, format, args );
	    va_end(args);
		fprintf(stdout, "\n");
	}
}

void print_error(const char* format, ... ) 
{
    va_list args;
	fprintf(stderr, "***ERROR: ");
    va_start(args, format);
    vfprintf(stderr, format, args );
    va_end(args);
	fprintf(stderr, "\n");
}

int get_server_info_frm_file (char *addr, int *port_num) 
{
	FILE *fp = NULL;
	char port_str[100];
	char addr_str[100];
	char *index_str;
	char *nline_index = NULL;
	int rc = 0;

	fp = fopen("conf.txt", "r");
	if (!fp) {
		ERROR("%s", "opening file: conf.txt resulted in error");
		return -1;
	}

	fgets(addr_str, 100, fp);
	fgets(port_str, 100, fp);

	if (addr) {
		if (strstr(addr_str, "server_addr") !=  NULL) {
			if ((index_str = strstr(addr_str, "=")) == NULL) {
				rc = -1;
				ERROR("%s", "Server Port String not found");
			} else {
				strcpy(addr, index_str+1);
				nline_index = strchr(addr, '\n');
				*nline_index = '\0';
				DEBUG("%s: %s %s", __FUNCTION__, "Server Address:", addr);
			}
		} else {
			ERROR("%s", "server_addr attribute not found in file");
			rc = -1;
		}
	}

	if (port_num) {
		if (strstr(port_str, "server_port") !=  NULL) {
			if ((index_str = strstr(port_str, "=")) == NULL) {
				rc = -1;
				ERROR("%s", "Server Port String not found");
			} else {
				DEBUG("%s: %s %s", __FUNCTION__, "Port nubr in str:", index_str+1);
				*port_num = atoi(index_str+1);
			}
		} else {
			ERROR("%s", "server_port attribute not found in file");
			rc = -1;
		}
	}

	fclose(fp);
	return rc;
}

void free_msg (msg_st *msg) 
{
	
	if (!msg) {
		return;
	}

	free(msg);
	msg = NULL;
}

int get_server_port_frm_file (void) 
{
	FILE *fp = NULL;
	char port_str[100];
	char addr_str[100];
	char *index_str;
	int port_num = 0;

	fp = fopen("conf.txt", "r");
	if (!fp) {
		ERROR("%s", "opening file: conf.txt resulted in error");
		return -1;
	}

	fgets(addr_str, 100, fp);
	fgets(port_str, 100, fp);

	if (strstr(port_str, "server_port") !=  NULL) {
		if ((index_str = strstr(port_str, "=")) == NULL) {
			port_num = -1;
			ERROR("%s", "Server Port String not found");
		} else {
			//DEBUG("%s: %s %s", __FUNCTION__, "Port nubr in str:", index_str+1);
			port_num = atoi(index_str+1);
		}
	} else {
		ERROR("%s", "server_port attribute not found in file");
		port_num = -1;
	}

	fclose(fp);
	return port_num;
}

int action_on_client_state(int socket_fd, msg_st *recv_msg,
						   client_state_en client_state_arg,	 
		                   struct sockaddr_in *addr, bool send_on)
{
	int rc = 0;
	int numbytes = 0;
	msg_st *msg = NULL;
	msg_st dummy_msg;
		
	memset(&dummy_msg, 0, sizeof(msg_st));

	if (send_on) {
		/* for send case, recv_msg shudnt be null */
		if (!recv_msg) {
			ERROR("%s %s", FUNC, "recv_msg is NULL");
			return ERR_CODE;
		}
	}

	/* chk if socket_fd is valid ? */
	if (!socket_fd) {
		ERROR("%s %s", FUNC, "socket_fd is not valid");
		return ERR_CODE;
	}
	
	switch (client_state_arg) {

		case CLIENT_INIT:
			/* CLient now shud send a registration req to server */
			numbytes = send(socket_fd, recv_msg, sizeof(recv_msg), 0);
			if (RC_NOTOK(numbytes)) {
				ERROR("%s %s", "Registration msg cudn't be send to server. errno: ", strerror(errno));
				return ERR_CODE;
			} else {
				PRINT("%s %s for group_id: %d", 
						"Registartion msg sent to server: ", 
						inet_ntoa(addr->sin_addr), recv_msg->group_id);
				client_state = CLIENT_REG_SENT;
				rc = action_on_client_state(socket_fd, msg, client_state, 
										   addr, FALSE);
				if (RC_NOTOK(rc)) {
					ERROR("%s: %s %s", __FUNCTION__, "failed for state: ",
							get_client_state_str(client_state));
					return ERR_CODE;
				} else {
					DEBUG("%s %s", "Action requested for client_state", 
								get_client_state_str(client_state));
				}
				free_msg(recv_msg);
			}
			break;

		case CLIENT_REG_SENT:
		case CLIENT_ACK_WAIT:
			DEBUG("%s: for state: %s", __FUNCTION__, 
				 get_client_state_str(client_state));
			numbytes = recv(socket_fd, &dummy_msg,
							MAX_BROADCAST_PKT_LEN, MSG_PEEK);
			if (RC_NOTOK(numbytes)) {
                ERROR("%s: %s %s", __FUNCTION__, 
							"dummy_msg recv() failed. errno. ", 
							 strerror(errno));
				return ERR_CODE;
            } else {
                msg = calloc(1, sizeof(msg_st)+dummy_msg.len);
                numbytes = recv(socket_fd, msg,
                                sizeof(msg_st)+dummy_msg.len, 0);
                if (RC_NOTOK(numbytes)) {
                    ERROR("%s %s %s", __FUNCTION__, 
									"recv() failed. errno. ", strerror(errno));
					return ERR_CODE;
                } else {
					client_state = CLIENT_ACK_OK;
                    PRINT("%s %s %s", get_msg_type_str(msg->type), 
									  "recieved from server:",
                                            inet_ntoa(addr->sin_addr));
					free_msg(msg);
				}
			}
			break;

		case CLIENT_ACK_OK:

			break;

		case CLIENT_EXIT:
			break;

		default:
			break;
	}
	return rc;
}

int action_on_server_state(int socket_fd, msg_st *recv_msg,
						   server_state_en server_state_arg,	 
		                   struct sockaddr_in *addr, bool send_on)
{
	int rc = 0;
	int numbytes = 0;
	msg_st *msg = NULL;
	msg_st dummy_msg;
		
	memset(&dummy_msg, 0, sizeof(msg_st));

	if (send_on) {
		/* for send case, recv_msg shudnt be null */
		if (!recv_msg) {
			ERROR("%s %s", FUNC, "recv_msg is NULL");
			return ERR_CODE;
		}
	}

	/* chk if socket_fd is valid ? */
	if (!socket_fd) {
		ERROR("%s %s", FUNC, "socket_fd is not valid");
		return ERR_CODE;
	}
	
	switch (server_state_arg) {

		case SERVER_INIT:
			/* Nothing to be done for now*/
			break;
		
		case SERVER_BROADCAST_SENT:
			DEBUG("%s called for state: %s", FUNC, 
				 get_server_state_str(server_state));
			numbytes = recv(socket_fd, &dummy_msg,
							MAX_BROADCAST_PKT_LEN, MSG_PEEK);
			if (RC_NOTOK(numbytes)) {
                ERROR("%s: %s %s", FUNC, 
							"dummy_msg recv() failed. errno. ", 
							 strerror(errno));
				return ERR_CODE;

            } else {
				DEBUG("%s %d length", "Registration request DATA part is of", 
														dummy_msg.len);
                msg = calloc(1, sizeof(msg_st)+dummy_msg.len);
                numbytes = recv(socket_fd, msg,
                                sizeof(msg_st)+dummy_msg.len, 0);
                if (RC_NOTOK(numbytes)) {
                    ERROR("%s %s %s", FUNC, 
									"recv() failed. errno. ", strerror(errno));
					return ERR_CODE;
                } else {

                    PRINT("%s %s %s", get_msg_type_str(msg->type), 
									  "recieved from client:",
                                            inet_ntoa(addr->sin_addr));
					server_state = SERVER_REG_RECV;
					free_msg(msg);

					/* Now prepaper a server_ack msg */
					msg = calloc(1, sizeof(msg_st));
					msg->type = ACK_FRM_SERVER;
					msg->group_id = 1;
					msg->hash_id = 1;
		
					PRINT("%s%s", "Now sending ACK_FRM_SERVER to client: ", 
												inet_ntoa(addr->sin_addr));
					rc = action_on_server_state(socket_fd, msg, 
											server_state, addr, TRUE);
					if (RC_NOTOK(rc)) {
						ERROR("%s: %s", FUNC, 
							 "failed for state: SERVER_REG_RECV");
						return ERR_CODE;
					} else {
						DEBUG("%s %s", FUNC, 
							"call success for state SERVER_REG_RECV");
						fprintf(stdout, 
							"\n*******************************************\n");
					}
				}
			}
			break;

		case SERVER_REG_WAIT:
		case SERVER_REG_RECV:
			numbytes = send(socket_fd, recv_msg, sizeof(recv_msg), 0);
			if (RC_NOTOK(numbytes)) {
				ERROR("%s %s", 
					"ACK_FRM_SERVER msg cudn't be send to client. errno: ", 
					strerror(errno));
				return ERR_CODE;
			} else {
				PRINT("%s %s for group_id: %d", 
						"ACK_FRM_SERVER msg sent to client: ", 
						inet_ntoa(addr->sin_addr), recv_msg->group_id);
				server_state = SERVER_ACK_SENT;
				free_msg(recv_msg);
			}
			break;

		case SERVER_ACK_SENT:
			break;
		
		case SERVER_EXIT_WAIT:
			break;

		case SERVER_EXIT_RECV:
			break;

		default:
			break;
	}
	return rc;
}

void disp_client_help_msg(void) {

 	fprintf(stdout, "   usage: server [-h] [-d] [-a servr_addr] [-p portnum]\n");
	fprintf(stdout, "\toption h:  help on usage\n");
	fprintf(stdout, "\toption d:  enable debug messages\n");
	fprintf(stdout, "\toption p:  to override default port number\n");
	fprintf(stdout, "\toption a:  specify server addr to connect\n");
	fprintf(stdout, "\toption g:  specify group to which client wants to belong\n");
}

void disp_server_help_msg(void) {

	fprintf(stdout, "   usage: server [-h] [-d] [-p portnum]\n");
	fprintf(stdout, "\toption h:  help on usage\n");
	fprintf(stdout, "\toption d:  enable debug messages\n");
	fprintf(stdout, "\toption p:  to override default port number\n");
	fprintf(stdout, "\toption b:  choose subnet where to braodcast\n");
}
