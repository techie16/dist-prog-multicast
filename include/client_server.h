/*
 * Client_server.h - function and #defines to be used 
 * for client server interaction
 * 10/30/2014, Siddharth S
 */ 

#define SERVERPORT 3690
#define RC_NOTOK(rc) (rc == -1)
#define FALSE 0
#define TRUE  1
#define MAX_MSG_STR_LEN 20
#define ADDR_FAMILY AF_INET
#define MAX_CLIENT 20
#define MAX_FILE_LEN 1024

typedef enum {
    MSG_RES,
    SERVER_UP,
    REGISTER_CLIENT,
    ACK_FRM_SERVER,
    CLIENT_DOWN,
    MAX_MSG_TYPE
} msg_type_en;

typedef struct client_db {
	int port;
	struct in_addr client_address;
	int group;
	struct client_db *next;
} client_db_st;

/* Function to convert mst tyoe to string */
void get_msg_type_str (msg_type_en msg_type, char *str);

/* Function to perform cleanup, when a program terminates */
void cleanExit(int);

/* Initialize any structure address */
void initialize_addr_struct (struct sockaddr_in *addr, int port_num);

/* add an entry to client_db */
void add_entry_to_db (struct in_addr client_addr, int port_num, int grp);

/* count total num of entries present in client_db */
int count_total (void);
