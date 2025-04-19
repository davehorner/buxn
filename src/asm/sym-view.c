#include <stdio.h>

int main(int argc, const char* argv[]) {
	(void)argc;
	FILE* file = fopen(argv[1], "rb");

	while (1) {
		int ch = fgetc(file);
		if (ch == EOF) { break; }
		printf("%02x", ch);
		ch = fgetc(file);
		printf("%02x ", ch);
		while ((ch = fgetc(file)) != 0) {
			printf("%c", ch);
		}
		printf("\n");
	}

	fclose(file);
}
