#include "io.h"
#include "pa2345.h"

int is_deferred[MAX_PROCESSES];

int request_cs(const void* self)
{
    IpcLocal* local = (IpcLocal*)self;
    timestamp_t request_time = ++local->local_time;

    Message request_msg = {
        .s_header = {
            .s_magic = MESSAGE_MAGIC,
            .s_type = CS_REQUEST,
            .s_local_time = get_lamport_time(),
            .s_payload_len = 0,
        }};
    send_multicast(local, &request_msg);

    int replies_left = num_processes - 2;
    while (replies_left > 0)
    {
        Message received_msg;
        local_id peer = receive_any(local, &received_msg);
        take_max_time_and_inc(local, received_msg.s_header.s_local_time);

        switch (received_msg.s_header.s_type)
        {
        case CS_REPLY:
            replies_left--;
            break;

        case CS_REQUEST:
            if (request_time > received_msg.s_header.s_local_time || (request_time == received_msg.s_header.s_local_time && local->ipc_id < peer))
            {
                is_deferred[peer] = 0;
                Message message = {
                    .s_header = {
                        .s_magic = MESSAGE_MAGIC,
                        .s_local_time = ++local->local_time,
                        .s_type = CS_REPLY,
                        .s_payload_len = 0,
                    }};
                send(local, peer, &message);
            }
            else
            {
                is_deferred[peer] = 1;
            }
            break;

        case DONE:
            local->done_received++;
            break;
        }
    }
    return 0;
}

int release_cs(const void* self)
{
    IpcLocal* local = (IpcLocal*)self;

    for (local_id peer = 1; peer <= num_processes - 1; ++peer)
    {
        if (peer != local->ipc_id && is_deferred[peer])
        {
            Message message = {
                .s_header = {
                    .s_magic = MESSAGE_MAGIC,
                    .s_local_time = ++local->local_time,
                    .s_type = CS_REPLY,
                    .s_payload_len = 0,
                }};
            send(local, peer, &message);
            is_deferred[peer] = 0;
        }
    }
    return 0;
}
