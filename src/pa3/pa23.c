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


void set_pending_in(IpcLocal* self, timestamp_t transfer_time, timestamp_t current_time, balance_t income);


int release_cs(const void * self) {
    AllHistory* all_history = (AllHistory*) self; 
    AllHistory new_all_history = (AllHistory) {
        .s_history_len = all_history->s_history_len,
    };

     self = &new_all_history;
    
    Message msg;
    timestamp_t max_time = 0;

    for(size_t from = 1; from <= all_history->s_history_len; from++){
        local.local_time++;
        receive(&local, from, &msg);
        take_max_time_and_inc(&local, msg.s_header.s_local_time);

        if(msg.s_header.s_type == BALANCE_HISTORY) {
            BalanceHistory* balance_history = (BalanceHistory*) msg.s_payload;
            all_history->s_history[balance_history->s_id - 1] = *balance_history;
            
            if (max_time < msg.s_header.s_local_time) {
                max_time = msg.s_header.s_local_time;
            }            
        }
        else {
            printf("Fail status in read history\n");            
        }                
    }   


    for(size_t child_id = 1; child_id <= all_history->s_history_len; child_id++) {
        BalanceHistory balance_history = all_history->s_history[child_id - 1];
        BalanceHistory new_balance_history = (BalanceHistory) {
            .s_id = balance_history.s_id,
            .s_history_len = max_time + 1,
        };

        for(int time = 0; time< balance_history.s_history_len; time++) {
            new_balance_history.s_history[time].s_balance = balance_history.s_history[time].s_balance;
            new_balance_history.s_history[time].s_time = time;
            new_balance_history.s_history[time].s_balance_pending_in = balance_history.s_history[time].s_balance_pending_in;
        }
        
        balance_t last_balance = balance_history.s_history[balance_history.s_history_len - 1].s_balance;
        for(int time = balance_history.s_history_len; time<=max_time; time++) {
            new_balance_history.s_history[time].s_balance = last_balance;
            new_balance_history.s_history[time].s_time = time;
            new_balance_history.s_history[time].s_balance_pending_in = 0;
        }

        new_all_history.s_history[child_id - 1] = new_balance_history;

    }

    print_history(&new_all_history);
    return IPC_STATUS_SUCCESS;
}

void transfer_handler(IpcLocal* self, Message* msg) {
    timestamp_t transfer_time = msg->s_header.s_local_time;
    balance_t delta;

    Transfer *s_transfer = (Transfer* ) msg->s_payload;
    TransferOrder transfer_order = s_transfer->transfer_order;
    // printf("Ttransfer_handler__________________________ id - %d; from - %d; to -%d\n", local.ipc_id, transfer_order.s_src, transfer_order.s_dst);
    if (local.ipc_id == transfer_order.s_dst) {    
        // printf("Ttransfer_handler_1 id - %d; from - %d; to -%d\n", local.ipc_id, transfer_order.s_src, transfer_order.s_dst);
        delta = transfer_order.s_amount;
        transfer_in_fmt(transfer_order.s_src, transfer_order.s_dst, transfer_order.s_amount);                    
        self->local_time++;
        // update_state(transfer_order.s_amount, current_time); 
        Message msg = {
            .s_header =
                {
                    .s_magic = MESSAGE_MAGIC,
                    .s_type = ACK,
                    .s_local_time = get_lamport_time(),
                    .s_payload_len = 0,
                },
        };
        msg.s_header.s_payload_len = strlen(msg.s_payload);        
        send(&local, PARENT_ID, &msg);                                       
    }
    else {         
        // printf("Ttransfer_handler_2 id - %d; from - %d; to -%d\n", local.ipc_id, transfer_order.s_src, transfer_order.s_dst);        
        delta = -transfer_order.s_amount;
        transfer_out_fmt(transfer_order.s_src, transfer_order.s_dst, transfer_order.s_amount);
        s_transfer->ipc_id = local.ipc_id;        
        transfer(s_transfer, transfer_order.s_src, transfer_order.s_dst, transfer_order.s_amount);
    }

    update_state(delta, transfer_time); 
}

void set_pending_in(IpcLocal* self, timestamp_t transfer_time, timestamp_t current_time, balance_t income) {
    if (current_time > transfer_time) {
        for (timestamp_t time = transfer_time; time < current_time; time++) {
            self->balance_history.s_history[time].s_balance_pending_in = income;
        }
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

    local.local_time = 0;

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
            
        if (status != IPC_STATUS_SUCCESS) {
            log_errorfmt("Error: failed to receive message. Code: %d\n", status);
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
            local.local_time++;
            type = receive_any(&local, &msg);
            take_max_time_and_inc(&local, msg.s_header.s_local_time);
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
            local.local_time++;
            type = receive_any(&local, &msg); 
            take_max_time_and_inc(&local, msg.s_header.s_local_time);                  
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
        local.local_time++;
        
        Message msg_hisory = {
            .s_header =
                {
                    .s_magic = MESSAGE_MAGIC,
                    .s_type = BALANCE_HISTORY,     
                    .s_payload_len = sizeof(BalanceHistory),               
                    .s_local_time = local.local_time,
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
    timestamp_t time_now = get_lamport_time();

    if (time_now <= MAX_T) {
        int len = local.balance_history.s_history_len;
        if (len - time_now == 1) {
            if (sum > 0) 
                set_pending_in(&local, time, time_now, sum);
            local.balance_history.s_history[len - 1].s_balance += sum;
        }
        else if (len == time_now) {
            if (sum > 0) 
                set_pending_in(&local, time, time_now, sum);
            balance_t new_balance = local.balance_history.s_history[len - 1].s_balance + sum;
            len++;            
            local.balance_history.s_history[len - 1] = (BalanceState) {
                .s_balance = new_balance,
                .s_time = time_now,
                .s_balance_pending_in = 0,
            };
            local.balance_history.s_history_len++;
        }
        else if (time_now - len > 0) {                   
            balance_t last_balance = local.balance_history.s_history[len - 1].s_balance;
            for (int index = len; index < time_now; index++) {
                local.balance_history.s_history[index] = (BalanceState) {
                    .s_balance = last_balance,
                    .s_time = index,
                    .s_balance_pending_in = 0,
                };
            }   
            if (sum > 0) 
                set_pending_in(&local, time, time_now, sum);         
            local.balance_history.s_history[time_now] = (BalanceState) {
                .s_balance = last_balance + sum,
                .s_time = time_now,
                .s_balance_pending_in = 0,
            };
            local.balance_history.s_history_len = time_now + 1;
        }
    }    
}
