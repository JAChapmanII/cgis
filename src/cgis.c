#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <arpa/inet.h>
#include <sys/stat.h>
#include <unistd.h>
#include <signal.h>
#include <time.h>

#define PROTOCOL "HTTP/1.0"
#define RFC1123FMT "%a, %d %b %Y %H:%M:%S GMT"

#define SERVER_NAME "cgis"
#define SERVER_URL "localhost:13120"

#define BUFFER_SIZE (1024 * 16)

#define BINARY "cgis_script"

static int backLog = 8;
static short port = 13120;

static int socketFD = -1;
static int clientFD = -1;

void sigintHandler(int signal) { // {{{
	if(clientFD != -1)
		close(clientFD);
	if(socketFD != -1)
		close(socketFD);
	fprintf(stderr, "C-c handled\n");
	_exit(0);
} // }}}

char *mimeType(const char *path);
void dumpHeader(FILE *file, int status, const char *title);
void errorPage(FILE *file, int status, const char *title, const char *text);
int serveStatic(FILE *file, const char *path, struct stat sb);

int main(int argc, char **argv) {
	signal(SIGINT, sigintHandler);

	socketFD = socket(AF_INET, SOCK_STREAM, 0);
	if(socketFD == -1) {
		perror("cgis: unable to create socket");
		return -1;
	}
	
	struct sockaddr_in sAddress;
	memset(&sAddress, '\0', sizeof(sAddress));
	sAddress.sin_family = AF_INET;
	sAddress.sin_addr.s_addr = INADDR_ANY;
	sAddress.sin_port = htons(port);
	if(bind(socketFD, (struct sockaddr *)&sAddress, sizeof(sAddress)) < 0) {
		perror("cgis: unable to bind socket");
		return -2;
	}

	if(listen(socketFD, backLog) == -1) {
		perror("cgis: unable to listen on socket");
		return -3;
	}

	while(true) {
		struct sockaddr_in cAddress;
		socklen_t csize = sizeof(cAddress);
		memset(&cAddress, '\0', sizeof(cAddress));

		int clientFD = accept(socketFD, (struct sockaddr *)&cAddress, &csize);
		if(clientFD == -1) {
			perror("cgis: unable to accept on socket");
			return -4;
		}
		FILE *in = fdopen(clientFD, "r"), *out = fdopen(clientFD, "w");
		if(!in || !out) {
			perror("cgis: unable to fdopen socket");
			return -5;
		}

		char buffer[BUFFER_SIZE] = { 0 };
		if(fgets(buffer, BUFFER_SIZE, in) != buffer) {
			errorPage(out, 400, "Bad Request", "No request found.");
			// TODO: this does close fdopen'd FILE *s, right?
			close(clientFD);
			continue;
		}

		char method[BUFFER_SIZE] = { 0 }, path[BUFFER_SIZE] = { 0 },
			protocol[BUFFER_SIZE] = { 0 };
		// parse main request line
		if(sscanf(buffer, "%[^ ] %[^ ] %[^ ]", method, path + 1, protocol) != 3) {
			errorPage(out, 400, "Bad Request", "Can't parse request.");
			// TODO: this does close fdopen'd FILE *s, right?
			close(clientFD);
			continue;
		}
		path[0] = '.';
		printf("Request for: %s\n", path);

		struct stat sb;
		bool statable = (stat(path, &sb) == 0);

		// if the request isn't for the binary and is a valid file
		if(strcmp(path, BINARY) && statable && sb.st_mtime && !access(path, R_OK)) {
			// skip other headers
			while(fgets(buffer, BUFFER_SIZE, in) == buffer) {
				if(strcmp(buffer, "\n") == 0 || strcmp(buffer, "\r\n") == 0)
					break;
			}
			if(serveStatic(out, path, sb) != 0) {
				close(socketFD);
				return -7;
			}
		}

		// dump other headers
		while(fgets(buffer, BUFFER_SIZE, in) == buffer) {
			if(strcmp(buffer, "\n") == 0 || strcmp(buffer, "\r\n") == 0)
				break;
			printf("header: %s", buffer);
		}

		errorPage(out, 333, "Don't know", "don't care");
		close(clientFD);
	}

	close(socketFD);
	return 0;
}

char *mimeType(const char *path) { // {{{
	char* suffix = strrchr(path, '.');
	if(suffix == NULL)
		return "text/plain; charset=utf-8";
	if(strcmp(suffix, ".html") == 0)
		return "text/html; charset=utf-8";
	if(strcmp(suffix, ".jpg") == 0 || strcmp(suffix, ".jpeg") == 0)
		return "image/jpeg";
	if(strcmp(suffix, ".png") == 0)
		return "image/png";
	if(strcmp(suffix, ".css") == 0)
		return "text/css";
	return "text/plain; charset=utf-8";
} // }}}

void dumpHeader(FILE *file, int status, const char *title) { // {{{
	time_t now = time(NULL);
	char timebuf[100] = { 0 };
	strftime(timebuf, sizeof(timebuf), RFC1123FMT, gmtime(&now));

	fprintf(file, "%s %d %s\r\n", PROTOCOL, status, title);
	fprintf(file, "Server: %s\r\n", SERVER_NAME);
	fprintf(file, "Date: %s\r\n", timebuf);
} // }}}

void errorPage(FILE *file, int status, const char *title, const char *text) { // {{{
	dumpHeader(file, status, title);
	fprintf(file, "Content-Type: text/html\r\n");
	fprintf(file, "Connection: close\r\n");
	fprintf(file, "\r\n");

	fprintf(file, "<html><head><title>%d %s</title></head>\n"
			"<body><h4>%d %s</h4>\n", status, title, status, title);
	fprintf(file, "%s\n", text);
	fprintf(file, "<hr>\n<address><a href=\"%s\">%s</a></address>\n"
			"</body></html>\n", SERVER_URL, SERVER_NAME);
	fflush(file);
} // }}}

int serveStatic(FILE *file, const char *path, struct stat sb) { // {{{
	FILE *sf = fopen(path, "r");
	if(sf == NULL) {
		errorPage(file, 402, "Forbidden", "File is protected.");
		fprintf(stderr, "cgis: unable to serve-static: file is protected\n");
		return 1;
	}

	char timebuf[100] = { 0 };
	strftime(timebuf, sizeof(timebuf), RFC1123FMT, gmtime(&sb.st_mtime));

	dumpHeader(file, 200, path);
	fprintf(file, "Content-Type: %s\r\n", mimeType(path));
	if(sb.st_size >= 0)
		fprintf(file, "Content-Length: %ld\r\n", (int64_t)sb.st_size);
	fprintf(file, "Last-Modified: %s\r\n", timebuf);
	fprintf(file, "Connection: close\r\n");
	fprintf(file, "\r\n");

	char *buffer[BUFFER_SIZE] = { 0 };
	while(true) {
		size_t rcount = fread(buffer, 1, BUFFER_SIZE, sf);
		if((rcount == 0) && feof(sf))
			break;
		if(rcount == 0) {
			fprintf(stderr, "cgis: error reading static file\n");
			fclose(sf);
			return 2;
		}
		size_t wcount = fwrite(buffer, 1, rcount, file);
		if(wcount != rcount) {
			fprintf(stderr, "cgis: error writing static file (%lu != %lu)\n",
					wcount, rcount);
			fclose(sf);
			return 3;
		}
	}
	fclose(sf);
	fflush(file);
	return 0;
} // }}}

