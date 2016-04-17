



int upload(struct tcp_client * client)
{


}


int main() 
{

    endpoint(8000, upload);
    int main(int argc, char *argv[]) 
{
	int i;
	char *cong_proto_name = NULL;
	char *upload_filename = NULL;
	for (i=1; i < argc; i+=2) {
		if (!strcmp(CONG_PROTO_FLAG, argv[i])) {
			if (i + 1 >= argc) goto cmdl_cleanup;
			cong_proto_name = strdup(argv[i + 1]);
		} else if (!strcmp(UPLOAD_FILE_FLAG, argv[i])) {
			if (i + 1 >= argc) goto cmdl_cleanup;
			upload_filename = strdup(argv[i+1]);
		} else {
			puts("usage: ./server [--upload <filename>]");
			puts("	--cong <tcp-congestion-name> ");
			goto cmdl_cleanup;
		}
	}

	if (!upload_filename) {
		puts("\"--upload\" required");
		goto cmdl_cleanup; 
	}


cleanup:
	close(server.sock);
cmdl_cleanup:
	if (upload_filename) free(upload_filename);
	if (cong_proto_name) free(cong_proto_name);
	return 0;
}
    return 0;
}