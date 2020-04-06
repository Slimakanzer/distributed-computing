#include "banking.h"
#include "io.h"
#include <errno.h>
#include <stdio.h>

#include <unistd.h>
#include <assert.h>
#include "ipc.h"
#include <string.h>

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

    if (local.ipc_id == PARENT_ID) {
        // ACK
        Message msg_receive_trans;    
        receive(&local, dst, &msg_receive_trans);
        if (msg_receive_trans.s_header.s_type != ACK) {
            printf("Trans is not atomarniy in proc - %u\n", local.ipc_id);
        }  
    }  
}
