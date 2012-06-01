#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <arpa/inet.h>
#include <unistd.h>

static int backLog = 8;
static short port = 13120;

int main(int argc, char **argv) {
	int sockfd = socket(AF_INET, SOCK_STREAM, 0);
	if(sockfd == -1) {
		perror("cgis: unable to create socket");
		return -1;
	}
	
	struct sockaddr_in sAddress;
	memset(&sAddress, '\0', sizeof(sAddress));
	sAddress.sin_family = AF_INET;
	sAddress.sin_addr.s_addr = INADDR_ANY;
	sAddress.sin_port = htons(port);
	if(bind(sockfd, (struct sockaddr *)&sAddress, sizeof(sAddress)) < 0) {
		perror("cgis: unable to bind socket");
		return -2;
	}

	if(listen(sockfd, backLog) == -1) {
		perror("cgis: unable to listen on socket");
		return -3;
	}

	while(true) {
		struct sockaddr_in cAddress;
		socklen_t csize = sizeof(cAddress);
		memset(&cAddress, '\0', sizeof(cAddress));

		int csockfd = accept(sockfd, (struct sockaddr *)&cAddress, &csize);
		if(csockfd == -1) {
			perror("cgis: unable to accept on socket");
			return -4;
		}

		while(true) {
			char buffer[1024 * 16] = { 0 };
			ssize_t rcount = read(csockfd, buffer, 1024 * 16);
			if(rcount == 0)
				break;
			if(rcount < 0) {
				perror("cgis: read failed on socket");
				return -5;
			}
			printf("%.*s", (int)rcount, buffer);
		}
		close(csockfd);
	}

	close(sockfd);
	return 0;
}
