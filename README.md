# Net project based on xv6
## Introduction
In this project, we add network support for xv6 operating system, following the instructions of [net Lab](https://courses.cs.washington.edu/courses/cse451/21au/labs/net.html). We implement a driver for virtio network interface controller(NIC) to enable xv6 receive and transmit packets. Also, we add socket to xv6 based on lwIP network stack.

## Desgin
### Driver
The design of the NIC driver include three parts: network device initialization, send packet, receive packet. Driver is written in `kernel/virtio_net.c`
* `virtio_net_init()`: The initiation process  first reset the device, set the acknowledge bit and set the driver bit, then negotiate the features bit. Since we only negotiate the VIRTIO_NET_F_MAC bit, we are actually using the legacy version and using minimum functionality. We should ensure the FEATURES_OK bit is set. After that, we need to initialize the receive and transmit queues. Each queue should be allocated their own pages for descriptors. And we set their registers to point to these pages. After setting each queue, we notify the device that the queue is ready with VIRTIO_MMIO_QUEUE_READY and tell the device with VIRTIO_CONFIG_S_DRIVER_OK bit and then pass out the mac address. One more thing we need to do is for our receive queues, we need to first allocate descriptors and buffers after the initialization process to make sure there are always available buffers for catching incoming data. There must be at least one empty available recv block being placed in recv virtio queue all the time. This guarantee that device can always receive data successfully. Device does not wait for available recv block. If there is no available recv block in recv virtio queue when data is incoming, unknown errors may happen. For efficiency, we could fullfill the recv virtio queue with empty read block.
* `virtio_net_send()`: At the beginning of sending, we first keep checking if there are any descriptors of send op that were just completed. If so, we update the used_idx until there are no more completed descriptors that need to be freed. Then we allocate two new descriptors for the current send request, the first one is for the header and the second one is for the data. Since we are using the legacy version, the header will not contain the `num_buffers` attribute, which means the size of the header is always 10 bytes. Both descriptors' flags should be `DEVICE_READ`. We notify our transmit queue after filling in descriptors.
* `virtio_net_recv()`:  Like above, we first check if there are any descriptors of recv op that were just completed. If so, we copy the data from buffer to the data address provided by user. One difference here is we just check `used_ring` for once instead of keeping checking, since we only have one data address to copy for the user in one recv request. Another difference is that both descriptors’ flags should be `DEVICE_WRITE`. The rest would be the same as the sending.
* `virtio_net_send` and `virtio_net_recv` function in driver should be asynchoronos operation. Recv op tries to get data from completed read block in recv virtio queue and return the data. It should not wait for the read operation to complete. Similarly, Send op allocate send block, place it into send virtio queue and then exit immediately. It doesn't need to wait for the write operation to complete.
* In this project, we do not use interrupt to notify the OS to get the data, so we place a `nettimer` in scheduler(`/kernel/proc.c`) to ask OS to incoke `virtio_net_recv` periodically. Without this timer, OS will get stuck and it keeps waiting for the incoming packet.
### Socket
In this project, we implement a basic and simple socket, which only supports TCP connection and IPv4 protocol. The detailed implementation can be found in `kernel/socket.c`.
* Following the design in linux, socket in xv6 is just a new type of file. We modify existing system calls `read()`, `write()`, `close()` to dispatch socket operations. Also, we add some new system calls to support server-side and client-side socket operations. 
* `socket_recv()`: Each socket has a recv_buffer(defined in `struct file`), which is implemented by a circular queue. Once data arrives, recv callback function `tcp_recv_packet()` will be called. This function will allocate a new recv buffer descriptor to store the arrived data. Later, `socket_recv()` called by application is responsible for retrieving data from recv_buffer and free corresponding descriptor. If recv buffer is empty when `socket_recv()` is called, `socket_recv()` will be blocked and wait for the available data. Therefore, read operation in the socket is asynchronous.
* `socket_send()`: Write operation in socket is simple. In `socket_send()`, it tries its best to put data into sending queue(maintained by lwIP). If it is successful, `socket_send()` will return 0, otherwise it will return -1. Data in sending queue will be sent out in the future and user applications don't need to take care of it.

## How to run this project
1. Run docker: `sudo docker run -it --rm -v $PWD:/home/xv6/xv6-riscv iqicheng/cps310-env`
2. Build and run xv6: `make clean`, `make`, `make qemu`
3. Run our tests for socket: See next section
4. Exit xv6 to docker container `ctrl-a follwed by x`

## Test
We write 3 simple functionality test for our sockets in `user/testsocket.c`
* Daytime test：Run `daytime_test()` function. In this test, xv6 works as a client, connects to a testing server and gets daytime response.
* Client test: Run `./echo_server.py` in other machine and run `client_send_recv_test()` in xv6. In this test, xv6 connects to a echo server and sends a message.
* Server test: Open two window linked to the same docker. In one window, run `server_send_recv_test()` function in xv6. In another window, run `sudo apt update` && `sudo apt install -y telnet`, then run `telnet 127.0.0.1 25001` to connect to the server program in xv6.

## Reference
1. Virtio driver document: https://docs.oasis-open.org/virtio/virtio/v1.0/virtio-v1.0.html
2. lwIP raw TCP APIs: https://www.nongnu.org/lwip/2_1_x/group__tcp__raw.html
3. xv6 document: https://pdos.csail.mit.edu/6.S081/2020/xv6/book-riscv-rev1.pdf

