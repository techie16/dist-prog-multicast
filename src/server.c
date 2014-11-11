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
#include "client_server.h"

int main(int argc, char **argv) 
{

    int cport_fd, dport_fd, cport_broad_fd, client_socket[MAX_CLIENT], dummy_sock;
    int cport = 0, broadport = 0, dport = 0;
	FILE *file_fd;
    struct sockaddr_in serv_addr;
    struct sockaddr_in broadcast_addr; // connector's address information
    struct sockaddr_in client_addr[MAX_CLIENT], dummy_addr;    // connected clients address information
    struct hostent *host = NULL;
    int broadcast = 1, reuse = 1;
    bool use_gbroadcast = FALSE;
    int rc = 0;
    int numbytes = 0, addr_len;
    char addr_str[35];
    char msg_str[MAX_MSG_STR_LEN];
    struct in_addr addr_binary;
	struct client_db_st *entry = NULL;
	char file_str[MAX_FILE_LEN];

    /* set prog behaviour on recieving below Signals */
    signal(SIGTERM, cleanExit);
    signal(SIGINT, cleanExit);

	file_fd = fopen("conf.txt", "a+");
	if (!file_fd) {
		fprintf(stderr, "fopen failed\n");
	}
	fgets(file_str, MAX_FILE_LEN, file_fd);
	printf("content of FILE: %s\n\n", file_str);

    /* check if Port has been passed as argument */
    if (argc != 3)
    {
        fprintf(stderr, "%s: usage %s <Port> <Broadcast IP_Address> \n\n\n", argv[0], argv[0]);
        fprintf(stderr, "%s: None arguments specified, using def port: %d and global broadcast addr\n", argv[0], SERVERPORT);
        cport = SERVERPORT;
        broadport = cport + 1;
        dport = cport + 2;
        
        
        /* since addr is nt provided correctly, use 
         * use global broadcast address for prog to operate 
         */
        use_gbroadcast = TRUE;

    } else {

        /* obtain port number */
        if (sscanf(argv[1], "%d", &broadport) <= 0) {
            fprintf(stderr, "%s: error parsing Port, using default port: %d\n", argv[0], SERVERPORT);
            broadport = SERVERPORT;
        }

        /* just increment cport by 1 for broadcast port and data port */
        cport = broadport + 1;
        dport = broadport + 2;

        if ((host = gethostbyname(argv[2])) == NULL) {
            fprintf(stderr, "%s: gethostbyname failed. prog will be using global broadcast addr \n", argv[0]);
            use_gbroadcast = TRUE;
        }
    }

    /* check if addr argument was not processed successfully, instead use localhost */ 
    if (use_gbroadcast) {

        inet_pton(ADDR_FAMILY, "255.255.255.255", &addr_binary);
        if ((host = gethostbyaddr(&addr_binary, sizeof(addr_binary), ADDR_FAMILY)) == NULL) {
            fprintf(stderr, "%s: gethostbyname failed for localhost. errno: %d\n", argv[0], errno);
            exit(1);
        }
    }


    /* Control msgs will be recvd from clients via this UDP based socket */
    cport_fd = socket(ADDR_FAMILY, SOCK_DGRAM, 0);
    initialize_struct_addr(&serv_addr, cport);

    /* associate the socket with the port number */ 
    rc = bind(cport_fd, (struct sockaddr*)&serv_addr, sizeof(serv_addr));
    if (RC_NOTOK(rc)) {
        fprintf(stderr, "bind to cport failed.. errno: %d\n", errno);
    }

    /* Braodcast message will be sent to clients using below UDP socket */
    cport_broad_fd = socket(ADDR_FAMILY, SOCK_DGRAM, 0);

    /* this call is what allows broadcast packets to be sent: */
    if (setsockopt(cport_broad_fd, SOL_SOCKET, SO_BROADCAST, 
                  &broadcast, sizeof broadcast) == -1) {
        fprintf(stderr, "error setting BROADCAST option for UDP socket. errno: %d\n", errno);
    }

    /* now set broadcast address to strcture */
    broadcast_addr.sin_family = ADDR_FAMILY;
    broadcast_addr.sin_addr   = *((struct in_addr *)host->h_addr);
    broadcast_addr.sin_port   = htons(broadport);

    #if 0
    /* associate the socket with the port number */ 
    rc = bind(cport_broad_fd, (struct sockaddr*)&broadcast_addr, sizeof(broadcast_addr));
    if (RC_NOTOK(rc)) {
        fprintf(stderr, "bind to broadcast port failed.. errno: %d\n", errno);
    }
    #endif

    fprintf(stdout, "Server is UP and ready, sending broadcast to all\n");
    /* Server is UP, now send SERVER_UP to all connected clients */
    get_msg_type_str(SERVER_UP, msg_str);
    if ((numbytes = sendto(cport_broad_fd, msg_str, strlen(msg_str), 0,
                    (struct sockaddr *)&broadcast_addr, sizeof(broadcast_addr))) == -1) {
        fprintf(stderr, "error sending BROADCAST message. errno: %d\n", errno);
    }
    
    inet_ntop(ADDR_FAMILY, &(broadcast_addr.sin_addr), addr_str, 35);
    printf("sent %d bytes to %s\n", numbytes, addr_str);

    close(cport_broad_fd);

    while (1) {

		memset(&dummy_addr, 0, sizeof(dummy_addr));
        fprintf(stdout, "Waiting for any control msg from cliets\n");
		addr_len = sizeof(dummy_addr);
        if ((numbytes = recvfrom(cport_fd, msg_str, MAX_MSG_STR_LEN, 0,
                    (struct sockaddr *)&dummy_addr, &addr_len)) == -1) {
            fprintf(stderr, "error while trying to recv control msg from clients. errno: %s (%d)\n", 
																				strerror(errno), errno);
            exit(1);
        } else {

	        inet_ntop(ADDR_FAMILY, &(dummy_addr.sin_addr), addr_str, 35);
    	    fprintf(stdout, "msg RECVD from %s, msg: %s\n", addr_str, msg_str);

			/* As we have recvd client registration req, lets add to client_db */
			add_entry_to_db(dummy_addr.sin_addr, dummy_addr.sin_port, 1);
			fprintf(stdout, "Total entries in client db: %d\n", count_total());
		}

		/* Now Send back the registration Ack to respective client */
		dummy_sock = socket(ADDR_FAMILY, SOCK_DGRAM, 0);
		printf("Sidd: Client port is %d\n", dummy_addr.sin_port);
		addr_len = sizeof(dummy_addr);
		get_msg_type_str(ACK_FRM_SERVER, msg_str);
		if ((numbytes = sendto(dummy_sock, msg_str, MAX_MSG_STR_LEN, 0,
                    (struct sockaddr *)&dummy_addr, addr_len)) == -1) {
			fprintf(stderr, "error while trying to send Registartion ACK to client. errno: %s (%d)\n", 
																			strerror(errno), errno);
		}
		fprintf(stdout, "Registration ACK sent to client: %s\n", inet_ntoa(dummy_addr.sin_addr));
    }

	/* Though it will never reache here, but just for completion sake */
    return 0;
}
