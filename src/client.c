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
#include "client_server.h"

int main(int argc, char *argv[])
{
	int broadcast_socket;
    int broad_port = 0, comm_port = 0;
    int reuse = 1, comm_socket = 0;
	int max_sd, activity;
    struct sockaddr_in my_addr; // my address information
    struct sockaddr_in server_addr; // connectors address information
    int addr_len, numbytes;
    char buf[MAX_MSG_STR_LEN];
    fd_set readfds, writefds; //set of socket descriptors
	bool reg_done = FALSE; //is_server_up = FALSE;

    /* set prog behaviour on recieving below Signals */
    signal(SIGTERM, cleanExit);
    signal(SIGINT, cleanExit);

    if (argc != 3) {
        fprintf(stderr, "%s: usage %s <Broadcast port> <Server IP> \n", argv[0], argv[0]);
        exit(1);
    }

    /* fetch broadcast port from cmd line args */
    if (sscanf(argv[1], "%d", &broad_port) <= 0) {
        fprintf(stderr, "%s: error parsing Port. exiting\n", argv[0]);
        exit(1);
    }
    comm_port = broad_port + 1;
   
	if ((comm_socket = socket(AF_INET, SOCK_DGRAM, 0)) == -1) {
		fprintf(stderr, "communication socket creation failed. errno: %s\n", 
															strerror(errno));
		exit(1);
	}

	#if 0
	if ((tcp_comm_socket = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
		fprintf(stderr, "TCP communication socket creation failed. errno: %s\n",
															strerror(errno));
		exit(1);
	}
	#endif

	/* fetch server IP address from argv */
	server_addr.sin_family = ADDR_FAMILY; 
	server_addr.sin_addr.s_addr = inet_addr(argv[2]);
	server_addr.sin_port = htons(comm_port);
	memset(&server_addr.sin_zero, 0, 8); //zero the rest of the struct

	#if 0
	setsockopt(tcp_comm_socket, SOL_SOCKET, SO_REUSEADDR,
               &reuse, sizeof(reuse));

	//check if server is up and available
	if(connect(tcp_comm_socket, (struct sockaddr*)&server_addr, sizeof(struct sockaddr)) == -1) {
		fprintf(stdout, "SERVER is not UP, moving client to listening state. err: %s\n", strerror(errno));
	} else {
		is_server_up = TRUE;
	}

	/* purpose is done, close the tcp socket for now */
	close(tcp_comm_socket);
	#endif

    if ((broadcast_socket = socket(AF_INET, SOCK_DGRAM, 0)) == -1) {
        fprintf(stderr, "socket creation failed. errno: %d\n", errno);
        exit(1);
    }

    my_addr.sin_family = ADDR_FAMILY; // host byte order
    my_addr.sin_port = htons(broad_port); 
    my_addr.sin_addr.s_addr = htonl(INADDR_ANY); // automatically fill with my IP
    memset(&(my_addr.sin_zero), 0, 8); // zero the rest of the struct

	/* 
	 * get rid of "address already in use" error message by 
     * setting the socket option to REUSE 
	 */
    setsockopt(broadcast_socket, SOL_SOCKET, SO_REUSEADDR,
               &reuse, sizeof(reuse));
	setsockopt(comm_socket, SOL_SOCKET, SO_REUSEADDR,
               &reuse, sizeof(reuse));

    if (bind(broadcast_socket, (struct sockaddr *)&my_addr,
            sizeof(struct sockaddr)) == -1) {
        fprintf(stderr, "error while binding to broadcast socket. errno: %d\n", errno);
        exit(1);
    }

	#if 0
    if (bind(comm_socket, (struct sockaddr *)&server_addr,
            sizeof(struct sockaddr)) == -1) {
        fprintf(stderr, "error while binding to communication socket. errno: %s\n", 	
																	  strerror(errno));
        exit(1);
    }
	#endif

    addr_len = sizeof(struct sockaddr);
    printf("listening for any broadcast msg from server on port: %d\n", broad_port);

	while (TRUE) {

        //clear the socket set
        FD_ZERO(&readfds);
        FD_ZERO(&writefds);

        //add broadcast_socket and comm_socket to FD set
        FD_SET(broadcast_socket, &readfds);
        FD_SET(comm_socket, &readfds);
        FD_SET(comm_socket, &writefds);
        max_sd = broadcast_socket;

		/* wait indefinitely, hence no timeout is specified */
		activity = select(max_sd+1 , &readfds , &writefds , NULL , NULL);
		if ((activity < 0) && (errno!=EINTR)) {
            printf("select failed. errno: %d", errno);
        }

        //If something happened on the braodcast socket , then its an SERVER_UP req
        if (FD_ISSET(broadcast_socket, &readfds)) {
			printf("Sidd: brodcast FD set\n");
			addr_len = sizeof(server_addr);
		    if ((numbytes = recvfrom(broadcast_socket, buf, MAX_MSG_STR_LEN-1, 0,
    		                (struct sockaddr *)&server_addr, &addr_len)) == -1) {
				fprintf(stderr, "braodcast msg recv failure. errno: %s\n", strerror(errno));
        		exit(1);
	    	}

		    buf[numbytes] = '\0';
	    	//inet_ntop(AF_INET, &server_addr.sin_addr, addr_str, 35);
	    	fprintf(stdout, "Recieved %s from: %s\n", buf,
											inet_ntoa(server_addr.sin_addr));

			/* Now send Registartion request to server */
			get_msg_type_str(REGISTER_CLIENT, buf);
			/* reset server Port */
			server_addr.sin_port = htons(comm_port);
			addr_len = sizeof(server_addr);
			if ((numbytes = sendto(comm_socket, buf, strlen(buf), 0,
						(struct sockaddr *)&server_addr, addr_len)) == -1) {
				fprintf(stderr, "Registration request failed. errno: %s", strerror(errno));
				exit(1);
			}
			
			fprintf(stdout, "Registration request sent to Server\n");
		} else if (FD_ISSET(comm_socket, &readfds)) {
			/* 
        	 * If something happened on the comm_socket, 
			 * then its an Registration Ack from server
			 */
			printf("Sidd: Comm socket FD is set \n");
			memset(&server_addr, 0, sizeof(server_addr));
			addr_len = sizeof(server_addr);
		    if ((numbytes = recvfrom(comm_socket, buf, MAX_MSG_STR_LEN-1, 0,
			                (struct sockaddr *)&server_addr, &addr_len)) == -1) {
				fprintf(stderr, "Registration Ack recv failure. errno: %s\n", strerror(errno));
    			exit(1);
    		}

	    	buf[numbytes] = '\0';
			reg_done = TRUE;
	    	//inet_ntop(AF_INET, &server_addr.sin_addr, addr_str, 35);
	    	fprintf(stdout, "Recieved %s through: %s\n", buf, 
											inet_ntoa(server_addr.sin_addr));
		} else if (FD_ISSET(comm_socket, &writefds)) {

			/* Now send Registartion request to server 
			 * No need to send registration req if its already done
			 */
			if (!reg_done) {
				get_msg_type_str(REGISTER_CLIENT, buf);
				addr_len = sizeof(server_addr);
				if ((numbytes = sendto(comm_socket, buf, strlen(buf), 0,
							(struct sockaddr *)&server_addr, addr_len)) == -1) {
					fprintf(stderr, "Explicit Registration request failed. errno: %s", strerror(errno));
					exit(1);
				}
				fprintf(stdout, "Explicit Registration request sent to Server\n");
			}
			sleep(10);
		}
	}

    /* recieving is done, close the broadcast listener socket */
	/* This is never going to happen, until someone kills the program */
	
    close(broadcast_socket);
	close(comm_socket);
    return 0;
}
