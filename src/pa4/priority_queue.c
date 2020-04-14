#include "priority_queue.h"

void ppush(PriorityQueueElement element)
{
    pqueue.elements[pqueue.size++] = element;
}

int element_comparator(PriorityQueueElement element1, PriorityQueueElement element2)
{
    if (element1.timestamp > element2.timestamp)
        return 1;

    if (element1.timestamp == element2.timestamp)
    {
        if (element1.ipc_id > element2.ipc_id)
            return 1;
        else if (element1.ipc_id == element2.ipc_id)
            return 0;
    }

    return -1;
}

int max_element_idx()
{
    if (pqueue.size <= 0)
        return -1;

    int max_idx = 0;
    for (int i = 1; i < pqueue.size; i++)
    {
        if (element_comparator(pqueue.elements[i], pqueue.elements[max_idx]) > 0)
            max_idx = i;
    }

    return max_idx;
}

PriorityQueueElement ppop()
{
    int max_idx = max_element_idx();

    PriorityQueueElement max_element = pqueue.elements[max_idx];
    pqueue.elements[max_idx] = pqueue.elements[pqueue.size - 1];
    pqueue.size--;

    return max_element;
}

PriorityQueueElement ppeek()
{
    int max_idx = max_element_idx();
    return pqueue.elements[max_idx];
}
