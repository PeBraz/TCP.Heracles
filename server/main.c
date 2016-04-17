#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sys/types.h>
#include <sys/queue.h>
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

#define SERVER_BACKLOG 1




struct __server {	
	LIST_HEAD(porthead, port_t) head;
	int size;
};

static struct __server server = { .head = LIST_HEAD_INITIALIZER(), .size=0 };

struct __port {
	int number;
	int sock;
	int(*callback)(struct *client_data);
	
	LIST_ENTRY(port_t) ports;	
	LIST_HEAD(clienthead, client_t) head;	
};

struct __client {
	struct __port *port;
	pthread_t thr_id;
	int sock;
	struct sockaddr_in addr;
	
	LIST_ENTRY(client_t) clients;
};


void close_socket_at_signal(int n)
{
	__cleanup();
	exit(0);
}

void *client_thread(void * args) 
{
#define CLIENT_BUFFER_SIZE 2048

	struct __client * client = (struct __client*)args;
	
	struct sirtcp_data recv_data = {0};
	struct sirtcp_data send_data = {0};
	ssize_t bytes = recv(client->sock, recv_data->buffer, CLIENT_BUFFER_SIZE, 0);
	
	if (bytes == -1)  {
		fprintf(stderr, "Failed to read from client (%d).\n", errno); 
	}
	else if (bytes) {
		client->callback((struct Sirtcp_data){recv_data, send_data});
		if (send_data->size) {
			bytes = send(client->sock, send_data->buffer, send_data->size, 0);
			if (bytes == -1)
				fprintf(stderr, "Failed to read from client (%d).\n", errno); 		
		}
	}

	if (shutdown(client->sock, SHUT_RDWR) == -1) 
		fprintf(stderr, "Failed to drop connection nicely. (%d)\n", errno);	
	
	__cleanup_client(client);
	return NULL;
}


int sirtcp_endpoint(int port, int (*callback)(struct client_data*))
{
	int err;
	
	int sock = socket(AF_INET, SOCK_STREAM, 0);
	if (server.sock == -1) {
		fprintf(stderr, "Failed to create socket (%d).\n", errno);
		return 1;
	}

	
	char *cong_proto_name = NULL;
	if (cong_proto_name) {
		err = setsockopt(server.sock, IPPROTO_TCP, TCP_CONGESTION,
				CONG_PROTO_NAME, strlen(CONG_PROTO_NAME));
				
		if (err == -1) {
			fprintf(stderr, "Failed to change congestion protocol (%d).\n", errno);
			close(sock);
			return 1;
		}
	}

		
	struct sockaddr_in my_addr;
	memset(&my_addr, 0, sizeof(struct sockaddr_in));

	my_addr.sin_family = AF_INET;
	my_addr.sin_addr.s_addr = htonl(INADDR_ANY);
	my_addr.sin_port = htons(port);

	err = bind(server.sock, (struct sockaddr*)&my_addr,	sizeof(struct sockaddr_in));
	if (err == -1) {
		fprintf(stderr, "Failed to bind (%d).\n", errno);
		close(sock);
		return 1;
	}


	err = listen(server.sock, SERVER_BACKLOG);
	if (err == -1) {
		fprintf(stderr, "Failed to start listening (%d)\n", errno);
		close(sock);
		return 1;
	} 

	struct __port *p = malloc(sizeof(struct __port));
	*p = { .number=port, .sock=sock, .callback=callback};
	LIST_INIT(&p->head);
	
	LIST_INSERT_HEAD(&server.head, p, ports);
	
	server.size++;

	printf("Waiting on port %d...\n", port);

	return 0;
}

__client *__accept_client(struct __port *port) 
{
	socklen_t client_sock_len = sizeof(struct sockaddr_in);
	struct __client *client = malloc(sizeof(struct __client));
	client->sock = accept(port->sock, (struct sockaddr *)&client->addr, &client_sock_len);
	
	if (client->sock == -1) {
			fprintf(stderr, "Failed to create client socket (%d).\n", errno);
		free(client);	
			return NULL;
	}
	LIST_INSERT_HEAD(&port->head, client, clients);
	client->port = port;

	int err;
	err = pthread_create(&client->thr_id, NULL, client_thread, (void*)client);
	if (err) {
		fprintf(stderr, "Failed to create client thread(%d).\n", err);
		free(client);
		return NULL
	}
	
	err = pthread_detach(client->thr_id);
	if (err)
		fprintf(stderr, "Failed to detach thread (%d).\n", err);
			
	return client;
}

void __cleanup_client(struct __client *client) 
{
	LIST_REMOVE(client, clients);
	server.size--;
	close(client->sock);
	free(client)
}

void sirTCP_start() 
{
	
	sigaction(SIGINT, close_socket_at_signal);
	sigaction(SIGQUIT, close_socket_at_signal);
	sigaction(SIGABRT, close_socket_at_signal);
	
	struct pollfd *poll_events = malloc(sizeof(struct pollfd) * server.size);
	
	struct __port *port;
	LIST_FOREACH(port, server->head, ports) {
		* poll_events = {.fd=port.number, .events=POLLIN, .revents=0};
		poll_events++; 
	}

	socklen_t client_sock_len = sizeof(struct sockaddr_in);
	int new_conns;
	
	while (1) {
		
		new_conns = poll(&poll_events, server.size, 0);	
		if (new_conns == -1) {
			fprintf(stderr, "Polling for new connections returned an error (%d)\n", errno);
			break;
		}
		
		int i;
		for(i=0; i < server.size; i++) {
			if (poll_events[i].revents) {
				//meeh...
				LIST_FOREACH(port, server->head, ports) {
					if (port->sock == poll_events[i].fd) break;
				}
				__accept_client(port);
			}
		}
		sleep(1);
	}
	
	free(poll_events);
	__cleanup();

}

void __cleanup()
{
	struct __client *client;
	struct __port *port;	
	LIST_FOREACH (port, server->head, ports) {
		LIST_FOREACH(client, port->head, clients) {
			LIST_REMOVE(client, clients);
			free(client);
		}
		LIST_REMOVE(port, ports);
		free(port);
	}
	close(server.sock);
}


