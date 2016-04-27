#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>

#include <signal.h>
#include <assert.h>
#include <pthread.h>
#include <errno.h>
#include <poll.h>
#include <unistd.h>
#include <time.h>

#define SERVER_BACKLOG 10
#define DEFAULT_PORT 8000

#define PORT_FLAG "--port"
#define CONG_PROTO_FLAG "--cong"
#define UPLOAD_FILE_FLAG "--upload"

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

	char *filename;
};


void close_socket_at_signal(int n)
{
	puts("\nInterrupt: closing socket");
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
#define CLIENT_BUFFER_SIZE 1024

	struct client_conn * client = (struct client_conn*)args;
	printf("Client %u started.\n", client->id);


	char buffer[CLIENT_BUFFER_SIZE];
	FILE * f = fopen(client->filename, "r");
	int bytes_red;	
	int total = 0;
	do {
		bytes_red = fread(buffer, 1, CLIENT_BUFFER_SIZE, f);
		if (ferror(f)) break;
		total += bytes_red; 
		printf("%dB\n", total);

		//sleep(1);//usleep(100000)
		send(client->sock, buffer, bytes_red, 0);
	}while (bytes_red == CLIENT_BUFFER_SIZE);
	fclose(f);

	if (shutdown(client->sock, SHUT_RDWR) == -1) 
		fprintf(stderr, "Failed to drop connection nicely. (%d)\n", errno);	
	
	server.clients[client->id] = 0;
	server.client_count--;
	free(client);	
	return NULL;
}

int main(int argc, char *argv[]) 
{
	int i;
	char *cong_proto_name = NULL;
	char *upload_filename = NULL;
	int port = DEFAULT_PORT;

	for (i=1; i < argc; i+=2) {
		if (!strcmp(CONG_PROTO_FLAG, argv[i])) {
			if (i + 1 >= argc) goto cmdl_cleanup;
			cong_proto_name = strdup(argv[i + 1]);
		} else if (!strcmp(UPLOAD_FILE_FLAG, argv[i])) {
			if (i + 1 >= argc) goto cmdl_cleanup;
			upload_filename = strdup(argv[i+1]);
		} else if (!strcmp(PORT_FLAG, argv[i])) {
			if (i + 1 >= argc) goto cmdl_cleanup;
			port = atol(argv[i+1]);
		}
		else {
			puts("usage: ./server [--upload <filename>]");
			puts("	--cong <tcp-congestion-name> ");
			puts("	--port <number> ");
			goto cmdl_cleanup;
		}
	}

	if (!upload_filename) {
		puts("\"--upload\" required");
		goto cmdl_cleanup; 
	}
	int err;

	server.sock = socket(AF_INET, SOCK_STREAM, 0);
	if (server.sock == -1) {
		fprintf(stderr, "Failed to create socket (%d)\n", errno);
		goto cmdl_cleanup;
	}
	
	signal(SIGINT, close_socket_at_signal); //terminal stop
		
	//Change congestion protocol
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
	my_addr.sin_port = htons(port);


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

	printf("Waiting on port %d...\n", port);

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
			client->filename = upload_filename;
			server.client_count++;

			err = pthread_create(&client->thr_id, NULL, client_thread, (void*)client);
			if (err != 0) {
				fprintf(stderr, "Failure to create client thread(%d).\n", err);
				server.client_count--;
				server.clients[j] = 0;
				free(client);
			}
			err = pthread_detach(client->thr_id);
			if (err != 0)
				fprintf(stderr, "Failed to detach thread, memory leak imminent(%d).\n", err);
		}
		sleep(1);
	}

cleanup:
	close(server.sock);
cmdl_cleanup:
	if (upload_filename) free(upload_filename);
	if (cong_proto_name) free(cong_proto_name);
	return 0;
}
