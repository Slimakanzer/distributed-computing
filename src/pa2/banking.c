#include "banking.h"
#include "io.h"
#include <errno.h>

#include <unistd.h>
#include <assert.h>
#include "ipc.h"

void transfer(void * parent_data, local_id src, local_id dst, balance_t amount) {
    TransferOrder s_transfer_order = {
        src,
        dst,
        amount
    };

    Transfer* transfer = (Transfer*) parent_data;
    transfer->transfer_order = s_transfer_order;
    
    Message msg_transfer = {
        .s_header = {
            .s_magic = MESSAGE_MAGIC,
            .s_type = TRANSFER,
            .s_payload_len = sizeof(Transfer),
            .s_local_time = get_physical_time(),
        },
    };

    memcpy(
        &msg_transfer.s_payload, 
        transfer,
        sizeof(Transfer)
    );

    if (src == num_processes || dst >= num_processes)
        return;
    if (msg_transfer.s_header.s_magic != MESSAGE_MAGIC)
        return;

    local_id next_id_process;
    if (transfer->ipc_id != src) {
        next_id_process = src;
    }
    else {
        next_id_process = dst;
    }
    if (write(writer[transfer->ipc_id][next_id_process], &msg_transfer, sizeof(MessageHeader) + msg_transfer.s_header.s_payload_len) == -1) {
        if (errno == EPIPE)
            return;
    }    
}



// void transfer(void * parent_data, local_id src, local_id dst, balance_t amount) {
//     Message * msg = (Message*)parent_data;

//     msg->s_header.s_payload_len = sizeof(TransferOrder);

//     TransferOrder transfer_order = {
//         src,
//         dst,
//         amount
//     };

//     memcpy(
//         ((Message*)parent_data)->s_payload, 
//         &transfer_order, 
//         sizeof(TransferOrder)
//     );


//     ((Transfer*)parent_data)->transfer_order = transfer_order;
//     ((Transfer*)parent_data)->s_header.s_payload_len = sizeof(transfer_order);

//     if (src == num_processes || dst >= num_processes)
//         IPC_STATUS_ERROR_INVALID_PEER;
//     if (((Transfer*)parent_data)->s_header.s_magic != MESSAGE_MAGIC)
//         IPC_STATUS_ERROR_INVALID_MAGIC;

//     local_id next_id_process;
//     if (((Transfer*)parent_data)->ipc_id != src) {
//         next_id_process = src;
//     }
//     else {
//         next_id_process = dst;
//     }
//     if (write(writer[((Transfer*)parent_data)->ipc_id][next_id_process], parent_data, sizeof(Transfer)) == -1) {
//         if (errno == EPIPE)
//             IPC_STATUS_ERROR_CLOSED_PIPE;
//     }
//     // printf("Write from - %u to - %u src - %u dst - %u sum - %u\n",
//     //     ((Transfer*)parent_data)->ipc_id,
//     //     next_id_process,
//     //     ((Transfer*)parent_data)->transfer_order.s_src,
//     //     ((Transfer*)parent_data)->transfer_order.s_dst,
//     //     ((Transfer*)parent_data)->transfer_order.s_amount
//     // );
// }

// int receive_transfer(local_id from, local_id to, IpcTransfer * msg) {
//     int nread;
//     if (from >= num_processes || to >= num_processes)
//         return IPC_STATUS_ERROR_INVALID_PEER;
//     while (1) {
//         nread = read(reader[from][to], msg, sizeof(IpcTransfer));
//         switch (nread) {
//             case -1:
//                 if (errno == EAGAIN) {
//                     printf("(pipe empty %u, %u, %u)\n", from, to, local.ipc_id);
//                     sleep(1);
//                     break;
//                 }
//                 else {
//                     perror("read\n");
//                     exit(4);
//                 }
//             case 0:
//                 printf("End of conversation\n");
//                 exit(0);
//             default:
//                 printf("Read proc - %u from - %u to - %u src - %u dst - %u sum - %u\n", local.ipc_id, from, to, msg->transfer_order.s_src, msg->transfer_order.s_dst, msg->transfer_order.s_amount);

//                 if (msg->s_header.s_magic != MESSAGE_MAGIC)
//                     return IPC_STATUS_ERROR_INVALID_MAGIC;

//                 if (local.ipc_id == msg->transfer_order.s_dst) {
//                     return IPC_END_LOOP;
//                 }
//                 else {
//                     msg->ipc_id = local.ipc_id;

//                     transfer(msg, msg->transfer_order.s_src, msg->transfer_order.s_dst, msg->transfer_order.s_amount);
//                     return 0;
//                 }
//         }
//     }
// }