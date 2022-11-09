typedef unsigned int   uint;
typedef unsigned short ushort;
typedef unsigned char  uchar;

typedef unsigned char uint8;
typedef unsigned short uint16;
typedef unsigned int  uint32;
typedef unsigned long uint64;

// string.c
int             atoi(const char *s);
int             memcmp(const void*, const void*, uint);
void*           memcpy(void*, const void*, uint);
void*           memmove(void*, const void*, uint);
void*           memset(void*, int, uint);
char*           safestrcpy(char*, const char*, int);
int             strlen(const char*);
int             strcmp(const char *, const char *);
int             strncmp(const char*, const char*, uint);
char*           strncpy(char*, const char*, int);

