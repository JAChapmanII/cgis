#include <stdio.h>
int main(int argc, char **argv) {
	printf("Content-Type: text/html\r\n");
	printf("Connection: close\r\n");
	printf("\r\n");

	printf("<html>"
		"<head>"
			"<title>cgis_scrit</title>"
		"</head>"
		"<body>"
			"<p>cgis_script test</p>"
			"<p>argc: %d</p>"
			"<p>binary: %s</p>"
			"<p>path: %s</p>"
		"</body>"
		"</html>", argc, argv[0], argv[1]);
	return 0;
}

