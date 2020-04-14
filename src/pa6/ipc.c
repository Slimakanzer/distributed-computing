#include <assert.h>
#include <errno.h>
#include <unistd.h>

#include "banking.h"
#include "io.h"
#include "ipc.h"
#include "pa2345.h"
#include "stdio.h"
//------------------------------------------------------------------------------

/** Send a message to the process specified by id.
 *
 * @param self    Any data structure implemented by students to perform I/O
 * @param dst     ID of recepient
 * @param msg     Message to send
 *
 * @return 0 on success, any non-zero value on error
 */
int send(void* self, local_id dst, const Message* msg)
{
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
int send_multicast(void* self, const Message* msg)
{
    for (local_id dst = 0; dst < num_processes; dst++)
    {
        if (dst == ((IpcLocal*)self)->ipc_id)
        {
            continue;
        }
        ipc_status_t status = send(self, dst, msg);
        if (status != IPC_STATUS_SUCCESS)
            return status;
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
int receive(void* self, local_id from, Message* msg)
{
    if (self == NULL || ((IpcLocal*)self)->ipc_id >= num_processes)
        return IPC_STATUS_ERROR_INVALID_LOCAL;
    if (from >= num_processes)
        return IPC_STATUS_ERROR_INVALID_PEER;

    int nread;
    while (1)
    {
        nread = read(reader[from][((IpcLocal*)self)->ipc_id], &msg->s_header, sizeof(MessageHeader));
        if (nread == -1 || nread == 0)
        {
            continue;
        }
        if (msg->s_header.s_payload_len > 0)
        {
            do
            {
                nread = read(reader[from][((IpcLocal*)self)->ipc_id], &msg->s_payload, msg->s_header.s_payload_len);
            } while (nread == -1 || nread == 0);
        }
        return msg->s_header.s_type;
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
int receive_any(void* self, Message* msg)
{
    while (1)
    {
        int nread;
        for (int from = 0; from < num_processes; from++)
        {
            if (from == ((IpcLocal*)self)->ipc_id)
                continue;
            nread = read(reader[from][((IpcLocal*)self)->ipc_id], &msg->s_header, sizeof(MessageHeader));
            if (nread == -1 || nread == 0)
            {
                continue;
            }
            if (msg->s_header.s_payload_len > 0)
            {
                do
                {
                    nread = read(reader[from][((IpcLocal*)self)->ipc_id], &msg->s_payload, msg->s_header.s_payload_len);
                } while (nread == -1 || nread == 0);
            }
            return from;
        }
    }
}

int receive_from_all_children(IpcLocal* self, Message* msg, int max_count_children_proc)
{
    for (int i = 1; i <= max_count_children_proc; i++)
    {
        if (i == self->ipc_id)
        {
            continue;
        }
        receive(self, i, msg);
        take_max_time_and_inc(self, msg->s_header.s_local_time);
    }
    return msg->s_header.s_type;
}

int send_started_to_all(IpcLocal* self)
{
    self->local_time++;
    Message msg = {
        .s_header =
            {
                .s_magic = MESSAGE_MAGIC,
                .s_type = STARTED,
                .s_local_time = get_lamport_time(),
            },
    };
    int payload_len = sprintf(
        msg.s_payload,
        log_started_fmt,
        get_physical_time(),
        self->ipc_id,
        getpid(),
        getppid(),
        self->balance_history.s_history[self->balance_history.s_history_len - 1].s_balance);
    msg.s_header.s_payload_len = payload_len;
    return send_multicast(self, &msg);
}

int send_done_to_all(IpcLocal* self)
{
    self->local_time++;
    Message msg = {
        .s_header =
            {
                .s_magic = MESSAGE_MAGIC,
                .s_type = DONE,
                .s_local_time = get_lamport_time(),
            },
    };
    int payload_len = sprintf(
        msg.s_payload,
        log_done_fmt,
        get_physical_time(),
        self->ipc_id,
        self->balance_history.s_history[self->balance_history.s_history_len - 1].s_balance);
    msg.s_header.s_payload_len = payload_len;
    return send_multicast(self, &msg);
}

int send_stop_to_all(IpcLocal* self)
{
    self->local_time++;
    Message msg = {
        .s_header =
            {
                .s_magic = MESSAGE_MAGIC,
                .s_type = STOP,
                .s_payload_len = 0,
                .s_local_time = get_lamport_time(),
            },
    };
    return send_multicast(self, &msg);
}
