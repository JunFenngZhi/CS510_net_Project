#include "kernel/types.h"
#include "user/user.h"

// TODO: write socket test in this file
int main(int argc, char *argv[]){
    uint32 addr;
    int res = dns_api_gethostbyname("localhost", &addr);
    printf("res %d, addr %d\n",res, addr);
    exit(0);
}