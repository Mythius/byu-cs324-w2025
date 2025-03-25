#include <stdio.h>
#include <pthread.h>
#include "sockhelper.h"
#include <string.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <unistd.h>
#include <arpa/inet.h>
#include "sbuf.h"


/* Recommended max object size */
#define MAX_OBJECT_SIZE 102400
#define SBUFSIZE  5
#define NTHREADS  8

static const char *user_agent_hdr = "User-Agent: Mozilla/5.0 (Macintosh; Intel Mac OS X 10.15; rv:97.0) Gecko/20100101 Firefox/97.0";

int complete_request_received(char *);
void parse_request(char *, char *, char *, char *, char *);
void test_parser();
void print_bytes(unsigned char *, int);
void* handle_client(void*);
int open_sfd( int );

sbuf_t sbuf;

int main(int argc, char *argv[])
{

	char *last_arg = argv[argc - 1];
    int port = atoi(last_arg); 

	sbuf_init(&sbuf,SBUFSIZE);
	pthread_t tid;
	for (int i = 0; i < NTHREADS; i++) {
		pthread_create(&tid, NULL, handle_client, NULL);
	}

	int sfd = open_sfd(port);
	while(1){
		struct sockaddr_storage local_addr_ss;
		struct sockaddr *local_addr = (struct sockaddr *)&local_addr_ss;
		socklen_t addr_len = sizeof(struct sockaddr_storage);
		int client = accept(sfd, local_addr, &addr_len);        
		sbuf_insert(&sbuf,client);
	}
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

void* handle_client(void *arg){
	pthread_detach(pthread_self());
	while(1){
	
		int client = sbuf_remove(&sbuf);
		
		char buf[1024];
		int writeto = 0;
		while(!complete_request_received(buf)){
			int r = read(client,&buf[writeto],1024);
			writeto += r;
			if(r == 0){
				printf("Client Disconnected\n");
				return;
			}
		}
		
		// print_bytes((unsigned char *)buf,strlen(buf));

		
		char method[16], hostname[64], port[8], path[64];

		
		if (complete_request_received(buf)) {
			// printf("REQUEST COMPLETE\n");
			parse_request(buf, method, hostname, port, path);
			// printf("METHOD: %s\n", method);
			// printf("HOSTNAME: %s\n", hostname);
			// printf("PORT: %s\n", port);
			// printf("PATH: %s\n", path);
		} else {
			// printf("REQUEST INCOMPLETE\n");
		}

		char newheaders[1024];
		writeto = 0;
		writeto += sprintf(&newheaders[writeto],"%s ",method);
		
		// if( strcmp(port,"80") == 0){
		// 	writeto += sprintf(&newheaders[writeto],"%s",hostname);
		// } else {
		// 	writeto += sprintf(&newheaders[writeto],"%s:%s",hostname,port);
		// }

		writeto += sprintf(&newheaders[writeto],"%s HTTP/1.0\r\n",path);
		
		if( strcmp(port,"80") == 0){
			writeto += sprintf(&newheaders[writeto],"Host: %s\r\n",hostname);
		} else {
			writeto += sprintf(&newheaders[writeto],"Host: %s:%s\r\n",hostname,port);
		}

		writeto += sprintf(&newheaders[writeto],"%s\r\n",user_agent_hdr);
		writeto += sprintf(&newheaders[writeto],"%s\r\n","Connection: close");
		writeto += sprintf(&newheaders[writeto],"%s\r\n","Proxy-Connection: close");
		writeto += sprintf(&newheaders[writeto],"\r\n");

		print_bytes((unsigned char *) newheaders,writeto);

		// fprintf(stderr,"Looking Up Server\n");
		
		struct addrinfo hints;
		struct addrinfo *result, *rp;
		int sfd, s;
		
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
			sfd = socket(rp->ai_family, rp->ai_socktype,
					rp->ai_protocol);
			if (sfd == -1)
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
		
		
				
			// if (bind(sfd, rp->ai_addr, rp->ai_addrlen) == 0){
				// fprintf(stderr, "Bound\n");
				break;  
			// }



			close(sfd);
		}

		if (rp == NULL) {
			fprintf(stderr, "Error: No valid address found for connection\n");
			freeaddrinfo(result);
			close(sfd);
			close(client);
			continue;
		}

		if(connect(sfd, rp->ai_addr, rp->ai_addrlen) == -1){
			fprintf(stderr,"Error Connecting to Server\n");
			close(sfd);
			close(client);
			continue;
		}

		
		if(send(sfd,newheaders,writeto,0) == -1){
			fprintf(stderr,"Error Connecting to Send Data\n");
			close(sfd);
			close(client);
			continue;
		}
		
		// fprintf(stderr,"Sent Data to Server\n");
		
		char response_buf[16384];
		writeto = 0;

		int n;
		n = read(sfd,&response_buf[writeto], 16384);
		writeto += n;
		while(n != 0){
			n = read(sfd,&response_buf[writeto], 16384);
			writeto += n;
		}

		
		// fprintf(stderr,"Recieved Data from Server\n");

		// print_bytes((unsigned char *) response_buf,writeto);

		if(write(client,response_buf,writeto) == -1){
			fprintf(stderr,"Error writing response to client\n");
			close(sfd);
			close(client);
			continue;
		}
		
		// fprintf(stderr,"Sent Server data to client\n");


		freeaddrinfo(result);
		close(sfd);
		close(client);
			
	}
}