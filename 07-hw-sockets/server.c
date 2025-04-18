#include <sys/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/socket.h>
#include <netdb.h>
#include <arpa/inet.h>

#include "sockhelper.h"

#define BUF_SIZE 500

int main(int argc, char *argv[]) {

	// Check usage
	if (!(argc == 2 || (argc == 3 &&
			(strcmp(argv[1], "-4") == 0 || strcmp(argv[1], "-6") == 0)))) {
		fprintf(stderr, "Usage: %s [ -4 | -6 ] port\n", argv[0]);
		exit(EXIT_FAILURE);
	}

	int portindex;
	if (argc == 2) {
		portindex = 1;
	} else {
		portindex = 2;
	}

	// Use IPv4 by default (or if -4 is specified);
	// If -6 is specified, then use IPv6 instead.
	int addr_fam;
	if (argc == 2 || strcmp(argv[1], "-4") == 0) {
		addr_fam = AF_INET;
	} else {
		addr_fam = AF_INET6;
	}

	unsigned short port = atoi(argv[portindex]);
	int sock_type = SOCK_STREAM;

	/* SECTION A - populate address structures */

	// Local address information is stored in local_addr_ss, which is of
	// type struct addr_storage.  However, all functions require a
	// parameter of type struct sockaddr *.  Instead of type-casting
	// everywhere, we declare local_addr, which is of type
	// struct sockaddr *, point it to the address of local_addr_ss, and use
	// local_addr everywhere.
	struct sockaddr_storage local_addr_ss;
	struct sockaddr *local_addr = (struct sockaddr *)&local_addr_ss;

	// Populate local_addr with the specified port and with default
	// address (indicated with NULL).  This is a little messy, so we use
	// the helper function parse_sockaddr(), which is defined in
	// ../code/sockhelper.c.
	populate_sockaddr(local_addr, addr_fam, NULL, port);


	/* SECTION B - set up socket */

	int sfd;
	if ((sfd = socket(addr_fam, sock_type, 0)) < 0) {
		perror("Error creating socket");
		exit(EXIT_FAILURE);
	}
	if (bind(sfd, local_addr, sizeof(struct sockaddr_storage)) < 0) {
		perror("Could not bind");
		exit(EXIT_FAILURE);
	}


	/* SECTION C - interact with clients; receive and send messages */

	listen(sfd,100);

	while(1){
		// Read datagrams and echo them back to sender

		struct sockaddr_storage local_addr_ss;
		struct sockaddr *local_addr = (struct sockaddr *)&local_addr_ss;
		socklen_t addr_len = sizeof(struct sockaddr_storage);

		int client = accept(sfd, local_addr, &addr_len);

		sleep(5);

		while (1) {
			char buf[BUF_SIZE];

			// Declare structures for remote address and port.
			// See notes above for local_addr_ss and local_addr_ss.
			struct sockaddr_storage remote_addr_ss;
			struct sockaddr *remote_addr = (struct sockaddr *)&remote_addr_ss;
			char remote_ip[INET6_ADDRSTRLEN];
			unsigned short remote_port;

			// printf("before recvfrom()\n"); fflush(stdout);
			// Read bytes from remote socket using recvfrom().  The remote
		// IP address and port from which the message was sent is
		// stored in the struct sockaddr_storage referenced by
		// remote_addr.
		//
		// NOTE: addr_len needs to be initialized before every call to
			// recvfrom().  See the man page for recvfrom().
			socklen_t addr_len = sizeof(struct sockaddr_storage);
			ssize_t nread = recv(client, buf, 1, 0);
			if(nread == 0){
				close(client);
				break;
			}
			
			if (nread < 0) {
				perror("receiving message");
				exit(EXIT_FAILURE);
			}


			// sleep(5);

			// printf("after recvfrom()\n"); fflush(stdout);
			// Extract the IP address and port from remote_addr using
			// parse_sockaddr().  parse_sockaddr() is defined in
			// ../code/sockhelper.c.
			parse_sockaddr(local_addr, remote_ip, &remote_port);
			printf("Received %zd bytes from %s:%d\n",
					nread, remote_ip, remote_port);

			if (send(client, buf, nread, 0) < 0) {
				perror("sending response");
			}
		}
	}
}
