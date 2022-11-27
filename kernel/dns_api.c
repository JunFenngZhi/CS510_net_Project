#include "lwip/include/lwip/dns.h"
#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "fs.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "file.h"
#include "stat.h"
#include "proc.h"

void dns_found_callback_fn(const char *name, const ip_addr_t *ipaddr, void *callback_arg) {
    printf("dns_found_callback_fn do nothing.\n");
}

int dns_api_gethostbyname(const char *hostname, uint32 *addr) {
    dns_found_callback dns_found_cb = dns_found_callback_fn;
    int res = dns_gethostbyname(hostname, (ip_addr_t *)addr, dns_found_cb, NULL);

    if (res != ERR_OK) {
        printf("dns_api_gethostbyname: cannot get the ip, error num: %d\n", res);
        return -1;
    }
    else {
        printf("dns_api_gethostbyname: get the ip: %d", *addr);
        return 0;
    }
}