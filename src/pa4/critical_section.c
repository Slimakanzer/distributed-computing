#include "io.h"
#include "pa2345.h"
#include "priority_queue.h"

int request_cs(const void* self)
{
    IpcLocal* local = (IpcLocal*)self;
    local->local_time++;

    PriorityQueueElement element = (PriorityQueueElement){
        .ipc_id = local->ipc_id,
        .timestamp = get_lamport_time(),
    };
    ppush(element);
    Message request_msg = {
        .s_header = {
            .s_magic = MESSAGE_MAGIC,
            .s_type = CS_REQUEST,
            .s_local_time = get_lamport_time(),
            .s_payload_len = 0,
        }};

    send_multicast(local, &request_msg);

    int replies_left = num_processes - 2;
    while (1)
    {

        if (replies_left == 0 && ppeek().ipc_id == local->ipc_id)
            break;

        Message received_msg;
        local_id peer = receive_any(local, &received_msg);
        take_max_time_and_inc(local, received_msg.s_header.s_local_time);

        switch (received_msg.s_header.s_type)
        {
        case CS_REPLY:
            replies_left--;
            break;

        case CS_REQUEST:
            element = (PriorityQueueElement){
                .ipc_id = peer,
                .timestamp = received_msg.s_header.s_local_time,
            };
            ppush(element);

            Message message = {
                .s_header = {
                    .s_magic = MESSAGE_MAGIC,
                    .s_local_time = ++local->local_time,
                    .s_type = CS_REPLY,
                    .s_payload_len = 0,
                }};
            send(local, peer, &message);
            break;

        case CS_RELEASE:
            ppop();
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
    ppop();

    Message message = {
        .s_header = {
            .s_magic = MESSAGE_MAGIC,
            .s_local_time = ++local->local_time,
            .s_type = CS_RELEASE,
            .s_payload_len = 0,
        }};

    send_multicast(local, &message);
    return 0;
}
