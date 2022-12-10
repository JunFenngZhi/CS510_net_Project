## run docker
`sudo docker run -it --rm -v $PWD:/home/xv6/xv6-riscv iqicheng/cps310-env`

## build and run xv6
`make clean`
`make`
`make qemu`

## link another window to the same docker
`docker exec -it \<container id\> /bin/bash`

## exit xv6 to docker container
ctrl-a follwed by x

## test
run `sudo python3 grade_xxx.py` in docker

## add a user program
1. Implement the Unix program for xv6 in `user/myUserProgram.c`
2. Add your user program to `UPROGS` in Makefile

## add a system call
1. Add system_call declaration in `user/user.h`  
2. Update `user/usys.pl` by adding an entry (which generates `user/usys.S` ). Update `kernel/syscall.h`, and `kernel/syscall.c` by adding new define and new items in syscalls array to allow user programm to invoke your system calls.
3. implement your system call in *.c file.(E.g. `/kernel/sysproc.c`)

## Debug
1. open another window to the same docker
2. In one window, run `make qemu-gdb`
3. In another window, run `make gdb`. Then you can add your own break point and type `c` to run the xv6.
4. Reference: https://courses.cs.duke.edu/fall22/compsci510/guidance.html