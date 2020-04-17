#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "io.h"
#include "ipc.h"
#include "log.h"
#include "pa1.h"

void close_pipes();
void wait_for_children(local_id);

pid_t process_pids[MAX_PROCESSES+1];
static const char * const usage =
"This is example of my IPC library.    \n\
Usage:      -p  NUM_PROCESSES (0 .. 10)\n";

int main(int argc, char const *argv[]) {
    size_t num_children;

    if (argc == 3 && strcmp(argv[1], "-p") == 0) {
        num_children = strtol(argv[2], NULL, 10);
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
            local = (IpcLocal) { .ipc_id = PARENT_ID }; // ??
            process_pids[id] = child_pid;
        } else if (child_pid == 0) {
            // We are inside the child process.
            local = (IpcLocal) { .ipc_id = id };
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
        sprintf(msg.s_payload, log_started_fmt, local.ipc_id, getpid(), getppid());
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

    // send DONE message
    if (local.ipc_id != PARENT_ID) {
        Message msg = {
            .s_header =
                {
                    .s_magic = MESSAGE_MAGIC,
                    .s_type = DONE,
                },
        };
        sprintf(msg.s_payload, log_done_fmt, local.ipc_id);
        msg.s_header.s_payload_len = strlen(msg.s_payload);
        send_multicast(&local, &msg);
        done();
    }

    // recieve DONE message
    for (size_t i = 1; i <= num_children; i++) {
        Message msg;
        if (i == local.ipc_id) {
            continue;
        }
        receive(&local, i, &msg);
    }
    received_all_done();

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
