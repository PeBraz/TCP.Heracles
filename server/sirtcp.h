#ifndef _SIRTCP_H_
#define _SIRTCP_H_

struct sirtcp_data {
    #define CLIENT_BUFFER_SIZE 2048
    unsigned char buffer[CLIENT_BUFFER_SIZE];  
    size_t size;
};

int sirtcp_endpoint(int port, int (*callback)(struct sirtcp_data []));
void sirtcp_start();

#endif /* _SIRTCP_H_ */