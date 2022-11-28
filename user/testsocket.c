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
    assert(socket_fd >= 0, "socket");

    res = dns_api_gethostbyname(SERVER_HOST, &ip);
    assert(res == 0, "dns");
    
    res = socket_connect(socket, ip, port);
    assert(res == 0, "connect");

    while (1) {
        char buf[512];
        uint32 n;

        n = recv(socket_fd, buf, sizeof(buf), 0);
        if (n <= 0)
            break;
        write(1, buf, n); //print
    }

    close(socket_fd);
    return 0;
}



int main(int argc, char *argv[]){
    daytime_test();
    return 0;
}