#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "io.h"
#include "ipc.h"
#include "log.h"
#include "pa2345.h"

void close_pipes();
void wait_for_children(local_id);

pid_t process_pids[MAX_PROCESSES + 1];

static const char* const usage =
    "This is example of my IPC library.     \
Usage:      -p  NUM_PROCESSES (0 .. 10)     \
            --mutexl                        \n";

int main(int argc, char* argv[])
{
    int is_mutal = 0;
    size_t num_children = -1;
    if (argc < 3)
    {
        log_errorfmt(usage);
        return 1;
    }

    int argi = 1;
    while (argi < argc)
    {
        if (strcmp(argv[argi], "--mutexl") == 0)
            is_mutal = 1;
        else if (strcmp(argv[argi], "-p") == 0)
        {
            argi++;
            if (argc <= argi)
            {
                log_errorfmt(usage);
                return 1;
            }
            else
            {
                num_children = strtol(argv[argi], NULL, 10);
                num_processes = num_children + 1;
            }
        }
        else
        {
            log_errorfmt(usage);
            return 1;
        }
        argi++;
    }

    if (num_children == -1 || num_children >= MAX_PROCESSES)
    {
        log_errorfmt(usage);
        return 1;
    }

    num_processes = num_children + 1;
    log_init();

    // open pipes
    for (size_t source = 0; source < num_processes; source++)
    {
        for (size_t destination = 0; destination < num_processes; destination++)
        {
            if (source != destination)
            {
                int fd[2];
                pipe(fd);
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

    for (size_t id = 1; id <= num_children; id++)
        is_requested[id] = 1;

    // create porcessess
    process_pids[PARENT_ID] = getpid();
    for (size_t id = 1; id <= num_children; id++)
    {
        int child_pid = fork();
        if (child_pid > 0)
        {
            local = (IpcLocal){
                .ipc_id = PARENT_ID,
            };
            process_pids[id] = child_pid;
        }
        else if (child_pid == 0)
        {
            // It is child process.
            local = (IpcLocal){
                .ipc_id = id,
                .balance_history = (BalanceHistory){
                    .s_id = id,
                    .s_history_len = 1,
                    .s_history[0] = (BalanceState){
                        .s_balance = 0,
                        .s_time = 0,
                        .s_balance_pending_in = 0,
                    }},
            };
            break;
        }
        else
        {
            log_errorfmt("Error: Cannot create process. Parrent: %d", process_pids[PARENT_ID]);
        }
    }

    close_pipes();

    local.local_time = 0;
    if (local.ipc_id == PARENT_ID)
    {
        Message msg;
        // Receive ALL DONE
        receive_from_all_children(&local, &msg, num_children);
        received_all_done();
    }
    else
    {
        Message msg;
        // Sent ALL START
        send_started_to_all(&local);
        // Receive ALL START
        receive_from_all_children(&local, &msg, num_children);
        local.done_received = 0;

        if (is_mutal)
            request_cs(&local);
        char str[128];
        int num_prints = local.ipc_id * 5;
        for (int i = 1; i <= num_prints; ++i)
        {
            memset(str, 0, sizeof(str));
            sprintf(str, log_loop_operation_fmt, local.ipc_id, i, num_prints);
            print(str);
        }
        if (is_mutal)
            release_cs(&local);

        send_done_to_all(&local);
        while (local.done_received < num_children - 1)
        {
            Message message;
            receive_any(&local, &message);
            MessageType message_type = message.s_header.s_type;
            switch (message_type)
            {
            case DONE:
                local.done_received++;
                break;
            default:
                break;
            }
        }
    }

    wait_for_children(PARENT_ID);
    log_end();
    return 0;
}

void close_pipes()
{
    for (size_t source = 0; source < num_processes; source++)
    {
        for (size_t destination = 0; destination < num_processes;
             destination++)
        {
            if (source != local.ipc_id && destination != local.ipc_id &&
                source != destination)
            {
                close(writer[source][destination]);
                close(reader[source][destination]);
            }
            if (source == local.ipc_id && destination != local.ipc_id)
            {
                close(reader[source][destination]);
            }
            if (destination == local.ipc_id && source != local.ipc_id)
            {
                close(writer[source][destination]);
            }
        }
    }
}

void wait_for_children(local_id parrend_id)
{
    if (local.ipc_id == parrend_id)
    {
        for (size_t i = 1; i <= num_processes; i++)
        {
            waitpid(process_pids[i], NULL, 0);
        }
    }
}
