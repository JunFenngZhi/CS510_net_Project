#include "kernel/types.h"
#include "user/user.h"

/* RFC 867: Daytime Protocol */
#define SERVER_HOST "utcnist.colorado.edu"
#define SERVER_PORT 13

int daytime_test(){
    int socket_fd;
    int res;
    ip4_addr ip;
    uint16 port = SERVER_PORT;

    socket_fd = socket();

    res = dns_api_gethostbyname("localhost", &ip);
    if(res != 0){
        printf("dns fail.\n");
        exit(1);
    }
    
    res = socket_connect(socket_fd, ip, port);
    if(res != 0){
        printf("socket connect fails.\n");
        exit(1);
    }

    while (1) {
        char buf[512];
        uint32 n;

        n = read(socket_fd, (void*)buf, sizeof(buf));
        if (n <= 0)
            break;
        write(1, (const void*)buf, n); //print
    }

    close(socket_fd);
    return 0;
}



int main(int argc, char *argv[]){
    daytime_test();
    return 0;
}