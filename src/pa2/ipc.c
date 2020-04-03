
#include <unistd.h>
#include <errno.h>
#include <assert.h>

#include "ipc.h"
#include "io.h"
#include "stdio.h"


// #include "pa2345.h"
/*
static int read_exact(int fd, void* buf, int bytes)
{
  int n = 0, rc;

  assert(buf);
  assert(bytes >= 0);

  while(bytes) {
    rc = read(fd, buf, bytes);
    if(rc <= 0)
        return n;

    buf = (char*) buf + rc;
    n += rc;
    bytes -= rc;
  }

  return n;
}
*/

//------------------------------------------------------------------------------

/** Send a message to the process specified by id.
 *
 * @param self    Any data structure implemented by students to perform I/O
 * @param dst     ID of recepient
 * @param msg     Message to send
 *
 * @return 0 on success, any non-zero value on error
 */
int send(void * self, local_id dst, const Message * msg) {
    if (self == NULL || ((IpcLocal*)self)->ipc_id >= num_processes)
        return IPC_STATUS_ERROR_INVALID_LOCAL;
    if (dst >= num_processes)
        return IPC_STATUS_ERROR_INVALID_PEER;
    if (msg->s_header.s_magic != MESSAGE_MAGIC)
        return IPC_STATUS_ERROR_INVALID_MAGIC;

    if (write(writer[((IpcLocal*)self)->ipc_id][dst], msg, sizeof(MessageHeader) + msg->s_header.s_payload_len) == -1)
    {
        if (errno == EPIPE)
            return IPC_STATUS_ERROR_CLOSED_PIPE;
        else
            return -1;
    }
    return IPC_STATUS_SUCCESS;
}

//------------------------------------------------------------------------------

/** Send multicast message.
 *
 * Send msg to all other processes including parrent.
 * Should stop on the first error.
 * 
 * @param self    Any data structure implemented by students to perform I/O
 * @param msg     Message to multicast.
 *
 * @return 0 on success, any non-zero value on error
 */
int send_multicast(void * self, const Message * msg) {
    for (local_id dst = 0; dst < num_processes; dst++)
    {
        if (dst != ((IpcLocal*)self)->ipc_id) {
            ipc_status_t status = send(self, dst, msg);
            if (status != IPC_STATUS_SUCCESS)
                return status;
        }
    }
    
    return IPC_STATUS_SUCCESS;
}

//------------------------------------------------------------------------------

/** Receive a message from the process specified by id.
 *
 * Might block depending on IPC settings.
 *
 * @param self    Any data structure implemented by students to perform I/O
 * @param from    ID of the process to receive message from
 * @param msg     Message structure allocated by the caller
 *
 * @return 0 on success, any non-zero value on error
 */
int receive(void * self, local_id from, Message * msg) {
    if (self == NULL || ((IpcLocal*)self)->ipc_id >= num_processes)
        return IPC_STATUS_ERROR_INVALID_LOCAL;
    if (from >= num_processes)
        return IPC_STATUS_ERROR_INVALID_PEER;

    int nread;
    while (1) {
        nread = read(reader[from][((IpcLocal*)self)->ipc_id], msg, sizeof(Message));
        switch (nread) {
            case -1:
                if (errno == EAGAIN) {
                    // printf("(pipe empty)\n");
                    sleep(1);
                    break;
                }
                else {
                    printf("read\n");
                    return -1;
               }
            case 0:
                printf("End of conversation\n");
                return -2;            
            default:
                if (msg->s_header.s_magic != MESSAGE_MAGIC)
                    return IPC_STATUS_ERROR_INVALID_MAGIC;
                //printf("Receive: type - %u, mess_text - %s, FROM %u\n", msg->s_header.s_type, msg->s_payload, from);
                return 0;
        }
    }
}

//------------------------------------------------------------------------------

/** Receive a message from any process.
 *
 * Receive a message from any process, in case of blocking I/O should be used
 * with extra care to avoid deadlocks.
 *
 * @param self    Any data structure implemented by students to perform I/O
 * @param msg     Message structure allocated by the caller
 *
 * @return 0 on success, any non-zero value on error
 */
int receive_any(void * self, Message * msg) {
    while(1) {
        int nread;
        for (int from = 0; from < num_processes; from++) {
            if (from == ((IpcLocal*)self)->ipc_id)
                continue;
            nread = read(reader[from][((IpcLocal*)self)->ipc_id], &msg->s_header, sizeof(MessageHeader));
            switch (nread) {
                case -1:
                    if (errno == EAGAIN) {                        
                        sleep(1);
                        continue;
                        break;
                    }
                    else {
                        // printf("read\n");
                        continue;
                        break;
                   }
                case 0:
                    // printf("End of conversation\n");
                    continue;
                    break;
                default:
                    if (msg->s_header.s_magic != MESSAGE_MAGIC)
                        continue;
                    if (msg->s_header.s_payload_len > 0){
                        do {
                            nread =read(reader[from][((IpcLocal*)self)->ipc_id], &msg->s_payload, msg->s_header.s_payload_len);
                        } while (nread == -1 ||  nread == 0);
                    }
                    return 0;                                   
                }
        }
    }


    // if (self == NULL || ((IpcLocal*)self)->ipc_id >= num_processes)
    //     return IPC_STATUS_ERROR_INVALID_LOCAL;

    // int nread;
    // for (int from = 0; from < num_processes; from++) {
    //     while (1) {
    //         nread = read(reader[from][((IpcLocal*)self)->ipc_id], &msg->s_header, sizeof(MessageHeader));
    //         switch (nread) {
    //             case -1:
    //                 if (errno == EAGAIN) {                        
    //                     sleep(1);
    //                     break;
    //                 }
    //                 else {
    //                     printf("read\n");
    //                     break;
    //                }
    //             case 0:
    //                 printf("End of conversation\n");
    //                 break;
    //             default:
    //                 if (msg->s_header.s_magic != MESSAGE_MAGIC)
    //                     break;
    //                 printf("ANY_RECIVE - %u %u\n",  msg->s_header.s_payload_len, sizeof(&msg->s_header));
    //                 nread = read(reader[from][((IpcLocal*)self)->ipc_id], &msg->s_payload, msg->s_header.s_payload_len);
    //                 if (nread != -1 && nread != 0)
    //                     return 0;
    //         }
    //     }
    // }
}