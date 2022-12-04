#include "kernel/types.h"
#include "user/user.h"

#define DAYTIME_SERVER_HOST "utcnist.colorado.edu"
#define DAYTIME_SERVER_PORT 13
#define CLIENT_TEST_SERVER_HOST "gl137-cps510.colab.duke.edu"
#define CLIENT_TEST_SERVER_PORT 1234
#define SERVER_TEST_HOST "localhost"
#define SERVER_TEST_PORT 80

#define IPADDR(a, b, c, d) ((((((a << 8) + b) << 8) + c) << 8) + d)

// The address is net order(big endian)
void print_ip(ip4_addr *addr) {
  uchar *bytes = (uchar *)addr;
  printf("IP address: %d.%d.%d.%d", bytes[0], bytes[1], bytes[2], bytes[3]);
  printf("\n");
}

int daytime_test() {
  int socket_fd;
  int res;
  ip4_addr ip;
  uint16 port = DAYTIME_SERVER_PORT;

  socket_fd = socket();

  res = dns_api_gethostbyname(DAYTIME_SERVER_HOST, &ip);
  if (res != 0) {
    printf("dns fail.\n");
    exit(1);
  }
  print_ip(&ip);

  res = socket_connect(socket_fd, ip, port);
  if (res != 0) {
    printf("socket connect fails.\n");
    exit(1);
  }

  while (1) {
    char buf[512];
    int n;
    n = read(socket_fd, (void *)buf, sizeof(buf));
    // printf("n = %d\n", n);
    if (n <= 0) {
      break;
    }
    write(1, (const void *)buf, n); // print
  }

  close(socket_fd);
  return 0;
}

// Test client's basic functionality
int client_send_recv_test() {
  int socket_fd;
  int res;
  ip4_addr ip;
  uint16 port = CLIENT_TEST_SERVER_PORT;

  socket_fd = socket();

  res = dns_api_gethostbyname(CLIENT_TEST_SERVER_HOST, &ip);
  if (res != 0) {
    printf("dns fail.\n");
    exit(1);
  }
  print_ip(&ip);

  res = socket_connect(socket_fd, ip, port);
  if (res != 0) {
    printf("socket connect fails.\n");
    exit(1);
  }

  char msg[] = "Hello World from xv6.\r\n";
  write(socket_fd, msg, sizeof(msg));

  char echo[sizeof(msg)];
  read(socket_fd, echo, sizeof(msg));

  printf(echo);
  close(socket_fd);
  return 0;
}

// Test server's basic functionality
int server_send_recv_test() {
  int listen_fd = socket();
  int conn_fd;
  ip4_addr ip = 0;
  uint16 port = SERVER_TEST_PORT;

  print_ip(&ip);

  socket_bind(listen_fd, ip, port);
  printf("socket_bind.\n");
  socket_listen(listen_fd);
  printf("socket_listen.\n");
  while (1) {
    conn_fd = socket_accept(listen_fd);
    if (conn_fd == -1) {
      printf("fail to accept new connection.\n");
      continue;
    }
    printf("socket_accept.\n");
    close(listen_fd);
    break;
  }

  // echo server
  char msg[512] = {0};
  while (1) {
    int n = read(conn_fd, msg, 512);
    if (n == 0) {
      printf("Connection closed by remote host.\n");
      close(conn_fd);
      break;
    }

    if (strcmp(msg, "quit\r\n") == 0 || strcmp(msg, "exit\r\n") == 0 ||
        strcmp(msg, "bye\r\n") == 0) {
      close(conn_fd);
      break;
    }
    printf("Get client request:\r\n%s", msg);

    strcpy(msg + strlen(msg), "---serverResponse---\r\n");
    write(conn_fd, msg, strlen(msg));
    memset(msg, 0, 512);
  }
  return 0;
}

int main(int argc, char *argv[]) {
  // daytime_test();
  // client_send_recv_test();
  server_send_recv_test();
  exit(0); // we can't use return 0 to exit xv6's user program
}