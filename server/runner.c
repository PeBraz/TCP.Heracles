#include <unistd.h>
#include <stdio.h>



int main () {

	pid_t id;
	server_counter = 2;
	port = 8000;
	while((id = fork()) && server_counter--) {
		if (id == -1) {
			fprintf(stderr, "Failed to create process (%d).\n", errno);
			break;
		}
		exevc(,(char *[]){})
	
	}



}
