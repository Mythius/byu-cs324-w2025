// Replace PUT_USERID_HERE with your actual BYU CS user id, which you can find
// by running `id -u` on a CS lab machine.
#define USERID 1823742663
#define BUFSIZE 8

#include <unistd.h> 
#include <stdio.h>
#include <stdlib.h>
#include "sockhelper.h"
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <arpa/inet.h>

int verbose = 0;

void print_bytes(unsigned char *bytes, int byteslen);

int main(int argc, char *argv[]) {
	char* server = argv[1];
	char* port_char = argv[2];
	unsigned short level = atoi(argv[3]);
	unsigned short seed = atoi(argv[4]);
	unsigned char treasure[1024];
	int treasure_pointer = 0;

	// Prepare First Message
	unsigned int userid = htonl(USERID);
	unsigned char buf[8];
	unsigned short seed_network_order = htons(seed);
	
	bzero(buf,BUFSIZE);
	memcpy(&buf[2],&userid,sizeof(int)); // 4 bytes
	buf[1] = level;
	memcpy(&buf[6],&seed_network_order,sizeof(short)); // 2 bytes
	// print_bytes(buf,8);

	// DNS Lookup
	struct addrinfo hints;
	memset(&hints, 0, sizeof(struct addrinfo));
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_DGRAM;


	struct addrinfo *result;
	int s;
	s = getaddrinfo(server, port_char, &hints, &result);
	if (s != 0) {
		fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(s));
		exit(EXIT_FAILURE);
	}
	

	char remote_ip[INET6_ADDRSTRLEN];
	unsigned short remote_port;
	char local_ip[INET6_ADDRSTRLEN];
	unsigned short local_port;
	
	// Set Up Socket 
	struct sockaddr_storage remote_addr_ss;
	struct sockaddr *remote_addr = (struct sockaddr *)&remote_addr_ss;
	memcpy(remote_addr, result->ai_addr, sizeof(struct sockaddr_storage));
	parse_sockaddr(remote_addr, remote_ip, &remote_port);
	socklen_t addr_len = sizeof(struct sockaddr_storage);

	int sfd;
	if(verbose) printf("Created socket with values %d, %d, %d\n",result->ai_family,result->ai_socktype,0);
	sfd = socket(result->ai_family,result->ai_socktype,0);
	if(sfd < 0){
		fprintf(stderr,"Error creating socket\n");
		return 1;
	}

	addr_len = result->ai_addrlen;
	int addr_fam = result->ai_family;
	int sock_type = result->ai_socktype;

	freeaddrinfo(result);
	
	struct sockaddr_storage local_addr_ss;
	struct sockaddr *local_addr = (struct sockaddr *)&local_addr_ss;
	s = getsockname(sfd, local_addr, &addr_len);
	parse_sockaddr(local_addr, local_ip, &local_port);
	s = sendto(sfd,buf,sizeof(buf),0,remote_addr,addr_len);

	while(1){
		// printf("Sending Data\n");
		if(s < 0){
			fprintf(stderr,"Error sending data\n");
			return 1;
		}

		unsigned char data[256];
		// bzero(data,256);
		int bytes = recvfrom(sfd,data,sizeof(data),0,remote_addr,&addr_len);

		if(bytes < 1){
			fprintf(stderr,"No Data\n");
		}
		// print_bytes(remote_addr,addr_len);
		// if(verbose) printf("Bytes recieved: %d\n",bytes);
		if(verbose) print_bytes(data,bytes);


		// Parse data into variables
		unsigned char chunklen;
		unsigned char chunk[256];
		unsigned char opcode;
		unsigned short opparam;
		unsigned int nonce;

		chunklen = data[0];
		memcpy(&chunk,&data[1],chunklen);
		chunk[chunklen] = '\0';
		opcode = data[chunklen+1];
		memcpy(&opparam,&data[chunklen+2],2);
		memcpy(&nonce,&data[chunklen+4],4);

		opparam = ntohs(opparam);
		nonce = ntohl(nonce);
		nonce += 1;
		nonce = htonl(nonce);

		memcpy(&treasure[treasure_pointer],&chunk,chunklen);
		treasure_pointer += chunklen;
		
		if(verbose) {
			printf("\n%d\n", chunklen);
			printf("%s\n", chunk);
			printf("%x\n", opcode);
			printf("%d\n", opparam);
			printf("%x\n", nonce);
		}

		if(opcode == 0){
			// break;
		} else if(opcode == 1) {
			if(verbose) printf("Updating Remote Info: %x, %s, %d\n",addr_fam,remote_ip,opparam);
			populate_sockaddr(remote_addr,addr_fam,remote_ip,opparam);
			// print_bytes(remote_addr,addr_len);
			// break;
		} else if(opcode == 2){
			if(verbose) printf("Updating Local Port: %d\n",opparam);
			close(sfd);
			sfd = socket(addr_fam,sock_type,0);

			populate_sockaddr(local_addr,addr_fam,NULL,opparam);
			if(bind(sfd,local_addr,sizeof(struct sockaddr_storage)) < 0){
				perror("bind()");
				exit(1);
			}
		} else if(opcode == 3){
			struct sockaddr_storage ss;
			struct sockaddr *ss_addr = (struct sockaddr *)&ss;
			socklen_t ss_addr_len = sizeof(struct sockaddr_storage);
			unsigned char data[16];
			unsigned int sum = 0;
			char server_ip[INET6_ADDRSTRLEN];
			unsigned short random_port;

			if(verbose) printf("Recieving %d datagrams\n",opparam);
			for(int i=0;i<opparam;i++){
				recvfrom(sfd,data,sizeof(data),0,ss_addr,&ss_addr_len);
				parse_sockaddr(ss_addr,server_ip,&random_port);
				sum += random_port;
				if(verbose) printf("Recieved datagram from port :%d\n",random_port);
			}
			nonce = sum + 1;
			nonce = htonl(nonce);	
			if(verbose) printf("New nonce is %u\n",nonce);
			// exit(1);
		} else if(opcode == 4){
			
			if(verbose) printf("Switching ip address\n");
			close(sfd);



			struct addrinfo hints;
			memset(&hints, 0, sizeof(struct addrinfo));
			
			if(addr_fam == AF_INET){
				hints.ai_family = AF_INET6;
			} else {
				hints.ai_family = AF_INET;
			}
			hints.ai_socktype = SOCK_DGRAM;


			struct addrinfo *result;
			int s;
			s = getaddrinfo(server, port_char, &hints, &result);
			if (s != 0) {
				fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(s));
				exit(EXIT_FAILURE);
			}
	
			
			memcpy(remote_addr, result->ai_addr, sizeof(struct sockaddr_storage));
			parse_sockaddr(remote_addr, remote_ip, &remote_port);
			addr_len = sizeof(struct sockaddr_storage);

			sfd = socket(result->ai_family,result->ai_socktype,0);
			if(sfd < 0){
				fprintf(stderr,"Error creating socket\n");
				return 1;
			}

			addr_len = result->ai_addrlen;
			addr_fam = result->ai_family;
			sock_type = result->ai_socktype;

			
			populate_sockaddr(remote_addr,addr_fam,remote_ip,opparam);
		}

		if(chunklen == 0) break;

		memcpy(&buf,&nonce,4);
		if(verbose) print_bytes(buf,4);
		// break;

		
		s = sendto(sfd,buf,4,0,remote_addr,addr_len);
		// if(opcode == 3) exit(1);
	}

	treasure[treasure_pointer] = '\0';
	printf("%s\n",treasure);
	// fflush(1);

	// // printf("sent: %d\n",sent);

	// int received = recvfrom(sfd,data,sizeof(data),0,remote_addr,&addr_len);
	// // printf("received: %d\n",received);
	// print_bytes(data,received);
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
