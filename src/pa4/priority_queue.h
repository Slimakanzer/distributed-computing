#pragma once
#ifndef __PQUEUE_H
#define __PQUEUE_H

#include "ipc.h"

#define MAX_ELEMENTS 64

typedef struct {
    local_id ipc_id;
    timestamp_t timestamp;
} __attribute__((packed)) PriorityQueueElement;

typedef struct {
    PriorityQueueElement elements[MAX_ELEMENTS];
    int size;
} __attribute__((packed)) PriorityQueue;

PriorityQueue pqueue;

void ppush(PriorityQueueElement element);

PriorityQueueElement ppop();

PriorityQueueElement ppeek();

#endif  // __PQUEUE_H
