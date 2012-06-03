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

#define BINARY "./cgis_script"

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

void cleanPath(char *path);
char *mimeType(const char *path);
void dumpHeader(FILE *file, int status, const char *title);
void errorPage(FILE *file, int status, const char *title, const char *text);
int serveStatic(FILE *file, const char *path, struct stat sb);
void handleRequest(int outFD, char *path, char *queryString);

int main(int argc, char **argv) {
	signal(SIGINT, sigintHandler);

	bool hasBinary = (access(BINARY, R_OK | X_OK | F_OK) == 0);
	// if the request isn't for the binary and is a valid file
	if(!hasBinary)
		fprintf(stderr, "cgis: note: binary doesn't exist/not executable\n");

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
		// NOTE: we use path + 1 so we can easily prepend a period
		if(sscanf(buffer, "%[^ ] %[^ ] %[^ ]", method, path + 1, protocol) != 3) {
			errorPage(out, 400, "Bad Request", "Can't parse request.");
			// TODO: this does close fdopen'd FILE *s, right?
			close(clientFD);
			continue;
		}

		// prepend the string with a period for local dir
		path[0] = '.';
		// figure out if there is a query string and where it is
		char *queryString = strchrnul(path, '?');
		// if there is one, separate it from the url
		if(*queryString)
			*(queryString++) = '\0';
		// strip out bad things like path/../file
		cleanPath(path);

		printf("\nRequest for: %s\n", path);
		printf("Query string: %s\n", queryString);


		struct stat sb;
		bool statable = (stat(path, &sb) == 0);

		// if the request isn't for the binary and is a valid file
		if(strcmp(path, BINARY) && strcmp(path, ".") &&
				statable && sb.st_mtime && !access(path, R_OK)) {
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

		// if there is a binary to handle other requests with, use it
		if(hasBinary) {
			handleRequest(clientFD, path, queryString);
			clientFD = -1;
		}
	}

	close(socketFD);
	return 0;
}

void cleanPath(char *path) { // {{{
	char buffer[BUFFER_SIZE] = { 0 };
	char *save, *tok = strtok_r(path, "/", &save);
	for(; tok; tok = strtok_r(NULL, "/", &save)) {
		if(tok[0] == '.' && tok[1] == '.')
			continue;
		// TODO: we really should have bounds checks even with 16k for space
		strcat(buffer, tok);
		strcat(buffer, "/");
	}
	size_t len = strlen(buffer);
	strncpy(path, buffer, len - 1);
} // }}}

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

void handleRequest(int outFD, char *path, char *queryString) { // {{{
	FILE *file = fdopen(outFD, "w");

	int sp_pipe[2] = { 0 };
	if(pipe(sp_pipe) != 0) {
		fprintf(stderr, "handleRequest: failed to create sp_pipe pipe\n");
		errorPage(file, 500, "Internal Server Error", "Left sp_pipe failed\n");
		return;
	}

	pid_t pid = fork();
	if(pid == -1) {
		close(sp_pipe[0]);
		close(sp_pipe[1]);
		fprintf(stderr, "handleRequest: failed to fork\n");
		errorPage(file, 500, "Internal Server Error", "Failed to fork\n");
		return;
	}

	// dump header for subprocess
	dumpHeader(file, 400, path);

	// if we're the child process, exec the binary
	if(pid == 0) {
		// copy read end of pipe onto stdin
		dup2(sp_pipe[0], 0);
		// copy the client socket fd onto our stdout
		dup2(outFD, 1);

		// close unused write end
		close(sp_pipe[1]);

		// setup arguments and execute binary
		char *argv[4] = { BINARY, path, queryString, NULL };
		execv(BINARY, argv);

		// we only get here in catastrophic failure
		fprintf(stderr, "handleRequest: OH MAN execv FAILED\n");
		errorPage(file, 500, "Internal Server Error", "Failed to execv\n");
		return;
	}

	// we're still in cgis, close read end of pipe
	close(sp_pipe[0]);

	// TODO: POST data?

	fclose(file);
} // }}}

