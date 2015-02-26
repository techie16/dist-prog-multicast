#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdbool.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdlib.h>
#include <regex.h>

char str[55];
void get_server_addr(FILE *fp, char *buf) {
	char addr_str[100];
	char port_str[100];
	char *index_str;
	int index_addr = 0;

	fgets(addr_str, 100, fp);
	fgets(port_str, 100, fp);

	printf("First Line: %s", addr_str);
	printf("Secnd Line: %s\n", port_str);

	if ( strstr(addr_str, "server_addr") !=  NULL) {
		if ((index_str = strstr(addr_str, "=")) == NULL) {
			printf("address String not found\n");
		}
	} else {
		printf("Error: server_addr not found");
	}

	printf("address of server: %s", index_str+1);
}

int main(int argc, char **argv)
{

	FILE *fp = NULL;	
	fp = fopen("conf.txt", "a+");
	get_server_addr(fp, str);
}
