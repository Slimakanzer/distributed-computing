#pragma once
#ifndef __IO_H
#define __IO_H

#include "ipc.h"
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

} __attribute__((packed)) IpcLocal;

IpcLocal local;
size_t num_processes;
int reader[MAX_PROCESSES][MAX_PROCESSES];
int writer[MAX_PROCESSES][MAX_PROCESSES];

#endif  // __IO_H
