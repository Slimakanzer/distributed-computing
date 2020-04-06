#include <stdio.h> 
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>

#include "banking.h"
#include "io.h"
#include "ipc.h"
#include "log.h"
#include "pa2345.h"
#include <errno.h>
#include <dlfcn.h>

void close_pipes();
void wait_for_children(local_id);
void update_state(int, timestamp_t);
void transfer_handler(IpcLocal*, Message*);

pid_t process_pids[MAX_PROCESSES+1];
balance_t start_balances[MAX_PROCESSES+1];

static const char * const usage =
"This is example of my IPC library.     \
Usage:      -p  NUM_PROCESSES (0 .. 10) and start balances for this processes\n";



int release_cs(const void * self) {
    AllHistory* all_history = (AllHistory*) self; 
    Message msg;

    for(size_t from = 1; from <= all_history->s_history_len ; from++){
        receive(&local, from, &msg);
        if(msg.s_header.s_type == BALANCE_HISTORY) {
            BalanceHistory* balance_history = (BalanceHistory*) msg.s_payload;
            all_history->s_history[balance_history->s_id - 1] = *balance_history;
            // all_history->s_history[from - 1] = *balance_history;
        }
        else {
            printf("Fail status in read history\n");
            return IPC_STATUS_ERROR_INVALID_READING;
        }                
    }    
    return IPC_STATUS_SUCCESS;
}

void transfer_handler(IpcLocal* self, Message* msg) {
    timestamp_t current_time = get_physical_time();

    Transfer *s_transfer = (Transfer* ) msg->s_payload;
    TransferOrder transfer_order = s_transfer->transfer_order;
    // printf("Ttransfer_handler__________________________ id - %d; from - %d; to -%d\n", local.ipc_id, transfer_order.s_src, transfer_order.s_dst);
    if (local.ipc_id == transfer_order.s_dst) {    
        // printf("Ttransfer_handler_1 id - %d; from - %d; to -%d\n", local.ipc_id, transfer_order.s_src, transfer_order.s_dst);
        update_state(transfer_order.s_amount, current_time);  
        transfer_in_fmt(transfer_order.s_src, transfer_order.s_dst, transfer_order.s_amount);                    
        Message msg = {
            .s_header =
                {
                    .s_magic = MESSAGE_MAGIC,
                    .s_type = ACK,
                    .s_local_time = current_time,
                    .s_payload_len = 0,
                },
        };
        msg.s_header.s_payload_len = strlen(msg.s_payload);
        send(&local, PARENT_ID, &msg);                                       
    }
    else {         
        // printf("Ttransfer_handler_2 id - %d; from - %d; to -%d\n", local.ipc_id, transfer_order.s_src, transfer_order.s_dst);        
        update_state(-transfer_order.s_amount, current_time); 
        transfer_out_fmt(transfer_order.s_src, transfer_order.s_dst, transfer_order.s_amount);
        s_transfer->ipc_id = local.ipc_id;        
        transfer(s_transfer, transfer_order.s_src, transfer_order.s_dst, transfer_order.s_amount);
    }
}

int main(int argc, char * argv[])
{
    size_t num_children;

    if (argc >= 3 && strcmp(argv[1], "-p") == 0 && strtol(argv[2], NULL, 10) == argc - 3) {
        num_children = strtol(argv[2], NULL, 10);
        for(int i = 1; i <= num_children; i++) {
            start_balances[i] = strtol(argv[i + 3 - 1], NULL, 10);
        }
    } else {
        log_errorfmt(usage);
        return 1;
    }

    if (num_children >= MAX_PROCESSES) {
        log_errorfmt(usage);
        return 1;
    }

    num_processes = num_children + 1;
    log_init();

    // open pipes
    for (size_t source = 0; source < num_processes; source++) {
        for (size_t destination = 0; destination < num_processes; destination++) {
            if (source != destination) {
                int fd[2]; pipe(fd);
                unsigned int flags_for_read = fcntl(fd[0], F_GETFL, 0);
                unsigned int flags_for_write = fcntl(fd[1], F_GETFL, 0);
                if (fcntl(fd[0], F_SETFL, flags_for_read | O_NONBLOCK) < 0)
                    exit(2);
                if (fcntl(fd[1], F_SETFL, flags_for_write | O_NONBLOCK) < 0)
                    exit(2);
                reader[source][destination] = fd[0];
                writer[source][destination] = fd[1];
                pipe_opened(source, destination);
            }
        }
    }


    // create porcessess
    process_pids[PARENT_ID] = getpid();
    for (size_t id = 1; id <= num_children; id++) {
        int child_pid = fork();
        if (child_pid > 0) {
            local = (IpcLocal) { 
                .ipc_id = PARENT_ID,
            };
            process_pids[id] = child_pid;
        } else if (child_pid == 0) {
            // It is child process.
            local = (IpcLocal) { 
                .ipc_id = id,
                .balance_history = (BalanceHistory) {
                    .s_id = id,
                    .s_history_len = 1,
                    .s_history[0] = (BalanceState) {
                        .s_balance = start_balances[id],
                        .s_time = 0,
                        .s_balance_pending_in = 0,
                    }
                },
            };
            break;
        } else {
            log_errorfmt("Error: Cannot create process. Parrent: %d", process_pids[PARENT_ID]);
        }
    }

    close_pipes();
    ipc_status_t status;

    if (local.ipc_id == PARENT_ID) {
        Message msg;
        started();
        // Receive ALL START
        receive_from_all_children(&local, &msg, num_children);
        received_all_started();
        // Main Work
        Transfer msg_transfer = {
            .ipc_id = local.ipc_id,
        };
        bank_robbery(&msg_transfer, num_children);    
        // Send ALL STOP         
        status = send_stop_to_all(&local);
        if (status != IPC_STATUS_SUCCESS) {
            log_errorfmt("Error: failed to send multicast from %d. Code: %d\n", local.ipc_id, status);
        }
        // Receive ALL DONE
        receive_from_all_children(&local, &msg, num_children);
        received_all_done();


        // Receive ALL HISTORY
        AllHistory all_history = {
            .s_history_len = num_children,
        };
        status = release_cs(&all_history);

        
        // for(int i = 0; i<4; i++) {
        //     printf("GET HISTORY: id - 1; time - %d value - %d\n", i, all_history.s_history[3].s_history[i].s_balance);
        // }
        // for(int i = 0; i<4; i++) {
        //     printf("GET HISTORY: id - 2; time - %d value - %d\n", i, all_history.s_history[1].s_history[i].s_balance);
        // }
        // for(int i = 0; i<4; i++) {
        //     printf("GET HISTORY: id - 3; time - %d value - %d\n", i, all_history.s_history[2].s_history[i].s_balance);
        // }
        
        if (status != IPC_STATUS_SUCCESS) {
            log_errorfmt("Error: failed to receive message. Code: %d\n", status);
        }
        else {
            print_history(&all_history);
        }
    }
    else {
        Message msg;
        // Sent ALL START
        send_started_to_all(&local);
        started();
        // Receive ALL START
        receive_from_all_children(&local, &msg, num_children);
        received_all_started();
        // Main Work
        int16_t type = -1;
        local_id count_child_proc = num_children - 1;
        while (type != STOP) {
            type = receive_any(&local, &msg);
            switch (type) {
                case TRANSFER:
                    transfer_handler(&local, &msg);                
                    break;
                case STOP:
                    send_done_to_all(&local);
                    done();
                    break;
                case DONE:
                    count_child_proc = count_child_proc - 1;
                    break;                    
            }
        }
        type = -1;
        while(count_child_proc > 0) {
            type = receive_any(&local, &msg);                       
            switch (type) {
                case TRANSFER:
                    transfer_handler(&local, &msg);                
                    break;               
                case DONE:
                    count_child_proc = count_child_proc - 1;
                    break;                    
            }
        }
        received_all_done();        
        // SENT HISTORY     
        timestamp_t time = get_physical_time();
        update_state(0, time);
        
        Message msg_hisory = {
            .s_header =
                {
                    .s_magic = MESSAGE_MAGIC,
                    .s_type = BALANCE_HISTORY,     
                    .s_payload_len = sizeof(BalanceHistory),               
                    .s_local_time = time,
                },
        };

        memcpy(
            &msg_hisory.s_payload, 
            &local.balance_history,
            sizeof(BalanceHistory)
        );    

        // for(int i = 0; i<local.balance_history.s_history_len; i++) {
        //     printf("SENT HISTORY: len - %u; id - %u; time - %d value - %d\n", local.balance_history.s_history_len, local.ipc_id, i, local.balance_history.s_history[i].s_balance);
        // }

        status = send(&local, PARENT_ID, &msg_hisory);
        if (status != IPC_STATUS_SUCCESS) {
            log_errorfmt("Error: failed to send multicast from %d. Code: %d\n", local.ipc_id, status);
        }
    }

    wait_for_children(PARENT_ID);
    log_end();
    return 0;
}

void close_pipes() {
    for (size_t source = 0; source < num_processes; source++) {
        for (size_t destination = 0; destination < num_processes;
             destination++) {
            if (source != local.ipc_id && destination != local.ipc_id &&
                source != destination) {
                close(writer[source][destination]);
                close(reader[source][destination]);
            }
            if (source == local.ipc_id && destination != local.ipc_id) {
                close(reader[source][destination]);
            }
            if (destination == local.ipc_id && source != local.ipc_id) {
                close(writer[source][destination]);
            }
        }
    }    
}

void wait_for_children(local_id parrend_id) {
    if (local.ipc_id == parrend_id) {
        for (size_t i = 1; i <= num_processes; i++) {
            waitpid(process_pids[i], NULL, 0);
        }
    }
}

void update_state(int sum, timestamp_t time) {
    if (time <= MAX_T) {
        int len = local.balance_history.s_history_len;
        if (len - time == 1) {
            local.balance_history.s_history[len - 1].s_balance += sum;
        }
        else if (len == time) {
            balance_t new_balance = local.balance_history.s_history[len - 1].s_balance + sum;
            len++;            
            local.balance_history.s_history[len - 1] = (BalanceState) {
                .s_balance = new_balance,
                .s_time = time,
                .s_balance_pending_in = 0,
            };
            local.balance_history.s_history_len++;
        }
        else if (time - len > 0) {                   
            balance_t last_balance = local.balance_history.s_history[len - 1].s_balance;
            for (int index = len; index < time; index++) {
                local.balance_history.s_history[index] = (BalanceState) {
                    .s_balance = last_balance,
                    .s_time = index,
                    .s_balance_pending_in = 0,
                };
            }            
            local.balance_history.s_history[time] = (BalanceState) {
                .s_balance = last_balance + sum,
                .s_time = time,
                .s_balance_pending_in = 0,
            };
            local.balance_history.s_history_len = time + 1;
        }
    }    
}
