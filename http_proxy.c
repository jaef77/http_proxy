#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <pthread.h>
#include <arpa/inet.h>

#define BUFSIZE 8192
#define TIMEOUT 60
#define CLIENT_NUM 20


typedef struct sock_info
{
	int sock;
	struct sockaddr_in addr;
}sock_info;

void dump(char *p, int len);
void get_host_name(char *packet, char *hostname);
void *http_relay(void *client_socket_information);

int client_sock[CLIENT_NUM];
int broad_echo = 0;


/***************************main*******************************/
int main(int argc, char **argv) {
	int origin;
	int portnum, option_val = 0;
	int n, client_len;
	double real_time;
	struct sockaddr_in server_addr;
	struct sockaddr_in client_addr;
	struct hostent *client;
	char *client_ip_addr;
	sock_info client_socket_info;


	/* input : port */
	if (argc == 2)
	{
		broad_echo = 0;
		portnum = atoi(argv[1]);
	}
	else if (argc == 3)
	{
		if(memcmp("-b", argv[2], 2) == 0)
		{
			broad_echo = 1;
			portnum = atoi(argv[1]);
		}
		else
		{
			fprintf(stderr, "SYNTAX : echoserver <port> [-b]\n");
			return -1;
		}
	}
	else
	{
		printf("You have to exec %s as\n%s <port>\n\n", argv[0], argv[0]);
		return -1;
	}

	/* creating the socket - origin : 연결요청에 대해 새로운 소켓 생성 */
	origin = socket(AF_INET, SOCK_STREAM, 0);
	if (origin < 0)
	{
		perror("ERROR : Origin Socket is not well created!\n\n");
		return -1;
	}
	option_val = 1;
	setsockopt(origin, SOL_SOCKET, SO_REUSEADDR,
		(const void *)&option_val, sizeof(int));


	/************** server's address setting **************/
	memset((char *)&server_addr, 0, sizeof(server_addr));
	server_addr.sin_family = AF_INET;
	server_addr.sin_port = htons(portnum);
	server_addr.sin_addr.s_addr = htonl(INADDR_ANY);

	/************** binding **************/
	if (bind(origin, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)
	{
		perror("ERROR : Origin Binding Failured!\n\n");
		return -1;
	}

	/************** listen for conneciton **************/
	if (listen(origin, CLIENT_NUM) < 0)
	{
		perror("ERROR : Origin Listening Failured!\n\n");
		return -1;
	}

	/************** accept **************/
	client_len = sizeof(client_addr);
	int client_number = 0;
	while (client_number < CLIENT_NUM)
	{
		/* accept */
		client_sock[client_number] = accept(origin, (struct sockaddr *)&client_addr, &client_len);
		if (client_sock[client_number] < 0)
		{
			perror("ERROR : Accepting Error!\n\n");
			return -1;
		}

		pthread_t thread_id;

		client_socket_info.sock = client_sock[client_number];
		client_socket_info.addr = client_addr;

		if (pthread_create(&thread_id, NULL, http_relay, (void *)&client_socket_info) < 0)
		{
			fprintf(stderr, "ERROR : cannot create PTHREAD!\n\n");
			return -1;
		}

		pthread_detach(thread_id);

		client_number++;

	} // while(1) : accept

	return 0;
}


void dump(char* p, int len) {
	for(int i=0; i<len; i++) {
		printf("%02x ", *p);
		p++;
		if((i & 0x0f) == 0x0f)
			printf("\n");
	}
	printf("\n");
}

// return value : hostname_length
// char *host : hostname_offset
void get_host_name(char *packet, char *host)
{
	int len = strlen(packet);
	for(int i=0;i<len-5;i++)
	{
		if(memcmp(&packet[i], "Host: ", 6) == 0)
		{
			for(int j=0;j<len-i-7;j++)
			{
				if(memcmp(&packet[i+6+j], "\r\n", 2) == 0)
				{
					memcpy(host, &packet[i+6], j);
					memset((void *)(host+j), 0, 1);
					break;
				}
			}
		}
	}
}


void *http_relay(void *client_socket_information)
{
	int n, time = 0, client_len;
	int client_sockk = (*(sock_info *)client_socket_information).sock;
	struct sockaddr_in client_addr = (*(sock_info *)client_socket_information).addr;
	int server_sock, portnum;
	double real_time, result_time;
	struct sockaddr_in server_addr;
	struct hostent *server;
	int host_len;
	char buf[BUFSIZE];
	sock_info server_socket_info;

	printf("Connection from client!\n");

	while(1)
	{
		char *hostname = (char *)calloc(100, sizeof(char));
		/********************* receive packet from client *********************/
		memset(buf, 0, BUFSIZE);
		n = read(client_sockk, buf, BUFSIZE);
		if(n < 0)
		{
			perror("ERROR : Read from client ERROR!\n\n");
			close(server_sock);
			close(client_sockk);
			free(hostname);
			return;
		}

		/********************* hostname, port number *********************/
		get_host_name(buf, hostname);
		printf("%s\n", hostname);
		portnum = 80;

		/********************* creating [proxy --> server]socket *********************/
		server_sock = socket(AF_INET, SOCK_STREAM, 0);	//IPv4
		if (server_sock < 0)
		{
			perror("ERROR : Socket is not well created!\n");
			close(server_sock);
			close(client_sockk);
			free(hostname);
			return;
		}

		/********************* get host by name : DNS *********************/
		server = gethostbyname(hostname);
		if (server == NULL)
		{
			fprintf(stderr, "ERROR : There is no such a domain as %s\n\n", hostname);
			close(server_sock);
			close(client_sockk);
			free(hostname);
			return;
		}

		memset((char *)&server_addr, 0, sizeof(server_addr));
		server_addr.sin_family = AF_INET;
		bcopy((char *)server->h_addr, (char *)&server_addr.sin_addr.s_addr, server->h_length);
		server_addr.sin_port = htons(portnum);

		/********************* connection [proxy - server] *********************/
		if (connect(server_sock, &server_addr, sizeof(server_addr)) < 0)
		{
			printf("ERROR : Connection Error\n\n");
			close(server_sock);
			close(client_sockk);
			free(hostname);
			return;
		}

		/********************* proxy --> server (HTTP request) *********************/
		n = write(server_sock, buf, strlen(buf));
		if(n<0)
		{
			printf("ERROR : write proxy-->server HTTP Request\n\n");
			close(server_sock);
			close(client_sockk);
			free(hostname);
			return;
		}


		/********************* server --> proxy (HTTP response) *********************/
		n = read(server_sock, buf, BUFSIZE);

		printf("READ server --> proxy (HTTP response) DONE!\n\n");
		/********************* proxy --> client (HTTP response) *********************/
		n = write(client_sockk, buf, strlen(buf));
		if(n<0)
		{	
			perror("ERROR : write proxy-->client HTTP Request\n\n");
			close(server_sock);
			close(client_sockk);
			free(hostname);
			return;
		}

		printf("packet relay success!\n\n");
		close(server_sock);
		free(hostname);
	}
}

