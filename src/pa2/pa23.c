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
// #include <assert.h>
#include <dlfcn.h>


void close_pipes();
void wait_for_children(local_id);
void update_state(int);
int receive_all_history(AllHistory*, local_id); 

pid_t process_pids[MAX_PROCESSES+1];
balance_t start_balances[MAX_PROCESSES+1];

static const char * const usage =
"This is example of my IPC library.     \
Usage:      -p  NUM_PROCESSES (0 .. 10) and start balances for this processes\n";


int release_cs(const void * self) {
    AllHistory* all_history = (AllHistory*) self; 
    Message msg;
    int count_child_proc = all_history->s_history_len;

    while(count_child_proc > 0) {
        receive_any(&local, &msg);
        switch(msg.s_header.s_type) {
            case BALANCE_HISTORY:    
                {
                    BalanceHistory * s_history = (BalanceHistory* ) msg.s_payload;
                    local_id id = s_history->s_id;
                    // for(int i = 0; i<s_history->s_history_len; i++) {
                    //     printf("GET HISTORY: id - %u; time - %d value - %d\n", id, i, s_history->s_history[i].s_balance);
                    // }
                    if (id <= all_history->s_history_len) {
                        count_child_proc--;
                        all_history->s_history[id-1] = *s_history;
                    }                    
                }
                break;      
        }
    }
    return IPC_STATUS_SUCCESS;
}

int request_cs(const void * self) {
    Message* msg = (Message*) self;    
    switch(msg->s_header.s_type) {
        case TRANSFER:    
            {
                Transfer *s_transfer = (Transfer* ) msg->s_payload;
                TransferOrder transfer_order = s_transfer->transfer_order;
                if (local.ipc_id == transfer_order.s_dst) {    
                    update_state(transfer_order.s_amount);               
                    transfer_in_fmt(transfer_order.s_src, transfer_order.s_dst, transfer_order.s_amount);                    
                    Message msg = {
                        .s_header =
                            {
                                .s_magic = MESSAGE_MAGIC,
                                .s_type = ACK,
                            },
                    };
                    msg.s_header.s_payload_len = strlen(msg.s_payload);
                    send(&local, PARENT_ID, &msg);                                       
                }
                else {         
                    update_state(-transfer_order.s_amount);       
                    transfer_out_fmt(transfer_order.s_src, transfer_order.s_dst, transfer_order.s_amount);
                    s_transfer->ipc_id = local.ipc_id;
                    transfer(s_transfer, transfer_order.s_src, transfer_order.s_dst, transfer_order.s_amount);
                }                        
            }
            break;
        case STOP:
            return STOP;
        case DONE:            
            return DONE;        
    }
    return -1;
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
                if (fcntl(fd[0], F_SETFL, O_NONBLOCK) < 0)
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
                // .balance_history = NULL,
            };
            process_pids[id] = child_pid;
        } else if (child_pid == 0) {
            // We are inside the child process.
            local = (IpcLocal) { 
                .ipc_id = id,
                .balance_history = (BalanceHistory) {
                    .s_id = id,
                    .s_history_len = 1,
                    .s_history[0] = (BalanceState) {
                        .s_balance = start_balances[id],
                        .s_time = get_physical_time(),
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
    
    // send STARTED message
    if (local.ipc_id != PARENT_ID) {
        Message msg = {
            .s_header =
                {
                    .s_magic = MESSAGE_MAGIC,
                    .s_type = STARTED,
                },
        };
        sprintf(
            msg.s_payload, 
            log_started_fmt, 
            get_physical_time(), 
            local.ipc_id, 
            getpid(), 
            getppid(), 
            local.balance_history.s_history[local.balance_history.s_history_len - 1].s_balance
        );    
        msg.s_header.s_payload_len = strlen(msg.s_payload);
        status = send_multicast(&local, &msg);
        if (status != IPC_STATUS_SUCCESS) {
            log_errorfmt("Error: failed to send multicast from %d. Code: %d\n", local.ipc_id, status);
        }
        started();
    }

    // recieve STARTED message
    for (size_t i = 1; i <= num_children; i++) {
        Message msg;
        if (i == local.ipc_id) {
            continue;
        }
        receive(&local, i, &msg);
    }
    received_all_started();


    if (local.ipc_id == PARENT_ID) {
        Transfer msg_transfer = {
            .ipc_id = local.ipc_id,
        };

        bank_robbery(&msg_transfer, num_children);

        // Send STOP         
        Message msg = {
            .s_header =
                {
                    .s_magic = MESSAGE_MAGIC,
                    .s_type = STOP,
                },
        };
        msg.s_header.s_payload_len = strlen(msg.s_payload);
        status = send_multicast(&local, &msg);
        if (status != IPC_STATUS_SUCCESS) {
            log_errorfmt("Error: failed to send multicast from %d. Code: %d\n", local.ipc_id, status);
        }

        int16_t type;
        local_id count_child_proc = num_children; 
        while(count_child_proc > 0) {
            receive_any(&local, &msg);
            type = request_cs(&msg);             
            if (type == DONE){
                count_child_proc = count_child_proc - 1;
            }
        }
        received_all_done();

        // Receive HISTORY
        AllHistory all_history = {
            .s_history_len = num_children,
        };
        status = release_cs(&all_history);

        
        for(int i = 0; i<4; i++) {
            printf("GET HISTORY: id - 1; time - %d value - %d\n", i, all_history.s_history[3].s_history[i].s_balance);
        }
        for(int i = 0; i<4; i++) {
            printf("GET HISTORY: id - 2; time - %d value - %d\n", i, all_history.s_history[1].s_history[i].s_balance);
        }
        for(int i = 0; i<4; i++) {
            printf("GET HISTORY: id - 3; time - %d value - %d\n", i, all_history.s_history[2].s_history[i].s_balance);
        }
        
        if (status != IPC_STATUS_SUCCESS) {
            log_errorfmt("Error: failed to receive message. Code: %d\n", status);
        }
        else {
            print_history(&all_history);
        }
    }
    else {
        Message msg;
        int16_t type;
        local_id count_child_proc = num_children - 1;
        while (1) {
            receive_any(&local, &msg);
            type = request_cs(&msg); 
            if (type == STOP) {
                Message msg = {
                    .s_header =
                        {
                            .s_magic = MESSAGE_MAGIC,
                            .s_type = DONE,
                        },
                };
                sprintf(msg.s_payload, log_done_fmt, get_physical_time(), local.ipc_id, local.balance_history.s_history[local.balance_history.s_history_len - 1].s_balance);
                msg.s_header.s_payload_len = strlen(msg.s_payload);
                send_multicast(&local, &msg);
                done();
                break;
            }else if (type == DONE){
                count_child_proc = count_child_proc - 1;
            }
        }
        while(count_child_proc > 0) {
            receive_any(&local, &msg);
            type = request_cs(&msg);             
            if (type == DONE){
                count_child_proc = count_child_proc - 1;
            }
        }
        received_all_done();
        
        // SENT HISTORY        
        Message msg_hisory = {
            .s_header =
                {
                    .s_magic = MESSAGE_MAGIC,
                    .s_type = BALANCE_HISTORY,     
                    .s_payload_len = sizeof(BalanceHistory),               
                    .s_local_time = get_physical_time(),
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


        

/*
            if (from == local.ipc_id) {
                from = from + 1;
            }
            if (from > num_children) {
                from = from - num_children - 1;
            }
            Message* msg = malloc(sizeof(*msg));
            nread = read(reader[from][local.ipc_id], msg, sizeof(Message));

            if (nread == -1) {
                if (errno == EAGAIN) {
                        // printf("(pipe empty %u, %u)\n", from, local.ipc_id);
                        sleep(1);
                    }
                    else {
                        perror("read\n");
                        exit(4);
                    }
            }
            else if (nread == 0)
            {
                printf("End of conversation %u\n", local.ipc_id);
                exit(0);
            }
            else
            {
                // printf("GET %u; %u\n", msg->s_header.s_magic, msg->s_header.s_type);
                // printf("Read from - %u to - %u src - %u dst - %u sum - %u\n", from, local.ipc_id, msg->transfer_order.s_src, msg->transfer_order.s_dst, msg->transfer_order.s_amount);
                if (msg->s_header.s_magic != MESSAGE_MAGIC)  
                    continue;

                switch(msg->s_header.s_type) {
                    case TRANSFER:                        
                        printf("GET TRANSFER - %u; %s\n", msg->s_payload, msg->s_payload);
                        // Transfer* transfer = (Transfer* ) msg->s_payload;
                        // TransferOrder transfer_order = transfer->transfer_order;

                        // if (local.ipc_id == transfer_order.s_dst) {    
                        //     update_state(-transfer_order.s_amount);               
                        //     transfer_in_fmt(transfer_order.s_src, transfer_order.s_dst, transfer_order.s_amount);                    
                        //     Message msg = {
                        //         .s_header =
                        //             {
                        //                 .s_magic = MESSAGE_MAGIC,
                        //                 .s_type = ACK,
                        //             },

                        //     };
                        //     msg.s_header.s_payload_len = strlen(msg.s_payload);
                        //     int result = send(&local, PARENT_ID, &msg);
                        //     if (result == IPC_STATUS_SUCCESS) {
                        //         printf("FINAL\n");
                                // break;   // Перенести 
                        //     }                    
                        // }
                        // else {         
                        //     update_state(+transfer_order.s_amount);       
                        //     transfer_out_fmt(transfer_order.s_src, transfer_order.s_dst, transfer_order.s_amount);
                        //     transfer->ipc_id = local.ipc_id;
                        //     transfer(transfer, transfer_order.s_src, transfer_order.s_dst, transfer_order.s_amount);
                        // }                    
                        break;
                    case STOP:
                        printf("STOP\n");
                        break;
                     
                }

            //     if (local.ipc_id == msg->transfer_order.s_dst) {    
            //         update_state(-msg->transfer_order.s_amount);               
            //         transfer_in_fmt(msg->transfer_order.s_src, msg->transfer_order.s_dst, msg->transfer_order.s_amount);                    
            //         Message msg = {
            //             .s_header =
            //                 {
            //                     .s_magic = MESSAGE_MAGIC,
            //                     .s_type = ACK,
            //                 },

            //         };
            //         msg.s_header.s_payload_len = strlen(msg.s_payload);
            //         int result = send(&local, PARENT_ID, &msg);
            //         if (result == IPC_STATUS_SUCCESS) {
            //             printf("FINAL\n");
            //             // break;   // Перенести 
            //         }                    
            //     }
            //     else {         
            //         update_state(+msg->transfer_order.s_amount);       
            //         transfer_out_fmt(msg->transfer_order.s_src, msg->transfer_order.s_dst, msg->transfer_order.s_amount);
            //         msg->ipc_id = local.ipc_id;
            //         transfer(msg, msg->transfer_order.s_src, msg->transfer_order.s_dst, msg->transfer_order.s_amount);
            //    }
            }
            from = from + 1;


            */
        // }
    }
    // else {
    //     int nread;
    //     local_id from = 0;
    //     while (1) {
    //         if (from == local.ipc_id) {
    //             from = from + 1;
    //         }
    //         if (from > num_children) {
    //             from = from - num_children - 1;
    //         }
    //         Transfer* msg = malloc(sizeof(*msg));
    //         nread = read(reader[from][local.ipc_id], msg, sizeof(Transfer));

    //         if (nread == -1) {
    //             if (errno == EAGAIN) {
    //                     // printf("(pipe empty %u, %u)\n", from, local.ipc_id);
    //                     sleep(1);
    //                 }
    //                 else {
    //                     perror("read\n");
    //                     exit(4);
    //                 }
    //         }
    //         else if (nread == 0)
    //         {
    //             printf("End of conversation %u\n", local.ipc_id);
    //             exit(0);
    //         }
    //         else
    //         {
    //             // printf("Read from - %u to - %u src - %u dst - %u sum - %u\n", from, local.ipc_id, msg->transfer_order.s_src, msg->transfer_order.s_dst, msg->transfer_order.s_amount);
    //             if (msg->s_header.s_magic != MESSAGE_MAGIC)  
    //                 continue;
    //             if (local.ipc_id == msg->transfer_order.s_dst) {    
    //                 update_state(-msg->transfer_order.s_amount);               
    //                 transfer_in_fmt(msg->transfer_order.s_src, msg->transfer_order.s_dst, msg->transfer_order.s_amount);                    
    //                 Message msg = {
    //                     .s_header =
    //                         {
    //                             .s_magic = MESSAGE_MAGIC,
    //                             .s_type = ACK,
    //                         },

    //                 };
    //                 msg.s_header.s_payload_len = strlen(msg.s_payload);
    //                 int result = send(&local, PARENT_ID, &msg);
    //                 if (result == IPC_STATUS_SUCCESS) {
    //                     printf("FINAL\n");
    //                     // break;   // Перенести 
    //                 }                    
    //             }
    //             else {         
    //                 update_state(+msg->transfer_order.s_amount);       
    //                 transfer_out_fmt(msg->transfer_order.s_src, msg->transfer_order.s_dst, msg->transfer_order.s_amount);
    //                 msg->ipc_id = local.ipc_id;
    //                 transfer(msg, msg->transfer_order.s_src, msg->transfer_order.s_dst, msg->transfer_order.s_amount);
    //            }
    //         }
    //         from = from + 1;
    //     }
    // }










    // // send DONE message
    // if (local.ipc_id != PARENT_ID) {
    //     Message msg = {
    //         .s_header =
    //             {
    //                 .s_magic = MESSAGE_MAGIC,
    //                 .s_type = DONE,
    //             },
    //     };
    //     // sprintf(msg.s_payload, log_done_fmt, local.ipc_id);
    //     msg.s_header.s_payload_len = strlen(msg.s_payload);
    //     send_multicast(&local, &msg);
    //     done();
    // }

    // // recieve DONE message
    // for (size_t i = 1; i <= num_children; i++) {
    //     Message msg;
    //     if (i == local.ipc_id) {
    //         continue;
    //     }
    //     receive(&local, i, &msg);
    // }
    // received_all_done();



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

void update_state(int sum) {
    timestamp_t time = get_physical_time();
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
                    .s_time = time,
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



        
        // int index = local.balance_history.s_history_len + 1;
        // balance_t new_balance = local.balance_history.s_history[index - 2].s_balance + sum;
        // printf("Uupdate_state: id - %d, sum - %d; old balanse - %d; new balance - %d index - %d\n", local.ipc_id,sum, local.balance_history.s_history[index - 2].s_balance, new_balance, index);
        
        // local.balance_history.s_history[index - 1] = (BalanceState) {
        //     .s_balance = new_balance,
        //     .s_time = time,
        //     .s_balance_pending_in = 0,
        // };
    }    
}

// int receive_all_history(AllHistory * all_history, local_id max_count_child_proc) {
//     all_history->s_history_len = num_processes;
//     Message msg;
//     int count_child_proc = max_count_child_proc; 
//     local_id body;

//     while(count_child_proc > 0) {
//         receive_any(&local, &msg);
//         body = release_cs(&msg);    
//         if (body != -1) {
//             History* s_history = (History*) body;
//             // local_id id = s_history->ipc_id;
//             // if (id <= max_count_child_proc) {
//         //         count_child_proc--;
//         //         all_history->s_history[id] = s_history->s_balance_history;
//             // }
//         }    
//     }
//     return IPC_STATUS_SUCCESS;
// }