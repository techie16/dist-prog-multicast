/*
 * Client_server.h - function and #defines to be used 
 * for client server interaction
 * 10/30/2014, Siddharth S
 */ 

#ifndef COMMON_HDR
#define COMMON_HDR 1

#include <stdarg.h>
#define SERVERPORT 3690
#define ERR_CODE (-1)
#define RC_NOTOK(rc) (rc == ERR_CODE)
#define CHK_ALLOC(msg) if(!msg) { return ERR_CODE; }
#define FALSE 0
#define TRUE  1
#define MAX_MSG_STR_LEN 20
#define SIZEOFINT sizeof(int)
#define MAX_BROADCAST_PKT_LEN (SIZEOFINT*4)
#define ADDR_FAMILY AF_INET
#define MAX_CLIENTS 20
#define MAX_FILE_LEN 1024
#define ADDR_STR_LEN 20
#define PRINT(format, ...) print_out(format, __VA_ARGS__) 
#define DEBUG(format, ...) print_debug(format, __VA_ARGS__) 
#define ERROR(format, ...) print_error(format, __VA_ARGS__)
#define ALERT(format, ...) print_error(format, __VA_ARGS__)
#define EXIT exit(0)
#define FUNC __FUNCTION__
#define HBT_TIME 180

extern int debug_on;

void print_out(const char* format, ...);
void print_debug(const char* format, ...);
void print_error(const char* format, ...);
void print_alert(const char* format, ...);

int get_server_port_frm_file(void);
int get_server_info_frm_file (char *addr, int *port_num);

typedef enum {
    MSG_RES,
    SERVER_UP,
    REGISTER_CLIENT,
    ACK_FRM_SERVER,
	HEARTBEAT,
    CLIENT_DOWN,
    MAX_MSG_TYPE
} msg_type_en;

typedef enum {
	CLIENT_RES,
	CLIENT_INIT,
	CLIENT_REG_SENT,
	CLIENT_ACK_WAIT,
	CLIENT_ACK_OK,
	CLIENT_EXIT,
	CLIENT_MAX_STATE
} client_state_en;

typedef enum {
	SERVER_RES,
	SERVER_INIT,
	SERVER_BROADCAST_SENT,
	SERVER_REG_WAIT,
	SERVER_REG_RECV,
	SERVER_ACK_SENT,
	SERVER_HBEAT_WAIT,
	SERVER_EXIT_RECV,
	SERVER_CLIENT_DOWN,
	SERVER_MAX_STATE
} server_state_en;

typedef struct client_db_ {
	int port;
	struct in_addr client_address;
	int group;
	struct client_db *next;
} client_db_st;

typedef struct new_client_db_ {
	int socket_id;
	int group_id;
	struct sockaddr_in client_addr;
	bool is_active;
	bool is_exec;
} new_client_db_st;

typedef struct msg_st_ {
	msg_type_en type; 
	int len;
	int group_id;
	int hash_id;
	char data[0];
} msg_st;

typedef struct thread_arg_st_ {
	int socket_id;
	server_state_en state_arg;
	struct sockaddr_in addr;
} thread_arg_st;

void * 
process_via_thread (void *arg);

void * 
process_data_thread (void *arg);

int get_msg_data_len (int socket_id);

bool get_send_flag_for_state (bool flag);

/* Function to convert mst tyoe to string */
char * get_msg_type_str (msg_type_en msg_type);

/* Function to perform cleanup, when a program terminates */
void cleanExit(int);
void cleanExit_client(int signum);

/* Initialize any structure address */
void initialize_addr_struct (struct sockaddr_in *addr, int port_num);

/* add an entry to client_db */
void add_entry_to_db (struct in_addr client_addr, int port_num, int grp);

/* count total num of entries present in client_db */
int count_total (void);

int action_on_msg(int socket_fd, msg_st *recv_msg, struct sockaddr_in *addr);

void free_msg (msg_st *msg);

void disp_client_help_msg(void);
void disp_server_help_msg(void);

extern client_state_en client_state;
extern server_state_en server_state;
extern struct sockaddr_in server_addr_copy;
extern int comm_port_copy;
extern int comm_sock_copy;
extern int group_id_copy;
extern int hash_id_copy;

char * get_client_state_str(client_state_en client_state_arg);

char * get_server_state_str(server_state_en server_state_arg);

int action_on_server_state(int socket_fd, 
						   server_state_en server_state_arg,
                  		   struct sockaddr_in *addr);

int action_on_client_state(int socket_fd, 
						   client_state_en client_state_arg,	 
		                   struct sockaddr_in *addr);

void set_signal_handler(void (*f)(int));

#endif
