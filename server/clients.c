#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <pthread.h>

#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>

struct server{
	char *ip;
	int port;
};

void *client_thread(void *args)
{
	struct server *s = (struct server*)args;
	int sock = socket(AF_INET, SOCK_STREAM, 0);


	struct sockaddr_in my_addr;
	memset(&my_addr, 0, sizeof(struct sockaddr_in));

	inet_aton(s->ip, &my_addr.sin_addr);
	my_addr.sin_family = AF_INET;
	my_addr.sin_port = htons(s->port);
	
	int err = connect(sock, (struct sockaddr*)&my_addr, sizeof(struct sockaddr_in));
	if (err == -1) {
		fprintf(stderr, "Failed to connect to server (%d).\n", errno);
		free(s);
		return NULL;
	}

	char buffer[1024];
	ssize_t r_bytes;
	do {
		r_bytes = recv(sock, buffer, 1024, 0);
		if (r_bytes == -1) {
			fprintf(stderr, "Failed to receive (%d).\n", errno);
		}
	} while(r_bytes > 0);
	free(s);
	return NULL;
}

pthread_t new_client(char *ip, int port) 
{
	pthread_t thr_id;
	struct server *s = malloc(sizeof(struct server));
	*s = (struct server){ip, port};

	pthread_create(&thr_id, NULL, client_thread, s);
	return thr_id;

}
//usage: clients <port> <number-of-clients>
int main(int argc, char *argv[]) {

	int port = 8000;
	int clients = 1;
	pthread_t client_buff[10];

	switch (argc)  {
	case 3: 
		clients = atol(argv[2]);
		if (clients > 10) {
			fprintf(stderr, "Too many clients to work, max is 10\n");
			exit(1);
		}
	case 2:
		port = atol(argv[1]);
	}

	char *inet_addr = "11.0.0.1";
	/*if (argc > 1)
		inet_addr = argv[1];*/
	int i;
	for (i=0; i<clients;i++ ) {
		client_buff[i] = new_client(inet_addr, port);
	}	
	for (i=0; i<clients;i++) {
		pthread_join(client_buff[i], NULL);
	}
	return 0;
}
