#include <stdio.h>
#include "sockhelper.h"
#include <string.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <unistd.h>
#include <arpa/inet.h>
#include "sbuf.h"
#include <errno.h>
#include <fcntl.h>
#include <sys/epoll.h>

/* Recommended max object size */
#define MAX_OBJECT_SIZE 102400
#define MAXEVENTS 64
#define READ_REQUEST 1
#define SEND_REQUEST 2
#define READ_RESPONSE 3
#define SEND_RESPONSE 4

static const char *user_agent_hdr = "User-Agent: Mozilla/5.0 (Macintosh; Intel Mac OS X 10.15; rv:97.0) Gecko/20100101 Firefox/97.0";

int complete_request_received(char *);
void parse_request(char *, char *, char *, char *, char *);
void test_parser();
void print_bytes(unsigned char *, int);

struct request_info {
	int connfd;
	int servfd;
	int current_state;
	char buf[1024];
	char response_buf[16384];
	int cbytesread;
	int cbyteswritten;
	int cbytes_to_read;
	int sbytesread;
	int sbyteswritten;
	int sbytes_to_read;
};


void handle_client(int, struct request_info *);

int open_sfd( int port ){

	int sfd = socket(AF_INET,SOCK_STREAM,0);	
	if( sfd < 0 ){
		// fprintf(stderr,"Couldn't create socket\n");
		exit(1);
	}

	int optval = 1;
	setsockopt(sfd, SOL_SOCKET, SO_REUSEPORT, &optval, sizeof(optval));

    struct sockaddr_in local_addr;
	local_addr.sin_family = AF_INET;
	local_addr.sin_port = htons(port);
	local_addr.sin_addr.s_addr = htonl(INADDR_ANY);
	
	if(bind(sfd,(struct sockaddr *) &local_addr, sizeof(local_addr)) < 0){
		// fprintf(stderr,"Failed to bind\n");
	}

	if(listen(sfd,100) < 0){
		// fprintf(stderr,"Failed to Listen\n");
	}

	// printf("Listening on localhost port:%d\n",ntohs(local_addr.sin_port));

	return sfd;

}

void handle_new_clients(int efd,int sfd){
	while(1){
		struct sockaddr_storage remote_addr_ss;
		struct sockaddr *remote_addr = (struct sockaddr *)&remote_addr_ss;
		char remote_ip[INET6_ADDRSTRLEN];
		unsigned short remote_port;

		socklen_t addr_len = sizeof(struct sockaddr_storage);
		int connfd = accept(sfd, remote_addr, &addr_len);

		if (fcntl(sfd, F_SETFL, fcntl(sfd, F_GETFL, 0) | O_NONBLOCK) < 0) {
			fprintf(stderr, "error setting socket option\n");
			exit(1);
		}

		if (connfd < 0) {
			if (errno == EWOULDBLOCK ||
					errno == EAGAIN) {
				// no more clients ready to accept
				break;
			} else {
				perror("accept");
				exit(EXIT_FAILURE);
			}
		}

		parse_sockaddr(remote_addr, remote_ip, &remote_port);
		printf("Connection from %s:%d\n",
				remote_ip, remote_port);

		if (fcntl(connfd, F_SETFL, fcntl(connfd, F_GETFL, 0) | O_NONBLOCK) < 0) {
			fprintf(stderr, "error setting socket option\n");
			exit(1);
		}

		struct request_info *new_client =
			(struct request_info *)malloc(sizeof(struct request_info));
		new_client->connfd = connfd;
		new_client->cbytesread = 0;
		new_client->cbyteswritten = 0;
		new_client->cbytes_to_read = 0;
		new_client->current_state = READ_REQUEST;

		struct epoll_event event;
		event.data.ptr = new_client;
		event.events = EPOLLIN | EPOLLET;
		if (epoll_ctl(efd, EPOLL_CTL_ADD, connfd, &event) < 0) {
			fprintf(stderr, "error adding event\n");
			exit(1);
		}

		// handle_client(efd,new_client);
	}
}

void handle_client(int efd, struct request_info *client){
	printf("Handling Client with fd: %d\n",client->connfd);
	if(client->current_state == READ_REQUEST){
		while(!complete_request_received(client->buf)){
			int r = read(client->connfd,&client->buf[client->cbytesread],1024);
			if(r < 0){
				if(errno == EAGAIN || errno == EWOULDBLOCK){
					return;
				} else {
					printf("Error reading from Client\n");
					return;
				}
			}
			client->cbytesread += r;
			if(r == 0){
				printf("Client Disconnected\n");
				return;
			}
		}
		char method[16], hostname[64], port[8], path[64];

		if (complete_request_received(client->buf)) {
			printf("REQUEST COMPLETE\n");
			parse_request(client->buf, method, hostname, port, path);
			printf("METHOD: %s\n", method);
			printf("HOSTNAME: %s\n", hostname);
			printf("PORT: %s\n", port);
			printf("PATH: %s\n", path);
		} else {
			printf("REQUEST INCOMPLETE\n");
		}

		client->sbyteswritten = 0;
		client->sbyteswritten += sprintf(&client->buf[client->sbyteswritten],"%s ",method);
		
		// if( strcmp(port,"80") == 0){
		// 	client->sbyteswritten += sprintf(&client->buf[client->sbyteswritten],"%s",hostname);
		// } else {
		// 	client->sbyteswritten += sprintf(&client->buf[client->sbyteswritten],"%s:%s",hostname,port);
		// }

		client->sbyteswritten += sprintf(&client->buf[client->sbyteswritten],"%s HTTP/1.0\r\n",path);
		
		if( strcmp(port,"80") == 0){
			client->sbyteswritten += sprintf(&client->buf[client->sbyteswritten],"Host: %s\r\n",hostname);
		} else {
			client->sbyteswritten += sprintf(&client->buf[client->sbyteswritten],"Host: %s:%s\r\n",hostname,port);
		}

		client->sbyteswritten += sprintf(&client->buf[client->sbyteswritten],"%s\r\n",user_agent_hdr);
		client->sbyteswritten += sprintf(&client->buf[client->sbyteswritten],"%s\r\n","Connection: close");
		client->sbyteswritten += sprintf(&client->buf[client->sbyteswritten],"%s\r\n","Proxy-Connection: close");
		client->sbyteswritten += sprintf(&client->buf[client->sbyteswritten],"\r\n");

		print_bytes((unsigned char *) client->buf,client->sbyteswritten);

		// fprintf(stderr,"Looking Up Server\n");
		
		struct addrinfo hints;
		struct addrinfo *result, *rp;
		int s;
		
		memset(&hints, 0, sizeof(hints));
		hints.ai_family = AF_INET;    /* Allow IPv4 or IPv6 */
		hints.ai_socktype = SOCK_STREAM; /* Datagram socket */
		// hints.ai_flags = AI_PASSIVE;    /* For wildcard IP address */
		hints.ai_protocol = 0;          /* Any protocol */

		s = getaddrinfo(hostname, port, &hints, &result);
		if (s != 0) {
			fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(s));
			exit(EXIT_FAILURE);
		}

		for (rp = result; rp != NULL; rp = rp->ai_next) {
			client->servfd = socket(rp->ai_family, rp->ai_socktype,
					rp->ai_protocol);
			if (client->servfd == -1)
				continue;

			char ip_str[INET6_ADDRSTRLEN];
			void *addr;

			if (rp->ai_family == AF_INET) {
				struct sockaddr_in *ipv4 = (struct sockaddr_in *)rp->ai_addr;
				addr = &(ipv4->sin_addr);
			} else if (rp->ai_family == AF_INET6) {
				struct sockaddr_in6 *ipv6 = (struct sockaddr_in6 *)rp->ai_addr;
				addr = &(ipv6->sin6_addr);
			}
		
			// Convert the binary address to a IP address
			inet_ntop(rp->ai_family, addr, ip_str, sizeof(ip_str));
		
			// Print the hostname, port, and resolved IP
			// fprintf(stderr, "Resolved hostname: %s, port: %s, IP address: %s\n", hostname, port, ip_str);
		
		
				
			// if (bind(client->servfd, rp->ai_addr, rp->ai_addrlen) == 0){
				// fprintf(stderr, "Bound\n");
				break;  
			// }



			close(client->servfd);
		}

		
		struct epoll_event event;
		event.data.ptr = client;
		event.events = EPOLLIN | EPOLLET;

		if (epoll_ctl(efd, EPOLL_CTL_ADD, client->servfd, &event) < 0) {
			fprintf(stderr, "error adding event for server\n");
			exit(EXIT_FAILURE);
		}

		// SEND DATA TO SERVER
		if (rp == NULL) {
			fprintf(stderr, "Error: No valid address found for connection\n");
			freeaddrinfo(result);
			close(client->servfd);
			close(client->connfd);
			return;
		}

		if(connect(client->servfd, rp->ai_addr, rp->ai_addrlen) == -1){
			fprintf(stderr,"Error Connecting to Server\n");
			close(client->servfd);
			close(client->connfd);
			return;
		}

		
		if (fcntl(client->servfd, F_SETFL, fcntl(client->servfd, F_GETFL, 0) | O_NONBLOCK) < 0) {
			fprintf(stderr, "error setting socket option\n");
			exit(1);
		}

		if(send(client->servfd,client->buf,client->sbyteswritten,0) == -1){
			fprintf(stderr,"Error Connecting to Send Data\n");
			close(client->servfd);
			close(client->connfd);
			return;
		}

		client->current_state = READ_RESPONSE;
		client->sbytesread = 0;
		printf("Sent Data To Server\n");
	} else if(client->current_state == READ_RESPONSE){
		printf("Recieving Data from Server.\n");
		int n = -1;
		while(n != 0){
			n = read(client->servfd,&client->response_buf[client->sbytesread], 16384);
			if(n < 0){
				if(errno == EAGAIN || errno == EWOULDBLOCK){
					return;
				} else {
					printf("Error reading from server\n");
					return;
				}
			}
			client->sbytesread += n;
		}

		
		fprintf(stderr,"Recieved Data from Server\n");

		print_bytes((unsigned char *) client->response_buf,client->sbytesread);

		if(write(client->connfd,client->response_buf,client->sbytesread) == -1){
			fprintf(stderr,"Error writing response to client\n");
			close(client->servfd);
			close(client->connfd);
			return;
		}
		
		// fprintf(stderr,"Sent Server data to client\n");

		close(client->servfd);
		close(client->connfd);
	} else {
		printf("Undefined behavior\n");
	}
}


int main(int argc, char *argv[])
{
	// SETUP TCP SOCKET
	char *last_arg = argv[argc - 1];
    int port = atoi(last_arg); 
	int sfd = open_sfd(port);
	int efd;

	// CREATE EPOLL
	if ((efd = epoll_create1(0)) < 0) {
		perror("Error with epoll_create1");
		exit(EXIT_FAILURE);
	}

	// ADD TCP SOCKET TO EPOLL
	struct request_info *listener =
	malloc(sizeof(struct request_info));
	listener->servfd = sfd;

	struct epoll_event event;
	event.data.ptr = listener;
	event.events = EPOLLIN | EPOLLET;

	if (epoll_ctl(efd, EPOLL_CTL_ADD, sfd, &event) < 0) {
		fprintf(stderr, "error adding event\n");
		exit(EXIT_FAILURE);
	}
	
	struct epoll_event events[MAXEVENTS];
	while(1){
		printf("EPOLL Waiting\n");
		int n = epoll_wait(efd, events, MAXEVENTS, -1);
		printf("EPOLL Recieved\n");
		if(n < 0){
			fprintf(stderr,"ERROR with Epoll\n");
			return -1;
		}
		for(int i=0;i<n;i++){
			struct request_info *request = (struct request_info *)(events[i].data.ptr);

			if ((events[i].events & EPOLLERR) ||
				(events[i].events & EPOLLHUP) ||
				(events[i].events & EPOLLRDHUP)) {
				/* An error has occured on this fd */
				printf("Error occurred in main on %d\n",request->servfd);
				close(request->servfd);
				free(request);
				continue;
			}

			if(request->servfd == sfd){
				printf("Handling New Client\n");
				handle_new_clients(efd,sfd);
				printf("Finished Handling New Client\n");
			} else {
				printf("Handling existing Client\n");
				handle_client(efd,request);
				printf("finished Handling existing Client\n");
			}
		}
	}


	// test_parser();
	// printf("%s\n", user_agent_hdr);
	// return 0;
}

int complete_request_received(char *request) {
	return strstr(request,"\r\n\r\n") == NULL ? 0 : 1;
}

void parse_request(char *request, char *method, char *hostname, char *port, char *path) {
	// Extract the method
	char *first_space = strstr(request, " ");
	strncpy(method, request, first_space - request);
	method[first_space - request] = '\0';  // Null-terminate the method string

	// Extract the URL
	char *second_space = strstr(first_space + 1, " ");
	char url[1024];
	strncpy(url, first_space + 1, second_space - first_space - 1);
	url[second_space - first_space - 1] = '\0';

	// Extract the headers (if needed)
	// Here, we're only using the URL, so headers are ignored as per the lab

	// Parse the URL components
	char *host_start = strstr(url, "://");
	if (host_start) {
		host_start += 3;  // Skip past "://"
	} else {
		host_start = url;  // No "://", assume it's the hostname directly
	}

	char *port_start = strstr(host_start, ":");
	char *path_start = strstr(host_start, "/");

	if (port_start && (!path_start || port_start < path_start)) {
		// Port is specified
		strncpy(hostname, host_start, port_start - host_start);
		hostname[port_start - host_start] = '\0';

		strncpy(port, port_start + 1, path_start - port_start - 1);
		port[path_start - port_start - 1] = '\0';
	} else {
		// No port specified, default to 80
		if (path_start) {
			strncpy(hostname, host_start, path_start - host_start);
			hostname[path_start - host_start] = '\0';
		} else {
			strcpy(hostname, host_start);
		}
		strcpy(port, "80");
	}

	if (path_start) {
		strcpy(path, path_start);
	} else {
		strcpy(path, "/");  // Default path if not specified
	}
}

void test_parser() {
	int i;
	char method[16], hostname[64], port[8], path[64];

       	char *reqs[] = {
		"GET http://www.example.com/index.html HTTP/1.0\r\n"
		"Host: www.example.com\r\n"
		"User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:68.0) Gecko/20100101 Firefox/68.0\r\n"
		"Accept-Language: en-US,en;q=0.5\r\n\r\n",

		"GET http://www.example.com:8080/index.html?foo=1&bar=2 HTTP/1.0\r\n"
		"Host: www.example.com:8080\r\n"
		"User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:68.0) Gecko/20100101 Firefox/68.0\r\n"
		"Accept-Language: en-US,en;q=0.5\r\n\r\n",

		"GET http://localhost:1234/home.html HTTP/1.0\r\n"
		"Host: localhost:1234\r\n"
		"User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:68.0) Gecko/20100101 Firefox/68.0\r\n"
		"Accept-Language: en-US,en;q=0.5\r\n\r\n",

		"GET http://www.example.com:8080/index.html HTTP/1.0\r\n",

		NULL
	};
	
	for (i = 0; reqs[i] != NULL; i++) {
		printf("Testing %s\n", reqs[i]);
		if (complete_request_received(reqs[i])) {
			printf("REQUEST COMPLETE\n");
			parse_request(reqs[i], method, hostname, port, path);
			printf("METHOD: %s\n", method);
			printf("HOSTNAME: %s\n", hostname);
			printf("PORT: %s\n", port);
			printf("PATH: %s\n", path);
		} else {
			printf("REQUEST INCOMPLETE\n");
		}
	}
}

void print_bytes(unsigned char *bytes, int byteslen) {
	int i, j, byteslen_adjusted;

	if (byteslen % 8) {
		byteslen_adjusted = ((byteslen / 8) + 1) * 8;
	} else {
		byteslen_adjusted = byteslen;
	}
	for (i = 0; i < byteslen_adjusted + 1; i++) {
		if (!(i % 8)) {
			if (i > 0) {
				for (j = i - 8; j < i; j++) {
					if (j >= byteslen_adjusted) {
						printf("  ");
					} else if (j >= byteslen) {
						printf("  ");
					} else if (bytes[j] >= '!' && bytes[j] <= '~') {
						printf(" %c", bytes[j]);
					} else {
						printf(" .");
					}
				}
			}
			if (i < byteslen_adjusted) {
				printf("\n%02X: ", i);
			}
		} else if (!(i % 4)) {
			printf(" ");
		}
		if (i >= byteslen_adjusted) {
			continue;
		} else if (i >= byteslen) {
			printf("   ");
		} else {
			printf("%02X ", bytes[i]);
		}
	}
	printf("\n");
	fflush(stdout);
}
