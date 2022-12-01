# Notes about Net lab
## driver
1. virtio_net_send and virtio_net_recv function in driver should be asynchoronos operation. Read will try to get data from completed read block in recv virtio queue and return the data. It should not wait for the read operation to complete. Similarly, Wrtie will allocate send block, place it into send virtio queue and then exit immediately. It doesn't need to wait for the write operation to complete.
2. There must be at least one empty available recv block being placed in recv virtio queue all the time. This guarantee that device can always receive data successfully. Device does not wait for available recv block. If there is no available recv block in recv virtio queue when data is incoming, unknown errors may happen. For efficiency, we could fullfill the recv virtio queue with empty read block.
3. The driver in this project use legacy interface. And we only implement the minimum driver, which only supports the basic functionality.
4. We do not use interrupt to notify the OS to get the data. So we place a timer(nettimer) in scheduler to ask OS to call virtio_net_recv periodically. Without this timer, OS will get stuck and it keeps waiting for the incoming packet.

## socket
1. In this project, we implement a basic and simple socket, which only supports TCP connection and IPv4 protocol.
2. The user interface of socket functions can be found in `user/user.h`. `socket_send()` and `socket_recv()` is called by `read()`, `write()` system calls. 
3. `socket_recv()`: //TODO: introduce design principles.
4. `socket_send()`: //TODO: introduce design principles.
5. Simple testcases in user/testsocket.c. TODO: introduce tests.
   
