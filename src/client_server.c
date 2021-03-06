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
#include <unistd.h>
#include <sys/poll.h>
#include "client_server.h"

/* Global variables */
int op_mode = 0;
bool is_multicast_supp = FALSE;
struct sockaddr_in clnt_mcast_addr;
int  clnt_mcast_fd = 0;
struct ip_mreq mreq_global;
bool hbeat_chk_start = FALSE;
bool disp_cons_job = TRUE;
client_state_en client_state = CLIENT_RES;
client_db_st *client_entry[MAX_CLIENTS];
grp_data_st  *grp_data[MAX_CLIENTS];
int total_fd;
struct pollfd readfds[MAX_CLIENTS];
time_t job_sent_ts = 0;
struct sockaddr_in server_addr_copy;
int comm_sock_copy = 0;
int req_grp_id = 0;
FILE *log_fp = NULL;
bool clnt_debug_on = FALSE;
int srvr_master_sock = 0;
bool job_in_progress = FALSE;
int my_client_id = 0;
int sub_job_count = 0;
int recvd_job_id = 0;
bool exec_job_start = FALSE;

void set_signal_handler(void (*f)(int)) 
{
	/* Ignore broken Pipe error */
	signal(SIGPIPE, SIG_IGN);

	/* Handle rest via function */
    signal(SIGTERM, *f);
    signal(SIGINT, *f);
    signal(SIGABRT, *f);
    signal(SIGFPE, *f);
    signal(SIGSEGV, *f);
}

void upd_client_db_info(int index, int new_socket_fd, 
						int port_num)
{

	PRINT("client_id: %d (%s) of group: %d %s", 
		  index, inet_ntoa(client_entry[index]->addr),
		  client_entry[index]->group_id, "requested re-registration");

	client_entry[index]->socket_id 	= new_socket_fd;
	client_entry[index]->port_num 	= port_num;
	client_entry[index]->is_active  = TRUE;
	client_entry[index]->server_ack = FALSE;
	client_entry[index]->is_participant = FALSE;

	/* set the socket_fd to readfd group, for poll() */	
	readfds[index].fd = new_socket_fd;
	readfds[index].events = POLLIN;
}

void add_client_db_info(int index, int socket_fd,
						short grp_id, struct sockaddr_in *client_addr)
{
	int i = index;
	client_entry[i] = (client_db_st *) calloc(1, sizeof(client_db_st));

	if (!client_entry[i]) {
		ERROR("%s %d file: %s", "calloc failure at line:", __LINE__, __FILE__);
		return;
	}

	client_entry[i]->socket_id = socket_fd;
	if (IGNORE_GROUP) {
		client_entry[i]->group_id = (i/4); 
	} else {
		client_entry[i]->group_id = grp_id;
	}

	client_entry[i]->hash_id = index; 
	client_entry[i]->family = client_addr->sin_family;
	client_entry[i]->port_num = client_addr->sin_port;
	client_entry[i]->addr = client_addr->sin_addr;
	client_entry[i]->is_active  = TRUE;
	client_entry[i]->is_exec    = FALSE;
	client_entry[i]->server_ack = FALSE;

	/* set this socket_fd to readfd group, for use in poll() */	
	readfds[i].fd = client_entry[i]->socket_id;
	readfds[i].events = POLLIN;
	
	/* incr the total_fd counter to be used by Poll */
	total_fd++;
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

        case HEARTBEAT:
			return "HEARTBEAT";

		case JOB_REQ:
			return "JOB_REQ";

		case JOB_RESP:
			return "JOB_RESP";

		case JOB_TERM:
			return "JOB_TERM";

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

int add_hash_id_to_grp (short hash_id, short grp_id, 
						grp_data_st **ptr) 
{

	grp_data_st *temp = NULL, *current = NULL;

	temp = (grp_data_st *) calloc(1, sizeof(grp_data_st));
	if (!temp) {
		return -1;
	}
	temp->hash_id = hash_id;
	temp->next = NULL;
	current = *ptr;

	if (!*ptr) {
		*ptr = temp;
		current = *ptr;
	} else {
		while(current->next != NULL) {
			current = current->next;
		}
		current->next = temp;
		current = temp;
	}

	DEBUG("client_id %d added succesfully to grp: %d", hash_id, grp_id);
	return 0;
}

int send_pkt_to_client (int socket_id, msg_type_en msg_type,
						struct sockaddr_in *client_addr, 
						int index, job_st *data)
{
	int msg_data_len = 0;
	msg_st *msg = NULL;
	int rc = 0;
	struct sockaddr_in mult_addr;
	
	if (!data) {
		msg_data_len = 0;
	} else {
		msg_data_len = sizeof(job_st);
	}

	switch (msg_type) {
		
		case ACK_FRM_SERVER:
			msg = (msg_st *)calloc(1, sizeof(msg_st)+msg_data_len);
			if (!msg) {
				ERROR("%s %d file : %s", "alloc for ACK msg failed at line:",
				  			     __LINE__, __FILE__);
				return ERR_CODE;
			}

		 	msg->type = msg_type;
			msg->len = msg_data_len;

			if (is_multicast()) {
				rc = sendto(socket_id, msg, sizeof(msg_st)+msg_data_len, 0,
							(struct sockaddr *) client_addr,
							sizeof(struct sockaddr_in));	
				if (RC_NOTOK(rc)) {
					ERROR("%s %s %s", get_msg_type_str(msg_type),
						"failed to be sent to client. errno: ",
						 strerror(errno));
					free_msg(msg);
					return rc;
				} else {
					DEBUG("%s", "ACK from server sent succesfully");
				}
			} else {	
		
				msg->group_id = client_entry[index]->group_id; 	
				msg->hash_id = client_entry[index]->hash_id;

				rc = send(socket_id, msg, sizeof(msg_st)+msg_data_len, 0);
				if (RC_NOTOK(rc)) {
					ERROR("%s %s %s", get_msg_type_str(msg_type),
						"failed to be sent to client: with hash_id: %d. errno: %s",
						 inet_ntoa(client_entry[index]->addr), msg->hash_id,
						 strerror(errno));
					free_msg(msg);
					return rc;
				} else {
					client_entry[index]->server_ack = TRUE;
					client_entry[index]->hbeat_time = 0;
					free_msg(msg);
				}
			}
			break;

		case JOB_REQ:
			msg = (msg_st *)calloc(1, sizeof(msg_st)+msg_data_len);
			if (!msg) {
				ERROR("%s %d file : %s", "alloc for ACK msg failed at line:",
				  			     __LINE__, __FILE__);
				return ERR_CODE;
			}

		 	msg->type = msg_type;
			msg->len = msg_data_len;
			if (!is_multicast()) {
				msg->group_id = client_entry[index]->group_id; 	
				msg->hash_id = client_entry[index]->hash_id;
			}
			if (!data) {
				ERROR("%s: data part is NULL", FUNC);
				free_msg(msg);
				return ERR_CODE;
			}
			if (msg_data_len && data) {
				msg->job_data->job_id = data->job_id;
				msg->job_data->start_range = data->start_range;
				msg->job_data->end_range   = data->end_range;
				strcpy(msg->job_data->inpt_file, data->inpt_file);
				strcpy(msg->job_data->outpt_file, data->outpt_file);
			}

			if (is_multicast()) {
				/* JOB_REQ will be sent to multicast group */
				memset(&mult_addr, 0, sizeof(struct sockaddr_in));
				if (req_grp_id == JOB_COMPUTE) {
					mult_addr.sin_addr.s_addr = 
							inet_addr(COMPUTE_MCAST_GRP);
					mult_addr.sin_port = htons(COMP_MCAST_PORT);
				} else if (req_grp_id == JOB_ANALYSIS) {
					mult_addr.sin_addr.s_addr =
							inet_addr(ANALYSIS_MCAST_GRP);
					mult_addr.sin_port = htons(ANALYSIS_MCAST_PORT);
				}

				if (req_grp_id != JOB_COMPUTE && req_grp_id != JOB_ANALYSIS) {
					ERROR("unsupported job grp. :%d", req_grp_id);
					return ERR_CODE;
				}

				rc = sendto(socket_id, msg,
							sizeof(msg_st)+msg_data_len, 0,
							(struct sockaddr *)&mult_addr, 
							sizeof(struct sockaddr_in));
				if (RC_NOTOK(rc)) {
					ERROR("Job sub part send to mcast_grp: %d failed..err: %s",
						  req_grp_id, strerror(errno)); 
				} else {
					DEBUG("Job sub part send to mcast_grp: %d success", 
						  req_grp_id);
				}
			} else {
				/* TCP based sending */
				rc = send(socket_id, msg, sizeof(msg_st)+msg_data_len, 0);
				if (RC_NOTOK(rc)) {
					ERROR("%s %s %d. errno.:%s", get_msg_type_str(msg_type),
						"failed to be sent to client with client_id:",
					 	msg->hash_id, strerror(errno));
					free_msg(msg);
					return rc;
				} else {
					DEBUG("Job_id: %d sent to client_id: %d (%s)",
						 data->job_id, msg->hash_id, 
						 inet_ntoa(client_entry[index]->addr));
					free_msg(msg);
				}
			}
			break;

		case JOB_TERM:

			msg = (msg_st *) calloc(1, sizeof(msg_st));
			if (!msg) {
				ERROR("%s line: %d file : %s", "alloc for JOB_TERM msg failed",
				       __LINE__, __FILE__);
				return ERR_CODE;
			}
			
			msg->type = msg_type;
			msg->len  = msg_data_len;
			msg->group_id = client_entry[index]->group_id;
			msg->hash_id = index;

			rc = send(socket_id, msg, sizeof(msg_st), 0);
			if (RC_NOTOK(rc)) {
				ERROR("%s %d %s %s", "JOB_TERM request to client_id: ",
					 index, "couldn't be sent. errno:", strerror(errno));
				free_msg(msg);
				break;
			} else {
				DEBUG("%s %d", "JOB_TERM request sent to client_id:", index);
				free_msg(msg);
			}
			break;

		default:
			DEBUG("%s: No need to send pkt for msg: %s", FUNC, 
				 get_msg_type_str(msg_type));
	}
	
	return rc;
}

void display_job_info (int *job_id) {
	
	int i = 0;
	printf("\n#########################################################\n");	
	while (TRUE) {
		fprintf(stdout, "Job 1: calculate Prime numbers\n");
		fprintf(stdout, "Job 2: calculate word counts in File\n");
		fprintf(stdout, "Job 3: calculate max in set of nos.\n");
		fprintf(stdout, "Job 4: calculate Sum of series\n");
		fprintf(stdout, "Select Job Id to process...");
	
		scanf("%d", job_id);
		printf("You selected: %d\n", *job_id);
		printf("#########################################################\n");	
		if (*job_id < (JOB_RES+1)  && (*job_id >= JOB_MAX_ID)) {
			printf("Input must be from Displayed Job_IDs.. Try again\n");
			continue;
		} else {
			break;
		}
	}
	printf("Below are the available clients to help in the Job\n");
	printf("=========================================================\n");

	if (is_multicast()) {
		printf("%d\n", total_fd);
	} else {
		for (i = 0; i < MAX_CLIENTS; i++) {
			if (grp_data[i] != NULL) {
				display_grp_info(i);
			}
		}
	}
	printf("=========================================================\n");
}

int send_job_to_grp_mcast (job_id_en job_id) {

	int i = 0, count_client = 0, rc = 0;
	int total_grp = 0;
	int total_set = 0;
	int set_per_grp = 0;
	int start_num = 0, end_num = 0;
	int start_sub_num = 0, end_sub_num = 0;
	int factor_inc = 0;
	int socket_id = 0;
	bool send_grp_fail = FALSE;
	job_st *job_det = NULL;
	grp_data_st *temp = NULL;
	char input_file[FILE_NAME_LEN];
	
	job_det = (job_st *) calloc(1, sizeof(job_st));
	if (!job_det) {
		ERROR("calloc failed at Line: file: %s", __LINE__, __FILE__);
		return -1;
	}

	switch (job_id) {

		case JOB_PRIME:
			start_num = 1;
			start_sub_num = 1;
			end_num = 64000;
			total_set = (end_num - start_num + 1)/total_fd;
			
			for (i = 0; i < total_fd; i++) {
				
				start_sub_num = start_sub_num;
				end_sub_num = start_sub_num + total_set + factor_inc - 1;

				/* set job details */
				job_det->job_id = job_id;
				job_det->start_range = start_sub_num;
				job_det->end_range   = end_sub_num;
				memset(job_det->inpt_file, 0, FILE_NAME_LEN);
				memset(job_det->outpt_file, 0, FILE_NAME_LEN);
				
				/* fetch socket_id where the pkt will be sent */	
				rc = send_pkt_to_client(srvr_master_sock, JOB_REQ, 
										NULL, 0, job_det);
				if (RC_NOTOK(rc)) {
					ERROR("Job_id: %d, sub_range: %d to %d cudnt be send",
						  job_id, start_sub_num, end_sub_num);
					start_sub_num = start_sub_num;
					factor_inc = end_sub_num - start_sub_num;
				} else {
					/* Update ranges for new client*/
					start_sub_num = end_sub_num + 1;
					factor_inc = 0;
					sub_job_count++;
					exec_job_start = TRUE;
				}
			}
			break;

		case JOB_WC:
			for (i = 0; i < MAX_CLIENTS; i++) {
				if (grp_data[i] != NULL) {
					temp = grp_data[i];
					while (temp != NULL) {
						/* set job details */
						job_det->job_id = job_id;
						snprintf(input_file, FILE_NAME_LEN, "job_2_%d", 
								 temp->hash_id);

						if(!is_file_exist(input_file)) {
							ERROR("File: %s doesn't exist, skipping..", 
								  input_file);
							temp = temp->next;
							continue;
						}
						memset(job_det->inpt_file, 0, FILE_NAME_LEN);
						strcpy(job_det->inpt_file, input_file);
						
						/* fetch socket_id where the pkt will be sent */
						socket_id = client_entry[temp->hash_id]->socket_id;
						rc = send_pkt_to_client(socket_id,
												JOB_REQ, NULL, temp->hash_id,
												job_det);
						if (RC_NOTOK(rc)) {
							ERROR("%s client_id: %d", 
								  "couldnt send job pkt to", temp->hash_id);
						} else {

		                    client_entry[temp->hash_id]->is_exec = TRUE;
		                    client_entry[temp->hash_id]->is_participant = TRUE;
							client_entry[temp->hash_id]->server_ack = TRUE;

							PRINT("JOB_REQ to calc WordCount in file :%s %s%d", 
							      input_file, "sent to client_id: ", 
								  temp->hash_id);
						}
						
						/* move to next ptr */
						temp = temp->next;
					}	

					/* move to next grp */
				}
			}
			break;

		case JOB_FIND_MAX:
			break;

		case JOB_SERIES:
			break;
		
		default:
			ERROR("%s job_id: %d", "in default: no such JOB found.", job_id);
			break;
	}

	return rc;
}

int send_job_to_grp(job_id_en job_id) {

	int i = 0, count_client = 0, rc = 0;
	int total_grp = 0;
	int total_set = 0;
	int set_per_grp = 0;
	int start_num = 0, end_num = 0;
	int start_sub_num = 0, end_sub_num = 0;
	int factor_inc = 0;
	int socket_id = 0;
	bool send_grp_fail = FALSE;
	job_st *job_det = NULL;
	grp_data_st *temp = NULL;
	char input_file[FILE_NAME_LEN];
	
	total_grp = count_total_grp();

	job_det = (job_st *) calloc(1, sizeof(job_st));
	if (!job_det) {
		ERROR("calloc failed at Line: file: %s", __LINE__, __FILE__);
		return -1;
	}

	switch (job_id) {

		case JOB_PRIME:
			start_num = 1;
			end_num = 32767;
			total_set = (end_num - start_num + 1)/total_grp;
			
			for (i = 0; i < MAX_CLIENTS; i++) {
				if (grp_data[i] != NULL) { 
					
					count_client = count_grp_total(i);
					start_sub_num = start_num;
					end_sub_num = start_num + total_set + factor_inc - 1;
					set_per_grp = (end_sub_num - start_sub_num)/count_client;
					temp = grp_data[i];
					while (temp != NULL) {
						send_grp_fail = TRUE;
						start_sub_num = start_num;
						end_sub_num = start_num + set_per_grp + factor_inc;
						
						/* set job details */
						job_det->job_id = job_id;
						job_det->start_range = start_sub_num;
						job_det->end_range   = end_sub_num;
						memset(job_det->inpt_file, 0, FILE_NAME_LEN);
						memset(job_det->outpt_file, 0, FILE_NAME_LEN);
					
						/* fetch socket_id where the pkt will be sent */	
						socket_id = client_entry[temp->hash_id]->socket_id;
						rc = send_pkt_to_client(socket_id, JOB_REQ, 
												NULL, temp->hash_id, 
												job_det);
						if (RC_NOTOK(rc)) {
							ERROR("%s client_id: %d",
								  "couldnt send job pkt to", temp->hash_id);
							start_sub_num = start_sub_num;
							factor_inc = set_per_grp;
						} else {
		                   /* 
        		            * Mark client is_exec flag to TRUE, move to FALSE,
							* once execution is completed  
                     		*/
		                    client_entry[temp->hash_id]->is_exec = TRUE;
		                    client_entry[temp->hash_id]->is_participant = TRUE;
							client_entry[temp->hash_id]->server_ack = TRUE;

							/* Update ranges for new client*/
							start_num = end_sub_num + 1;
							factor_inc = 0;
							send_grp_fail = FALSE;
						}
				
						/* move to next ptr */
						temp = temp->next;
					}
	
					/* move to next grp */
					/* if last grp sending has failed, we need reset 
				     * numbers to be sent 
					 */
					if (send_grp_fail) {
						start_num = start_sub_num;
						factor_inc = total_set;
					} else {
						start_sub_num = end_sub_num + 1;
						factor_inc = 0;
					}
				}
			}
			break;

		case JOB_WC:
			for (i = 0; i < MAX_CLIENTS; i++) {
				if (grp_data[i] != NULL) {
					temp = grp_data[i];
					while (temp != NULL) {
						/* set job details */
						job_det->job_id = job_id;
						snprintf(input_file, FILE_NAME_LEN, "job_2_%d", 
								 temp->hash_id);

						if(!is_file_exist(input_file)) {
							ERROR("File: %s doesn't exist, skipping..", 
								  input_file);
							temp = temp->next;
							continue;
						}
						memset(job_det->inpt_file, 0, FILE_NAME_LEN);
						strcpy(job_det->inpt_file, input_file);
						
						/* fetch socket_id where the pkt will be sent */
						socket_id = client_entry[temp->hash_id]->socket_id;
						rc = send_pkt_to_client(socket_id,
												JOB_REQ, NULL, temp->hash_id,
												job_det);
						if (RC_NOTOK(rc)) {
							ERROR("%s client_id: %d", 
								  "couldnt send job pkt to", temp->hash_id);
						} else {

		                    client_entry[temp->hash_id]->is_exec = TRUE;
		                    client_entry[temp->hash_id]->is_participant = TRUE;
							client_entry[temp->hash_id]->server_ack = TRUE;

							PRINT("JOB_REQ to calc WordCount in file :%s %s%d", 
							      input_file, "sent to client_id: ", 
								  temp->hash_id);
						}
						
						/* move to next ptr */
						temp = temp->next;
					}	

					/* move to next grp */
				}
			}
			break;

		case JOB_FIND_MAX:
			break;

		case JOB_SERIES:
			break;
		
		default:
			ERROR("%s job_id: %d", "in default: no such JOB found.", job_id);
			break;
	}

	return rc;
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
	if (is_debug_mode() || clnt_debug_on) {
		char buff[1000] = {0};
		char temp_buff[100] = {0};
	    va_list args;
		sprintf(buff, "***DEBUG: ");

	    va_start(args, format);
    	vsprintf(temp_buff, format, args);
		strcat(buff, temp_buff);
	    va_end(args);
		
		strcat(buff, "\n");
		fprintf(stdout, "%s", buff);
		if (log_fp) {
			fputs(buff, log_fp);
		}
	}
}

void print_error(const char* format, ... ) 
{
	char buff[1000] = {0};
	char temp_buff[100] = {0};
    va_list args;
	sprintf(buff, "~~~ERROR: ");

    va_start(args, format);
    vsprintf(temp_buff, format, args );
	strcat(buff, temp_buff); 
    va_end(args);

	strcat(buff, "\n");
	fprintf(stderr, "%s", buff);
	if (log_fp) {
		fputs(buff, log_fp);	
	}
}

void print_alert(const char* format, ... ) 
{
    va_list args;
	fprintf(stderr, "\n#########################################################\n");
	fprintf(stderr, "***ALERT:");
    va_start(args, format);
    vfprintf(stderr, format, args );
    va_end(args);
	fprintf(stderr, "\n#########################################################\n");
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

void display_job_output(void)
{
	int i = 0, read_val = 0;
	FILE *fp = NULL;
	int count = 0;
	int num1 = 0;
	char outp_file[FILE_NAME_LEN];

	if (is_multicast()) {
		
		if (sub_job_count == 0 && exec_job_start) {
			for (i = 0; i < total_fd; i++) {
				snprintf(outp_file, FILE_NAME_LEN, "clnt_job_%d_%d",
                        1, i);
	
				fp = fopen(outp_file, "r");
				if (!fp) {
					ERROR("File %s (output from client_id: %d) for reading failed. errno: %s",
						  outp_file, i, strerror(errno));
					continue;
				} else {
					ALERT("%s %d", "output recieved from client_id:", i);
				}

				while ((read_val = fscanf(fp, "%d", &num1)) != EOF) {
					if (read_val == 1) {
						count++;	
					}
				
					printf("%d ", num1);

					// display from new line once 10 numbers are printed
					if (count == 10) {
						printf("\n");
						count = 0;
					}
				}
				//flush any rem data in buffer
				printf("\n");
				fflush(stdout);
				DEBUG("%s %d is over", "Ouput from Client:", i);
				fclose(fp);
			}
			exec_job_start = FALSE;
		}
		return;
	}

	for(i = 0; i < MAX_CLIENTS; i++) {
		if (client_entry[i] != NULL && client_entry[i]->is_participant) {
			if (!client_entry[i]->is_exec) {
				fp = fopen(client_entry[i]->file_outp, "r");
				if (!fp) {
					ERROR("File %s (output from client_id: %d) for reading failed",
						  client_entry[i]->file_outp, i);
					continue;
				} else {
					ALERT("%s %d", "output recieved from client_id:", i);
				}

				while ((read_val = fscanf(fp, "%d", &num1)) != EOF) {
					if (read_val == 1) {
						count++;	
					}
				
					printf("%d ", num1);

					// display from new line once 10 numbers are printed
					if (count == 10) {
						printf("\n");
						count = 0;
					}
				}
				//flush any rem data in buffer
				printf("\n");
				fflush(stdout);
				DEBUG("%s %d is over", "Ouput from Client:", i);
				fclose(fp);
				client_entry[i]->is_participant = FALSE;
				count = 0;
			} else if (client_entry[i]->is_exec) {

				/* This client participated in JOB, but couldnt respond
				 * with timeframe. Send JOB_TERMINATE request to this 
				 * client 
				 */
				(void) send_pkt_to_client(client_entry[i]->socket_id, 
										  JOB_TERM, NULL, i, NULL);
			}
		}
	}

	//reset job_sent timestamp, it will be set once a new job is executed
	job_sent_ts = 0;
}

inline bool is_prime (int num)
{
	int p =1, s = 1, i = 0;
	
	/* 
	 * logic to find square root of num which is 
	 * just less or equal.
	 */
	while (p <= num) {
		s++;
		p = s*s;
	}

	for (i = 2; i < s; i++) {
		if (num%i == 0) {
			return FALSE;
		}
	}
	
	return TRUE;
}

int del_file_if_exist(char *file) 
{
	
	if (!file) {
		return ERR_CODE;
	}

	if (!access(file, F_OK)) {
		if (remove(file)) {
			ERROR("%s. %s errno: %s", "Unable to delete file:", file, 
				 strerror(errno));
			return ERR_CODE;
		}
	}
	return 0;
}

bool is_file_exist(char *file) 
{
	
	if (!file) {
		return FALSE;
	}

	if (!access(file, F_OK)) {
			return TRUE;
	}

	return FALSE;
}

int compute_job(clnt_thread_arg_st *data)
{

	int start_num = 0, end_num = 0, i = 0;
	FILE *fp = NULL;
	char ascii_num[MAX_ASC_CHLEN];
	char ascii_word[MAX_WORD_LEN];

	if (!data) {
		ERROR("%s: %s", FUNC, "data is NULL, exiting");
		return ERR_CODE;
	}

	if (!data->outpt_file) {
		ERROR("%s: %s", FUNC, "outp_filename is NULL, exiting");
		return ERR_CODE;
	}

	start_num = data->start_range;
	end_num   = data->end_range;

	switch(data->job_id) {
		
		case JOB_PRIME:
			//Delete File if already exist
			(void) del_file_if_exist(data->outpt_file);

			fp = fopen(data->outpt_file, "a+");
			if (!fp) {
				ERROR("%s: %s", FUNC, "fp for filename is NULL, exiting");
				return ERR_CODE;
			}

			for(i = start_num; i <= end_num; i++) {
				if (is_prime(i)) {
					//write this number to output file
					snprintf(ascii_num, MAX_ASC_CHLEN, "%d ", i);
					fputs(ascii_num, fp);
				}
			}
			fclose(fp);
			break;

		case JOB_WC:
			fp = fopen(data->inpt_file, "r");
			if (!fp) {
				ERROR("%s: %s", FUNC, "fp for filename is NULL, exiting");
				return ERR_CODE;
			}

			i = 0;
			while (fscanf(fp, "%s", ascii_word) != EOF) {	
				i++;
			}
		   
			fclose(fp);

			/* Write the o/p to file */
			(void) del_file_if_exist(data->outpt_file);
			fp = fopen(data->outpt_file, "a+");	
			if (!fp) {
				ERROR("%s: %s", FUNC, "fp for filename is NULL, exiting");
				return ERR_CODE;
			}

			DEBUG("word count in file: %s is %d", data->inpt_file, i);
			snprintf(ascii_word, MAX_WORD_LEN, "%d", i);
			fputs(ascii_word, fp);
			fclose(fp);
			break;

		case JOB_SERIES:
			break;

		case JOB_FIND_MAX:
			break;
		
		default:
			ERROR("%s: %s", FUNC, "in default, exiting");
			return ERR_CODE;
	}
	return 0;
}

int action_on_client_state(int socket_fd, 
						   client_state_en client_state_arg,	 
		                   struct sockaddr_in *addr)
{
	int rc = 0;
	int numbytes = 0;
	msg_st *msg = NULL;
	int msg_data_len = 0;
	static int hash_id = 0, group_id = 0;
	static time_t base_time = 0;
	time_t curr_time = 0;
	static int hbeat_counter = 0;
	bool is_hbt_time_set = FALSE;
	char outp_file[FILE_NAME_LEN];
	pthread_t job_thread;
	pthread_attr_t attr;
	clnt_thread_arg_st arg;
	int job_id = 0;
	struct sockaddr_in client_addr;
	struct sockaddr_in serv_addr;
	int addr_len = 0;
	
	/* chk if socket_fd is valid? */
	if (!socket_fd) {
		ERROR("%s %s", FUNC, "socket_fd is not valid");
		return ERR_CODE;
	}

	switch (client_state_arg) {

		case CLIENT_INIT:

			/* CLient now shud send a registration req to server */
			msg = calloc(1, sizeof(msg_st));
			msg->type = REGISTER_CLIENT;
			msg->len = 0;
			msg->group_id = req_grp_id;
			msg->hash_id = 0;

			if (is_multicast_supp) {
				numbytes = sendto(socket_fd, msg, sizeof(msg_st), 0, 
								 (struct sockaddr *) addr, 
								 sizeof(struct sockaddr_in));
				if (RC_NOTOK(numbytes)) {
					ERROR("%s %s", 
						  "Registration msg cudn't be send to server. errno: ", 
					   	   strerror(errno));
					free_msg(msg);
					return ERR_CODE;
				} else {
					PRINT("%s %s, port:%d for group_id: %d", 
						"Registartion msg sent to server: ", 
						inet_ntoa(addr->sin_addr), 
						htons(addr->sin_port), msg->group_id);
					free_msg(msg);
				}
			} else {
				numbytes = send(socket_fd, msg, sizeof(msg), 0);
				if (RC_NOTOK(numbytes)) {
					ERROR("%s %s", 
						  "Registration msg cudn't be send to server. errno: ", 
					   		strerror(errno));
					free_msg(msg);
					return ERR_CODE;
				} else {
					PRINT("%s %s for group_id: %d", 
							"Registartion msg sent to server: ", 
							inet_ntoa(addr->sin_addr), msg->group_id);
					free_msg(msg);
				}
			}
			client_state = CLIENT_REG_SENT;
			break;

		case CLIENT_REG_SENT:
		case CLIENT_ACK_WAIT:
			/* client will wait for any ack msg from server */
			if (is_multicast_supp) {
				msg_data_len = get_msg_data_len_udp(socket_fd);
				if (RC_NOTOK(msg_data_len)) {
					return ERR_CODE;
				} else {

					memset(&serv_addr, 0, sizeof(serv_addr));
					addr_len = sizeof(struct sockaddr_in);
					DEBUG("%s %d length", "ACK_FRM_SERVER data part is of", 
															msg_data_len);
            	    msg = calloc(1, sizeof(msg_st)+msg_data_len);
					CHK_ALLOC(msg);
		
					numbytes = recvfrom(socket_fd, msg, 
									sizeof(msg_st)+msg_data_len, 0,
									(struct sockaddr *) &addr,
									(socklen_t *)&addr_len);
    	            if (RC_NOTOK(numbytes)) {
        	            ERROR("%s %s", "ACK_FRM_SERVER recv() failed. errno.", 
								       strerror(errno));
						return ERR_CODE;
	                } else {
						PRINT("%s", "multicast ACK_FRM_SERVER "
									"recv successfully");
						my_client_id = msg->hash_id;

						/* Join to respective multicast group */
						rc = join_mcast_group(req_grp_id);
						if (RC_NOTOK(rc)) {
							ERROR("Couldnt Join Multicast group for grp_id: %d",
								  req_grp_id);
							client_state = CLIENT_EXIT;
						}
					}
				}
			} else {
					/* TCP based comm */
					msg_data_len = get_msg_data_len(socket_fd);
					if (RC_NOTOK(msg_data_len)) {
						return ERR_CODE;
					} 

					DEBUG("%s %d length", "ACK_FRM_SERVER data part is of", 
															msg_data_len);
            	    msg = calloc(1, sizeof(msg_st)+msg_data_len);
					CHK_ALLOC(msg);

                	numbytes = recv(socket_fd, msg,
                     	           sizeof(msg_st)+msg_data_len, 0);
	                if (RC_NOTOK(numbytes)) {
    	                ERROR("%s %s", "ACK_FRM_SERVER recv() failed. errno.", 
								       strerror(errno));
						free_msg(msg);
						return ERR_CODE;
                	} else {
						PRINT("%s %s my client_id: %d and grp_id: %d", 
								get_msg_type_str(msg->type),
								"recieved from server", msg->hash_id, 
                                msg->group_id);

						hash_id = msg->hash_id;
						group_id = msg->group_id;
						free_msg(msg);
					}
				}
			client_state = CLIENT_ACK_OK;
			break;

		case CLIENT_ACK_OK:

			if (is_multicast_supp) {
				memset(&client_addr, 0, sizeof(client_addr));
				addr_len = sizeof(struct sockaddr_in);

				msg_data_len = get_msg_data_len_udp(clnt_mcast_fd);

				DEBUG("%s %d length", "JOB data part is of", 
											msg_data_len);
	            msg = calloc(1, sizeof(msg_st)+msg_data_len);
				CHK_ALLOC(msg);

				/* We need to listen to mcast_fd as server will
				 * now be sending job details via MCAST */
				numbytes = recvfrom(clnt_mcast_fd, msg, 
									sizeof(msg_st)+msg_data_len, 0,
									(struct sockaddr *) &client_addr,
									(socklen_t *)&addr_len);
	            if (RC_NOTOK(numbytes)) {
    	            ERROR("%s %s", "JOB_REQ recv() failed. errno.", 
						       strerror(errno));
					free_msg(msg);
					return ERR_CODE;
            	} else {
					if (!job_in_progress) {
						PRINT("%s", "JOB_REQ recvd successfully");
						job_in_progress = TRUE;
					} else {
						free_msg(msg);
						client_state = CLIENT_ACK_OK;
						break;
					}
				}
			} else {

				/* TCP mode operation */
				/* 
				 * Client will send periodic hearbeats to server to inform 
				 * server that its still alive 
				 */
				hbeat_counter++;
				is_hbt_time_set = FALSE;
				if (hbeat_counter == 1) {
					base_time = time(NULL);	
				}
				sleep(1);
				curr_time = time(NULL);

				if (( (curr_time-base_time) >= (HBT_TIME-1)) &&
					 ((curr_time-base_time) <= (HBT_TIME+1))) {
					base_time = time(NULL);
					is_hbt_time_set = TRUE;
				}
		
				if (is_hbt_time_set || (hbeat_counter == 1)) {
					msg = calloc(1, sizeof(msg_st));
					if (!msg) {
						ERROR("Calloc failed during HBEAT case at line: %d"
							  " file: %s", __LINE__, __FILE__);
						return ERR_CODE;
					}

					msg->type = HEARTBEAT;
					msg->len = 0;
					msg->group_id = group_id;
					msg->hash_id = hash_id;

					numbytes = send(socket_fd, msg, sizeof(msg_st), 0);
					if (RC_NOTOK(numbytes)) {
						ERROR("%s %s", 
					  		"HEARTBEAT msg cudn't be send to server. errno: ", 
					   		strerror(errno));
						free_msg(msg);
						return ERR_CODE;
					} else {
						DEBUG("%s %s for group_id: %d", 
							"HEARTBEAT msg sent to server: ", 
							inet_ntoa(addr->sin_addr), msg->group_id);
						client_state = CLIENT_ACK_OK;
						free_msg(msg);
					}
				}

				/* Now chk in NON-WAIT mode for any recv pkt */
				msg_data_len = get_msg_data_len_non_wait(socket_fd); 
				if (RC_NOTOK(msg_data_len)) {
					break;
				} else {
					/* we have recieved a valid PKT with proper len */
					msg = calloc(1, sizeof(msg_st)+msg_data_len);
					if (!msg) {
						ERROR("Calloc failed during HBEAT case "
							  "at line: %d file: %s", __LINE__, __FILE__);
						return ERR_CODE;
					}

        	        numbytes = recv(socket_fd, msg,
            	                    sizeof(msg_st)+msg_data_len, 0);
                	if (RC_NOTOK(numbytes)) {
	                    ERROR("%s %s", "msg recv() failed. errno.", 
								       strerror(errno));
						free_msg(msg);
						return ERR_CODE;
	                } else {
						DEBUG("%s %s bytes: %d", 
									get_msg_type_str(msg->type),
									"recieved from server", numbytes);
					}
				}
			}

			if (msg && msg->type == JOB_REQ) {
						
				job_id = msg->job_data->job_id;
				if (is_multicast_supp) {
					arg.client_id = my_client_id;
				} else {
					arg.client_id = msg->hash_id;
				}
				arg.group_id = msg->group_id;
				arg.job_id = msg->job_data->job_id;
				arg.start_range = msg->job_data->start_range;
				arg.end_range   = msg->job_data->end_range;
				
				recvd_job_id = job_id;

				PRINT("%s %d (from_num: %d to_num: %d)",
					  "Recvd JOB_REQ. job_id:", 
					  job_id, msg->job_data->start_range, 
					  msg->job_data->end_range);
	
				snprintf(outp_file, FILE_NAME_LEN, "clnt_job_%d_%d", 
						job_id, arg.client_id);
				DEBUG("%s %s", "output file will be:", outp_file);
	
				strcpy(arg.inpt_file, msg->job_data->inpt_file);
				strcpy(arg.outpt_file, outp_file);
		
				/* set thread to be used to detached state */
				rc = pthread_attr_init(&attr);
				if (rc) {
					ERROR("%s %s", "Client's thread's attribute "
								"init failed. errno. :", 
								strerror(errno));
				}

				rc = pthread_attr_setdetachstate(&attr, 
								PTHREAD_CREATE_DETACHED);
				if (rc) {
					ERROR("%s errno.: %s", "Client's thread coudnt "
											"be set as DETACHED thread", 
									  		strerror(errno));
				}	

				/* create a thread that will compute the job */
				rc = pthread_create(&job_thread, &attr,
							    exec_job_thread, &arg);
		
				/* 
				 * Destroy the attr created, as we are done 
				 * with its use
				 */
				(void) pthread_attr_destroy(&attr);

				if (rc) {
					ERROR("exec_job_thread creation failed. errno: %s",
						   strerror(errno));
					free_msg(msg);
					break;
				}
				free_msg(msg);
			} else if (msg && msg->type == JOB_TERM) {
				PRINT("%s %s", "Recvd Job Termination request", 
							 "from server due to timeout");
				//cancel the execution thread
				rc = pthread_cancel(job_thread);
				if (!rc) {
					DEBUG("%s", "Thread cancellation successfull");
				}
				free_msg(msg);
			} else if (msg) {
				DEBUG("%s: %d", "msg type is diff", msg->type);
				free_msg(msg);
			}
			client_state = CLIENT_ACK_OK; 
			break;

		case CLIENT_EXIT:
			/* Client is Exiting, send Down signal to server */
			msg = calloc(1, sizeof(msg_st));
			msg->type = CLIENT_DOWN;
			msg->len = 0;
			msg->group_id = group_id;
			msg->hash_id = hash_id;

			if (is_multicast_supp) {

			} else {
				numbytes = send(socket_fd, msg, sizeof(msg_st), 0);
				if (RC_NOTOK(numbytes)) {
					ERROR("%s %s", 
						  "CLIENT_DOWN msg cudn't be send to server. errno: ", 
						   strerror(errno));
					free_msg(msg); 
					return ERR_CODE;
				} else {
					PRINT("%s %s for group_id: %d", 
							"CLIENT_DOWN msg sent to server: ", 
							inet_ntoa(addr->sin_addr), msg->group_id);
					client_state = CLIENT_EXIT;
					free_msg(msg);
				}
			}
			break;

		default:
			DEBUG("%s: %s", FUNC, "In default");
			break;
	}
	return rc;
}

int server_action_on_msg(int socket_id, int hash_id,
						 msg_st *msg)
{

	if (!msg) {
		ERROR("%s %s", FUNC, "msg is Null");
		return ERR_CODE;
	}
	
	switch (msg->type) {
		
		case REGISTER_CLIENT:
			/* Do nothing for now */
			break;

		case HEARTBEAT:
			client_entry[hash_id]->is_active = TRUE;
			client_entry[hash_id]->hbeat_time = time(NULL);

			DEBUG("Sock_Id: %d recvd Hearbeat for hash_id: %d from client: %s",
					socket_id,  
					hash_id, inet_ntoa(client_entry[hash_id]->addr));
			break;
		
		case JOB_RESP:
			if (is_multicast()) {
				sub_job_count--;
				break;
			}
			client_entry[hash_id]->is_exec = FALSE;
			strcpy(client_entry[hash_id]->file_outp, msg->job_data->outpt_file);

			PRINT("JOB RESPONSE recvd from Client_id: %d (%s) for Job_id: %d",
					hash_id, inet_ntoa(client_entry[hash_id]->addr), 
					msg->job_data->job_id);
			break;

		case CLIENT_DOWN:
			client_entry[hash_id]->is_active = FALSE;
			readfds[hash_id].fd = -1;
			PRINT("Client_id %d (%s) has been marked Inactive %s", 
				  hash_id, inet_ntoa(client_entry[hash_id]->addr),
				  "in response to CLIENT_DOWN msg");
			break;

		default:
			ERROR("%s: In default case", FUNC);
	}
	
	return 0;
}

char * sigtostr(int signum)
{
	switch (signum) {

		case SIGPIPE:
			return "SIGPIPE";	
		case SIGTERM:
			return "SIGTERM";
		case SIGINT:
			return "SIGINT";
		case SIGABRT:
			return "SIGABRT";
		case SIGFPE:
			return "SIGFPE";
		case SIGSEGV:
			return "SIGSEGV";
		default:
			return "UNKNOWN";
	}
}

void cleanExit(int signum)
{

    printf("Program exiting with signal: %s\n", sigtostr(signum));
    exit(0);
}

void cleanExit_client(int signum)
{
	int rc = 0;	

    PRINT("Recieved EXIT(signal: %s) %s", sigtostr(signum), " Informing Server");

	rc = action_on_client_state(comm_sock_copy, CLIENT_EXIT,
								&server_addr_copy);
	if (RC_NOTOK(rc)) {
		ERROR("%s", "SERVER couldn't be informed about client exit");
	}

	sleep(2);
    exit(0);
}

int count_total_grp (void) {
	int i = 0;
	int counter = 0;
	for (i = 0; i < MAX_CLIENTS; i++) {
		if (grp_data[i] != NULL) {
			counter++;
		}
	}
	return counter;
}

void display_grp_info (short grp_id) 
{

	grp_data_st *temp = NULL;
	temp = grp_data[grp_id];
	int counter = 0;
	
	PRINT("Group Id: %d contains below Client_ids: ", grp_id);
	while (temp != NULL) {
		printf(" %d ", temp->hash_id);
		temp = temp->next;
		counter++;
	}

	printf("\nTotal: %d\n", counter);
	printf("=========================================================\n");
}

int count_grp_total (short grp_id) 
{

	grp_data_st *temp = NULL;
	temp = grp_data[grp_id];
	int counter = 0;
	
	while (temp != NULL) {
		temp = temp->next;
		counter++;
	}

	return counter;
}

void * verify_client_hbeat (void *arg) 
{
	int i = 0;
	arg = NULL;
	time_t curr_time = 0;
	int job_pend_counter = 0;
	int job_exec_start = 0;

	DEBUG("%s: %s", FUNC, "called");	

	/* probe every HBT_TIME if heartbeat is recvd properly */
	while (TRUE) {

		if (is_multicast()) {
			if (sub_job_count == 0 && exec_job_start) {
				//All jobs are completed, display output
				PRINT("%s", "JOB RESPONSE recvd from all clients");
				display_job_output();
				disp_cons_job = TRUE;
			}

			//check if timeout happend, display all recvd Job output
			sleep(1);
			curr_time = time(NULL);
			if ((job_sent_ts != 0) && (curr_time - job_sent_ts) > JOB_FIN_TIME) {

				if (exec_job_start) {
					/* signal end of JOB */
					PRINT("%s", "Wait for JOB RESP timer expired.. Displaying recvd results");
					display_job_output();
					disp_cons_job = TRUE;	
					exec_job_start = FALSE;
				}
			}
			continue;
		}

		//reset job_pend_counter 
		job_pend_counter = 0;
		job_exec_start   = 0;

		/* As Job execution starts, hbeat_chk_start will be set to TRUE */
		if (hbeat_chk_start == FALSE) {
			continue;
		}

		curr_time = time(NULL);

		// chk if client HBEAT Timestamp hasn't expired	
		for(i = 0; i < MAX_CLIENTS; i++) {
			if (client_entry[i] != NULL && 
					client_entry[i]->is_active && 
					client_entry[i]->hbeat_time != 0) {
				if ((curr_time - client_entry[i]->hbeat_time) > HBT_EXPTIME) {
					client_entry[i]->is_active = FALSE;
					ERROR("Client_id: %d (%s) has been marked inactive %s", i,
						  inet_ntoa(client_entry[i]->addr),
						  "due to no HEARTBEAT signal");
				}
			}
		}

		//check if any of the client is still executing JOB
		for(i = 0; i < MAX_CLIENTS; i++) {
			if (client_entry[i] != NULL && client_entry[i]->is_participant) {
				
				//logic to ckeck if any JOB_RESP is still pending 
				if (client_entry[i]->is_exec) {
					job_pend_counter++;
				} else {
					job_exec_start++;
				}
			}
		}
		
		if (job_pend_counter == 0 && job_exec_start) {
			//All jobs are completed, display output
			PRINT("%s", "JOB RESPONSE recvd from all clients");
			display_job_output();
			disp_cons_job = TRUE;
		}

		//check if timeout happend, display all recvd Job output
		curr_time = time(NULL);
		if ((job_sent_ts != 0) && (curr_time - job_sent_ts) > JOB_FIN_TIME) {
			/* signal end of JOB */
			PRINT("%s", "Wait for JOB RESP timer expired.. Displaying recvd results");
			display_job_output();
			disp_cons_job = TRUE;	
		}
	} // end of while 
}

void * send_thread (void *arg) 
{

	int i = 0, rc = 0;
	int job_id = 0;
		
	arg = NULL;	
	DEBUG("%s: %s", FUNC, "entered, waiting for ENTER key event");

	/* wait for user to confirm if all clients are done */
	while (TRUE) {
		if (getchar() == '\n') {
			DEBUG("%s", "User pressed Enter, continue to send_thread");
			break;
		}
	}

	while (TRUE) {

		if (!is_multicast()) {
			for (i = 0; i < MAX_CLIENTS; i++) {
				/* 
				 * If any client's server_ack is pending, then server must 
				 * first send an ack to server, informing client abt 
			 	 * the group  and hash_id the client has been associated to
				 */
				if (client_entry[i] != NULL && client_entry[i]->is_active) {

					if (client_entry[i]->server_ack == FALSE) {
	
						rc = send_pkt_to_client(client_entry[i]->socket_id, 
												ACK_FRM_SERVER, NULL, i, NULL);
						if (RC_NOTOK(rc)) {
							/* move fwd with other clients */
							continue;
						}
					}
				} // client_entry null chk
			} //for loop end
		
			/* trigger start of Hbeat chk */
			hbeat_chk_start = TRUE;
		} //is_multicast check ends here

		/* Display Available Jobs which can be executed */ 
		if (disp_cons_job) {
			display_job_info (&job_id);

			if (is_multicast()) {
				rc = send_job_to_grp_mcast(job_id);
			} else {	
				rc = send_job_to_grp(job_id);
			}
			
			if (RC_NOTOK(rc)) {
				ERROR("Job_id: %d %s", "sending failed to grp");
			} else {
				job_sent_ts = time(NULL);
			}
			disp_cons_job = FALSE;
		}
	}
	return NULL;
}

void * recv_thread (void *arg) 
{

	int rc = 0, i = 0;
	int msg_data_len = 0;
	msg_st *msg = NULL;
	struct sockaddr_in client_addr;
	int addr_len = 0;

	arg = NULL;

	while (TRUE) {

		if (is_multicast()) {

			msg_data_len = get_msg_data_len_udp(srvr_master_sock);			
			msg = (msg_st *)calloc(1, sizeof(msg_st)+msg_data_len);
			if (!msg) {
				ERROR("%s at File: %s, line: %d", 
					 "alloc failure while recv pkt frm client",
					  __FILE__, __LINE__);
				continue;
			}
			addr_len = sizeof(struct sockaddr_in);	 
			memset(&client_addr, 0, sizeof(client_addr));
			rc = recvfrom(srvr_master_sock, msg, sizeof(msg_st), 0,
							   (struct sockaddr *)&client_addr, 
 						 	   (socklen_t *)&addr_len);
	
			if (RC_NOTOK(rc)) {
				ERROR("%s %s", "msg from Client failed to be recvd. errno. ", 
								strerror(errno));
				continue;
			} else {
				DEBUG("%s: recvd from client", get_msg_type_str(msg->type));
				rc = server_action_on_msg(srvr_master_sock, 0, msg);
				if (RC_NOTOK(rc)) {
					ERROR("%s: %s", FUNC, "failed");
				}
			}
			
		} else {

			rc = poll(readfds, total_fd, 1000);
			if (RC_NOTOK(rc)) {
				ERROR("%s %s", "poll() failed. errno:", strerror(errno));
				continue;
			} else if (rc == 0) {
				continue;
			}

			/* Poll returned something valid, chk what */
			for (i = 0; i < MAX_CLIENTS; i++) {

				if(client_entry[i] != NULL && 
						client_entry[i]->is_active) {

					if (readfds[i].revents & POLLIN) {
						DEBUG("Socket_id: %d of Client Id: %d is set", 
							 readfds[i].fd,
							 client_entry[i]->hash_id);
						msg_data_len = get_msg_data_len(client_entry[i]->socket_id);
						if (RC_NOTOK(msg_data_len)) {
							continue;
						}

						msg = (msg_st *)calloc(1, sizeof(msg_st)+msg_data_len);
						if (!msg) {
							ERROR("%s id: %d", 
								 "alloc failure while recv pkt frm client",
								  client_entry[i]->hash_id);
							continue;
						}
						rc = recv(client_entry[i]->socket_id, msg, 
								sizeof(msg_st)+msg_data_len, 0);
						if (RC_NOTOK(rc)) {
							ERROR("%s %d (%s)", 
									"pkt failed to be recvieved frm client id:",
									client_entry[i]->hash_id,
									inet_ntoa(client_entry[i]->addr));
							free_msg(msg);
							continue;
						} else {
							/* pkt recv success */
							rc = server_action_on_msg(client_entry[i]->socket_id, 
													  i, msg);
							if (RC_NOTOK(rc)) {
								ERROR("%s: %s", FUNC, "failed");
							}
							free_msg(msg);
						}
					} // end of revents event chk
				} // end of client_entry null chk
			} //end of for loop
		} // end of else for TCP based comm
	}	//while end

	return NULL;
}

void * exec_job_thread (void *arg)
{
	clnt_thread_arg_st *thread_arg = (clnt_thread_arg_st *)arg;
	msg_st *msg = NULL;
	int rc = 0, numbytes = 0;

	if (!thread_arg) {
		ERROR("%s %s", FUNC, "thread_arg is NULL, exiting");
		return NULL;
	}

	rc =  compute_job(thread_arg);
	if (RC_ISOK(rc)) {
		/* mark the job as done */
		job_in_progress = FALSE;

		/* Construct a JOB_RES msg */
		msg = (msg_st *) calloc(1, sizeof(msg_st) + sizeof(job_st));
		if (!msg) {
			ERROR("%s %d file: %s", "calloc failed at line:",
				  __LINE__, __FILE__);
			return NULL;
		}

		msg->type = JOB_RESP;
		msg->len = sizeof(job_st);
		msg->group_id = thread_arg->group_id;
		msg->hash_id = thread_arg->client_id;
		msg->job_data->job_id = thread_arg->job_id;
		strcpy(msg->job_data->outpt_file, thread_arg->outpt_file);

		if (is_multicast_supp) {
			numbytes = sendto(comm_sock_copy, msg, 
							  sizeof(msg_st)+sizeof(job_st), 0,
							  (struct sockaddr *)&server_addr_copy,
							  sizeof(struct sockaddr_in));	
		} else { 
			numbytes = send(comm_sock_copy, msg, 
							sizeof(msg_st)+sizeof(job_st), 0);
		}
	
		if (RC_NOTOK(numbytes)) {
			ERROR("%s %d %s errno: %s", 
				  "Job_RESP for job_id:", thread_arg->job_id,
				  "couldn't be sent.", strerror(errno));
			free_msg(msg);
		} else {
			PRINT("%s %d in file: %s %s", 
				 "JOB_RESP for job_id:", 
				 thread_arg->job_id, msg->job_data->outpt_file, 
				 "sent successfully");
			free_msg(msg);
		}
	}

	return NULL;	
}

bool is_client_entry_exists(struct sockaddr_in *addr, int *index) 
{

	for (int i = 0; i < MAX_CLIENTS; i++) {
		if (client_entry[i] == NULL) {
			continue;
		}

		if ((client_entry[i]->family == addr->sin_family) && 
			!memcmp(&(client_entry[i]->addr), &(addr->sin_addr), 
									sizeof(struct in_addr)) &&
			(client_entry[i]->is_active == FALSE)) {
			*index = i;
			return TRUE;
		}
	}
	return FALSE;
}

int get_msg_data_len (int socket_id)
{
	int rc = 0;
	msg_st dummy_msg;

	memset(&dummy_msg, 0 , sizeof(msg_st));
	rc = recv(socket_id, &dummy_msg, 
			 sizeof(msg_st), MSG_PEEK);
	if (RC_NOTOK(rc)) {
		ERROR("%s: %s %s", FUNC, "dummy_msg recv() failed. errno. ",
							strerror(errno));
		return rc;
	}	

	return dummy_msg.len;
}

int get_msg_data_len_udp (int socket_id)
{
	int rc = 0;
	msg_st dummy_msg;
	int addr_len = 0;
	struct sockaddr_in addr;

	memset(&addr, 0, sizeof(addr));
	memset(&dummy_msg, 0 , sizeof(msg_st));
	addr_len = sizeof(struct sockaddr_in);

	rc = recvfrom(socket_id, &dummy_msg, 
				  sizeof(msg_st), MSG_PEEK, (struct sockaddr *)&addr, 
				  (socklen_t *)&addr_len);
	if (RC_NOTOK(rc)) {
		ERROR("%s: %s %s", FUNC, "dummy_msg recv() failed. errno. ",
							strerror(errno));
		return rc;
	}	

	return dummy_msg.len;
}

int get_msg_data_len_non_wait (int socket_id)
{

	int rc = 0;
	msg_st dummy_msg;

	memset(&dummy_msg, 0 , sizeof(msg_st));
	rc = recv(socket_id, &dummy_msg, sizeof(msg_st), 
			  MSG_PEEK | MSG_DONTWAIT);
	if (RC_NOTOK(rc)) {
		if (errno != EAGAIN) {
			ERROR("%s: %s %s", FUNC, "dummy_msg recv() failed. errno. ",
							strerror(errno));
			return -1;
		}
		return -1;
	}
		
	if (dummy_msg.type == MSG_RES) {
		return -1;
	}
	return dummy_msg.len;
}

bool is_multicast (void) 
{
	if (op_mode & MULTICAST_ON)
		return TRUE;
	
	return FALSE;
}

bool is_debug_mode (void) 
{
	if (op_mode & DEBUG_ON)
		return TRUE;
	
	return FALSE;
}

int join_mcast_group (grp_id_en group_id)
{
	int rc = 0;
	int reuse_sock = 1;

	memset(&mreq_global, 0, sizeof(mreq_global));
	clnt_mcast_fd = socket(ADDR_FAMILY, SOCK_DGRAM, 0);
	if (RC_NOTOK(clnt_mcast_fd)) {
		ERROR("Multicast socket creation failed. errno : %s", strerror(errno));
		return ERR_CODE;
	}

	/* allow multiple sockets to use the same PORT number */
	rc = setsockopt(clnt_mcast_fd, SOL_SOCKET, SO_REUSEADDR, 
		   			&reuse_sock, sizeof(reuse_sock));
	if (RC_NOTOK(rc)) {
		ERROR("Failed to set socket to REUSEADDR. errno: %s", 
			 strerror(errno));
	}

     /* Request that the kernel join a multicast group */
	if (group_id == JOB_COMPUTE) {
		mreq_global.imr_multiaddr.s_addr = inet_addr(COMPUTE_MCAST_GRP);
	} else if (group_id == JOB_ANALYSIS) {
    	mreq_global.imr_multiaddr.s_addr = inet_addr(ANALYSIS_MCAST_GRP);
	}
    mreq_global.imr_interface.s_addr=htonl(INADDR_ANY);
    rc = setsockopt(clnt_mcast_fd, IPPROTO_IP, IP_ADD_MEMBERSHIP, 
					&mreq_global, sizeof(mreq_global));

	if (RC_NOTOK(rc)) {
		ERROR("Client Failed to add socket to multicast grp. errno. :%s", 
			  strerror(errno));
		return ERR_CODE;
	} else {
		DEBUG("%s", "setopt succ for MULTICAST grp");
	}

	/* set up destination address */
	memset(&clnt_mcast_addr, 0, sizeof(clnt_mcast_addr));
    clnt_mcast_addr.sin_family = ADDR_FAMILY; 
    clnt_mcast_addr.sin_addr.s_addr = htonl(INADDR_ANY); 
	clnt_mcast_addr.sin_port = htons(COMP_MCAST_PORT);
	
	/* bind to receive address */
	rc = bind(clnt_mcast_fd, (struct sockaddr *) &clnt_mcast_addr, 
			  sizeof(clnt_mcast_addr));
	if (RC_NOTOK(rc)) {
		ERROR("Bind failed for mcast socket. errno. :%s", strerror(errno));
		return ERR_CODE;
	} else {
		DEBUG("%s", "Bind success for multicast fd");
	}	

	return 0;
}

int server_join_mcast_group (grp_id_en group_id, int sock_id)
{
	int rc = 0;

     /* Request that the kernel join a multicast group */
	if (group_id == JOB_COMPUTE) {
		mreq_global.imr_multiaddr.s_addr = inet_addr(COMPUTE_MCAST_GRP);
	} else if (group_id == JOB_ANALYSIS) {
    	mreq_global.imr_multiaddr.s_addr = inet_addr(ANALYSIS_MCAST_GRP);
	} 

    mreq_global.imr_interface.s_addr = htonl(INADDR_ANY);
    rc = setsockopt(sock_id, IPPROTO_IP, IP_ADD_MEMBERSHIP, 
					&mreq_global, sizeof(mreq_global));
	if (RC_NOTOK(rc)) {
		ERROR("Failed to add socket to multicast grp. errno. :%s", 
			  strerror(errno));
		return ERR_CODE;
	}

	return 0;
}

int leave_mcast_group (void)
{
	int rc = 0;
	setsockopt (clnt_mcast_fd, IPPROTO_IP, IP_DROP_MEMBERSHIP, 
				&mreq_global, sizeof(mreq_global));
	if (RC_NOTOK(rc)) {
		ERROR("Failed to remove socket from multicast grp. errno: %s",
			 strerror(errno));
		return ERR_CODE;
	}	
	return rc;
}

int server_leave_mcast_group (grp_id_en group_id, int sock_id)
{
	int rc = 0;

	if (group_id == JOB_COMPUTE) {
		mreq_global.imr_multiaddr.s_addr = inet_addr(COMPUTE_MCAST_GRP);
	} else if (group_id == JOB_ANALYSIS) {
    	mreq_global.imr_multiaddr.s_addr = inet_addr(ANALYSIS_MCAST_GRP);
	}

	setsockopt(sock_id, IPPROTO_IP, IP_DROP_MEMBERSHIP, 
				&mreq_global, sizeof(mreq_global));
	if (RC_NOTOK(rc)) {
		ERROR("Failed to remove socket from multicast grp. errno: %s",
			 strerror(errno));
		return ERR_CODE;
	}	
	return rc;
}

void disp_client_help_msg(void) 
{

 	fprintf(stdout, "   usage: client [-h] [-d] [-a servr_addr] [-p portnum]\n");
	fprintf(stdout, "\toption h:  help on usage\n");
	fprintf(stdout, "\toption d:  enable debug messages\n");
	fprintf(stdout, "\toption p:  to override default port number\n");
	fprintf(stdout, "\toption a:  specify server addr to connect\n");
	fprintf(stdout, "\toption g:  specify group to which client wants to belong\n");
}

void disp_server_help_msg(void) 
{

	fprintf(stdout, "   usage: server [-h] [-d] [-p portnum]\n");
	fprintf(stdout, "\toption h:  help on usage\n");
	fprintf(stdout, "\toption d:  enable debug messages\n");
	fprintf(stdout, "\toption m:  enable multicast mode\n");
	fprintf(stdout, "\toption p:  to override default port number\n");
}
