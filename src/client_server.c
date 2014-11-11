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
#include "client_server.h"

client_db_st *head = NULL, *current = NULL;

void cleanExit(){
    printf("Program exiting\n");
    exit(0);
}

void get_msg_type_str (msg_type_en msg_type, char *str) 
{

    if (!str) {
        return;
    }
    memset(str, 0, MAX_MSG_STR_LEN);

    switch(msg_type) {

        case MSG_RES:
            strncpy(str, "MSG_RES", strlen("MSG_RES"));
            break;
        case SERVER_UP:
            strncpy(str, "SERVER_UP", strlen("SERVER_UP"));
            break;
        case REGISTER_CLIENT:
            strncpy(str, "REGISTER_CLIENT", strlen("REGISTER_CLIENT"));
            break;
        case ACK_FRM_SERVER:
            strncpy(str, "ACK_FRM_SERVER", strlen("ACK_FRM_SERVER"));    
            break;
        case CLIENT_DOWN:
            strncpy(str, "CLIENT_DOWN", strlen("CLIENT_DOWN"));  
            break;
        default:
            fprintf(stderr, "unknown msg type\n");
            break;
    }

    str[MAX_MSG_STR_LEN] = '\0';
}

void initialize_struct_addr (struct sockaddr_in *addr, int port_num) {

    addr->sin_family = ADDR_FAMILY;
    addr->sin_addr.s_addr = htonl(INADDR_ANY);
    addr->sin_port = htons(port_num);
}

bool search_entry (struct in_addr client_addr, int port_num) {

}

void add_entry_to_db (struct in_addr client_addr, int port_num, int grp) {

	client_db_st *entry = NULL;
	entry = (client_db_st *) malloc(sizeof(client_db_st));

	printf("Sidd: entry to add.. size to alloc: %d\n", sizeof(client_db_st));	
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

int count_total () {
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
