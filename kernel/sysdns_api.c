#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "date.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "proc.h"
#include "lwip/include/lwip/opt.h"

uint64 sys_dns_api_gethostbyname(void) {
    int n;
    char host[DNS_MAX_NAME_LENGTH + 1];

    uint64 u_ip_addr;
    uint32 addr;

    if((n = argstr(0, host, MAXPATH)) < 0) {
        panic("fail to get hostname.");
    }

    if (argaddr(1, &u_ip_addr) < 0) {
        panic("fail to get ip_addr.");
    }
    int res = dns_api_gethostbyname((const char *)host, &addr);
    printf("dns: complete, addr %d\n", addr);
    copyout(myproc()->pagetable, u_ip_addr, (char*)&addr, sizeof(addr));

    return res;
}