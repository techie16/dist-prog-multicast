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
#define RC_ISOK(rc)  (rc != ERR_CODE)
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
#define MAX_WORD_LEN 20
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
#define IGNORE_GROUP 0
#define JOB_WAIT_TIME 120
#define SECS_IN_MSEC 1000
#define MAX_ASC_CHLEN 10
#define DIE_ON_PIPE(errno) if (errno == 32) {exit(0);}

#define COMPUTE_MCAST_GRP   "225.0.0.1"
#define ANALYSIS_MCAST_GRP  "226.0.0.1"
#define COMP_MCAST_PORT	 	9090
#define ANALYSIS_MCAST_PORT 9091

#define DEBUG_ON 	 0x00000001
#define MULTICAST_ON 0x00000002

typedef enum {
	JOB_COMPUTE  =  1,
	JOB_ANALYSIS = 2,
	JOB_SERVER_LIST = 3,
	JOB_MAX
} grp_id_en;

typedef enum {
	JOB_RES,
	JOB_PRIME,
	JOB_WC,
	JOB_FIND_MAX,
	JOB_SERIES,
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
	JOB_TERM,
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

typedef struct client_db_ {
	int socket_id; 			 //Socket_fd to be used for send to clnt
	short group_id;			 // group to which client belongs
	short hash_id;			 // unique client id
	short family;            // address family
	unsigned short port_num; // port number to which client is cnnected
	struct in_addr addr;	 // addr struct of client
	bool is_active;			 // flag TRUE if client is active
	bool is_exec;			 // flag TRUE if clnt is exec any job
	bool server_ack;		 // flag FALSE if serevr is yet to send an ACK 
	bool is_participant;	 // flag TRUE if clnt is participating in any job
	time_t hbeat_time;		 // time stamp updated each time a HBEAT is recvd
	char file_outp[FILE_NAME_LEN]; 
} client_db_st;

typedef struct job_st_ {
	short job_id;
	int start_range;
	int end_range;
	char inpt_file[FILE_NAME_LEN];
	char outpt_file[FILE_NAME_LEN];
} job_st;

typedef struct msg_st_ {
	msg_type_en type; 
	int len;
	short group_id;
	short hash_id;
	job_st job_data[0];
} msg_st;

typedef struct clnt_thread_arg_st_ {
	int client_id;
	short group_id;
	short job_id;
	int start_range;
	int end_range;
	char outpt_file[FILE_NAME_LEN];
	char inpt_file[FILE_NAME_LEN];
} clnt_thread_arg_st;

typedef struct client_recv_ {
	client_db_st *entry;
} client_recv_st;

typedef struct grp_data_ {
	short hash_id;
	struct grp_data_ *next;
} grp_data_st;


extern int op_mode;
extern bool is_multicast_supp;
extern struct sockaddr_in clnt_mcast_addr;
extern int clnt_mcast_fd;
extern struct ip_mreq mreq_global;
extern client_db_st *client_entry[MAX_CLIENTS];
extern grp_data_st  *grp_data[MAX_CLIENTS];
extern client_state_en client_state;
extern int total_fd;
extern bool hbeat_chk_start;
extern struct sockaddr_in server_addr_copy;
extern int comm_sock_copy;
extern int req_grp_id;
extern FILE *log_fp;
extern bool clnt_debug_on;
extern int srvr_master_sock;
extern bool job_in_progress;
extern int my_client_id;
extern int sub_job_count;
extern int recvd_job_id;
extern bool exec_job_start;

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

void * send_thread (void *arg);

void * recv_thread (void *arg);

void * exec_job_thread (void *arg);

int get_msg_data_len (int socket_id);

int get_msg_data_len_non_wait (int socket_id);

bool get_send_flag_for_state (bool flag);

void display_grp_info (short grp_id);

/* Function to convert mst tyoe to string */
char * get_msg_type_str (msg_type_en msg_type);

/* Function to perform cleanup, when a program terminates */
void cleanExit(int);
void cleanExit_client(int signum);

int send_job_to_grp(job_id_en id);

int add_hash_id_to_grp (short hash_id, short grp_id, grp_data_st **ptr);

int send_pkt_to_client (int socket_id, msg_type_en msg_type,
                        struct sockaddr_in *client_addr,
                        int index, job_st *data);

int action_on_msg(int socket_fd, msg_st *recv_msg, struct sockaddr_in *addr);
int count_total_grp (void);

void free_msg (msg_st *msg);

void disp_client_help_msg(void);
void disp_server_help_msg(void);

char * get_client_state_str(client_state_en client_state_arg);

bool is_client_entry_exists(struct sockaddr_in *addr, 
							int *index);

int action_on_client_state(int socket_fd, 
						   client_state_en client_state_arg,	 
		                   struct sockaddr_in *addr);

void add_client_db_info(int index, int socket_fd,
						short grp_id, struct sockaddr_in *client_addr);

void upd_client_db_info(int index, int new_socket_fd, int port_num);

void set_signal_handler(void (*f)(int));

void * verify_client_hbeat (void *arg);

int compute_job(clnt_thread_arg_st *data);

inline bool is_prime (int num);

int del_file_if_exist(char *file);

void display_job_output(void);

int count_grp_total (short grp_id);

bool is_file_exist(char *file);

bool is_multicast (void);
bool is_debug_mode (void);

int join_mcast_group (grp_id_en group_id);
int server_join_mcast_group (grp_id_en group_id, int sock_id);

int leave_mcast_group (void);
int server_leave_mcast_group (grp_id_en group_id, int sock_id);

int get_msg_data_len_udp (int socket_id);
int send_job_to_grp_mcast (job_id_en job_id);
#endif
