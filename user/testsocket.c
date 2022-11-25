#include "kernel/types.h"
#include "user/user.h"

// TODO: write socket test in this file
int main(int argc, char *argv[]){
    int s = socket();
    printf("%d\n",s);
    exit(0);
}