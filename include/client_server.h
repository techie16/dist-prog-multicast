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
#define FILE_NAME_LEN 20
#define PRINT(format, ...) print_out(format, __VA_ARGS__) 
#define DEBUG(format, ...) print_debug(format, __VA_ARGS__) 
#define ERROR(format, ...) print_error(format, __VA_ARGS__)
#define ALERT(format, ...) print_alert(format, __VA_ARGS__)
#define EXIT exit(0)
#define FUNC __FUNCTION__
#define HBT_TIME 60 //(60+rand()%20)
#define HBT_EXPTIME (HBT_TIME*2)
#define JOB_FIN_TIME 60
#define HBT_CHK_ITR 20
#define IGNORE_GROUP 1
#define JOB_WAIT_TIME 120
#define SECS_IN_MSEC 1000
#define MAX_ASC_CHLEN 10
#define DIE_ON_PIPE(errno) if (errno == 32) {exit(0);}

typedef enum {
	JOB_RES,
	JOB_PRIME,
	JOB_WC,
	JOB_SERIES,
	JOB_FIND_MAX,
	JOB_MAX_ID
} job_id_en;

typedef enum {
    MSG_RES,
    SERVER_UP,
    REGISTER_CLIENT,
    ACK_FRM_SERVER,
	HEARTBEAT,
	JOB_REQ,
	JOB_RESP,
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
	int socket_id;
	short group_id;
	short hash_id;
	short family;
	unsigned short port_num;
	struct in_addr addr;
	bool is_active;
	bool is_exec;
	bool server_ack;
	bool is_participant;
	time_t hbeat_time;
	char file_outp[FILE_NAME_LEN];
} client_db_st;

typedef struct job_st_ {
	short job_id;
	int start_range;
	int end_range;
	char outpt_file[FILE_NAME_LEN];
	char inpt_file[FILE_NAME_LEN];
} job_st;

typedef struct msg_st_ {
	msg_type_en type; 
	int len;
	short group_id;
	short hash_id;
	job_st job_data[0];
} msg_st;

typedef struct thread_arg_st_ {
	int socket_id;
	server_state_en state_arg;
	struct sockaddr_in addr;
} thread_arg_st;

typedef struct client_recv_ {
	client_db_st *entry;
} client_recv_st;

typedef struct grp_data_ {
	short hash_id;
	struct grp_data_ *next;
} grp_data_st;


extern int debug_on;
extern client_db_st *client_entry[MAX_CLIENTS];
extern grp_data_st  *grp_data[MAX_CLIENTS];
extern client_state_en client_state;
extern server_state_en server_state;
extern struct sockaddr_in server_addr_copy;
extern int comm_port_copy;
extern int comm_sock_copy;
extern int group_id_copy;
extern int hash_id_copy;
extern int total_fd;
extern bool hbeat_chk_start;

void print_out(const char* format, ...);
void print_debug(const char* format, ...);
void print_error(const char* format, ...);
void print_alert(const char* format, ...);

int get_server_port_frm_file(void);
int get_server_info_frm_file (char *addr, int *port_num);

int server_action_on_msg(int socket_fd, int hash_id,
						 msg_st *msg);

void display_job_info (int *job_id);

char * 
sigtostr(int signum);

void * 
process_via_thread (void *arg);

void * 
process_data_thread (void *arg);

void * send_thread (void *arg);

void * recv_thread (void *arg);

int get_msg_data_len (int socket_id);

int get_msg_data_len_non_wait (int socket_id);

bool get_send_flag_for_state (bool flag);

int display_grp_info (short grp_id);

/* Function to convert mst tyoe to string */
char * get_msg_type_str (msg_type_en msg_type);

/* Function to perform cleanup, when a program terminates */
void cleanExit(int);
void cleanExit_client(int signum);

int send_job_to_grp(job_id_en id);

int add_hash_id_to_grp (short hash_id, short grp_id, grp_data_st **ptr);

int send_pkt_to_client (int socket_id, msg_type_en msg_type, 
						int index, job_st *data);

int action_on_msg(int socket_fd, msg_st *recv_msg, struct sockaddr_in *addr);
int count_total_grp (void);

void free_msg (msg_st *msg);

void disp_client_help_msg(void);
void disp_server_help_msg(void);

char * get_client_state_str(client_state_en client_state_arg);

char * get_server_state_str(server_state_en server_state_arg);

bool is_client_entry_exists(struct sockaddr_in *addr, 
							int *index);

int action_on_server_state(int socket_fd, 
						   server_state_en server_state_arg,
                  		   struct sockaddr_in *addr);

int action_on_client_state(int socket_fd, 
						   client_state_en client_state_arg,	 
		                   struct sockaddr_in *addr);

void add_client_db_info(int index, int socket_fd,
						short grp_id, struct sockaddr_in *client_addr);

void upd_client_db_info(int index, int new_socket_fd, int port_num);

void set_signal_handler(void (*f)(int));

void * verify_client_hbeat (void *arg);

int compute_job(job_st *data, char *file);

inline bool is_prime (int num);

int del_file_if_exist(char *file);

void display_job_output(void);

int count_grp_total (short grp_id);

#endif
