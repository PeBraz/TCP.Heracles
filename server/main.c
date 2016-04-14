#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
//#include <netdb.h>
#include <signal.h>
#include <assert.h>
#include <pthread.h>
#include <errno.h>
#include <poll.h>
#include <unistd.h>
#include <time.h>

#define SERVER_BACKLOG 10
#define PORT 8000


static struct _server {
	int sock;
	struct client_conn *clients[SERVER_BACKLOG];
	int client_count;
} server;

struct client_conn {
	int id;
	pthread_t thr_id;
	int sock;
	struct sockaddr_in addr;
};


void close_socket_at_signal(int n)
{
	close(server.sock);
	
	int i;
	for (i=0; i<SERVER_BACKLOG; i++)
		if (server.clients[i])
			free(server.clients[i]);
	//what about threads?
	exit(0);
}

void *client_thread(void * args) 
{
	struct client_conn * client = (struct client_conn*)args;
	printf("Client %u started.\n", client->id);
	//do nothing for now
	
	if (shutdown(client->sock, SHUT_RDWR) == -1) 
		fprintf(stderr, "Failed to drop connection nicely. (%d)\n", errno);	
	
	server.clients[client->id] = 0;
	server.client_count--;
	free(client);
	return NULL;
}

int main(int argc, char *argv[]) 
{

	int err;

	server.sock = socket(AF_INET, SOCK_STREAM, 0);
	if (server.sock == -1) {
		fprintf(stderr, "Failed to create socket (%d)\n", errno);
		return 1;
	}
	
	signal(SIGINT, close_socket_at_signal); //terminal stop
		
	//Change congestion protocol
	char * cong_proto_name = NULL; //"reno2";
	if (cong_proto_name){
		err = setsockopt(server.sock, IPPROTO_TCP, TCP_CONGESTION,
			cong_proto_name, strlen(cong_proto_name));
		if (err == -1) {
			fprintf(stderr, "Failed to change congestion protocol (%d)\n", errno);
			goto cleanup;
		}
	}

	struct sockaddr_in my_addr;
	memset(&my_addr, 0, sizeof(struct sockaddr_in));

	my_addr.sin_family = AF_INET;
	my_addr.sin_addr.s_addr = htonl(INADDR_ANY);
	my_addr.sin_port = htons(PORT);


	err = bind(server.sock, (struct sockaddr*)&my_addr,
		       	sizeof(struct sockaddr_in));
	if (err == -1) {
		fprintf(stderr, "Failed to bind (%d)\n", errno);
		goto cleanup;
	}

	err = listen(server.sock, SERVER_BACKLOG);
	if (err == -1) {
		fprintf(stderr, "Failed to start listening (%d)\n", errno);
		goto cleanup;
	} 

	//final server initializations	
	server.client_count = 0;
	memset(server.clients, 0, 10 * sizeof(struct client_conn*));

	printf("Waiting on port %d...\n", PORT);

	int new_conns;
	struct pollfd poll_event = {.fd=server.sock, .events=POLLIN}; 
	socklen_t client_sock_len = sizeof(struct sockaddr_in);
	while (1) {
		new_conns = poll(&poll_event, 1, 0);	
		if (new_conns == -1) {
			fprintf(stderr, "Polling for new connections returned an error (%d)\n", errno);
			break;
		}
		//add new connections
		int i;
		for (i=0; i < new_conns; i++) {
			struct client_conn *client = 
				malloc(sizeof(struct client_conn));
			client->sock = accept(server.sock, (struct sockaddr *)&client->addr, &client_sock_len);
			if (client->sock == -1) {
				puts("failed to create client socket");
				free(client);	
				continue;
			}
			//a scalable alternative would use a stack/queue
			int j=0;
			while (j<SERVER_BACKLOG && server.clients[j]) j++;
			assert(j != SERVER_BACKLOG);
				 
			server.clients[j] = client;
			client->id = j;
			server.client_count++;

			err = pthread_create(&client->thr_id, NULL, client_thread, (void*)client);
			if (err != 0) {
				puts("Failure to create client thread");
				server.client_count--;
				server.clients[j] = 0;
				free(client);
			}
		}
		sleep(1);
	}

cleanup:
	close(server.sock);
	return 0;
}
