#include "kernel/types.h"
#include "user/user.h"

/* RFC 867: Daytime Protocol */
#define SERVER_HOST "utcnist.colorado.edu"
#define SERVER_PORT 13
#define IPADDR(a,b,c,d) ((((((a<<8)+b)<<8)+c)<<8)+d)

// The address is net order(big endian)
void print_ip(ip4_addr* addr){
    uchar* bytes=(uchar*)addr;
    printf("IP address: %d.%d.%d.%d",bytes[0],bytes[1],bytes[2],bytes[3]);
    printf("\n");
}

int daytime_test(){
    int socket_fd;
    int res;
    ip4_addr ip;
    uint16 port = SERVER_PORT;

    socket_fd = socket();

    res = dns_api_gethostbyname(SERVER_HOST, &ip);
    if(res != 0){
        printf("dns fail.\n");
        exit(1);
    }
    print_ip(&ip);
    
    res = socket_connect(socket_fd, ip, port);
    if(res != 0){
        printf("socket connect fails.\n");
        exit(1);
    }
    
    while (1) {
        char buf[512];
        int n;
        n = read(socket_fd, (void*)buf, sizeof(buf));
        // printf("n = %d\n", n);
        if (n <= 0){
          break;
        }
        write(1, (const void*)buf, n); //print
    }

    close(socket_fd);
    return 0;
}

int send_test(){
    int socket_fd;
    int res;
    ip4_addr ip;
    uint16 port = 1234;

    socket_fd = socket();

    res = dns_api_gethostbyname("gl137-cps510.colab.duke.edu", &ip);
    if(res != 0){
        printf("dns fail.\n");
        exit(1);
    }
    print_ip(&ip);
    
    res = socket_connect(socket_fd, ip, port);
    if(res != 0){
        printf("socket connect fails.\n");
        exit(1);
    }

    char msg[]="Hello World from xv6.\r\n";
    write(socket_fd,msg,sizeof(msg));
    char echo[sizeof(msg)];
    read(socket_fd,echo,sizeof(msg));
    printf(echo);
    return 0;
}

int main(int argc, char *argv[]){
    daytime_test();
    send_test();
    exit(0);
    return 0;
}