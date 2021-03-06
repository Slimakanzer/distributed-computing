#pragma once
#ifndef __IO_H
#define __IO_H

#include "ipc.h"
#include "banking.h"
#define MAX_PROCESSES 10

typedef enum {
    IPC_STATUS_SUCCESS              = 0,
    IPC_STATUS_ERROR_INVALID_PEER   = 1,
    IPC_STATUS_ERROR_INVALID_MAGIC  = 2,
    IPC_STATUS_ERROR_INVALID_LOCAL  = 3,
    IPC_STATUS_ERROR_CLOSED_PIPE    = 4,
    IPC_STATUS_ERROR_INVALID_READING= 5,
    IPC_STATUS_NOT_IMPLEMENT        = 6,
} ipc_status_t;


typedef struct {
    local_id ipc_id;    
    timestamp_t local_time;
    int done_received;
} __attribute__((packed)) IpcLocal;

IpcLocal local;
size_t num_processes;
int reader[MAX_PROCESSES][MAX_PROCESSES];
int writer[MAX_PROCESSES][MAX_PROCESSES];

int is_deferred[MAX_PROCESSES];
int is_requested[MAX_PROCESSES];

int receive_from_all_children(IpcLocal* self, Message* msg, int max_count_children_proc);
int send_started_to_all(IpcLocal* self);
int send_done_to_all(IpcLocal* self);
int send_stop_to_all(IpcLocal* self);

void take_max_time_and_inc(IpcLocal *self, timestamp_t time);

#endif  // __IO_H
