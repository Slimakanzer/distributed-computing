#include "banking.h"
#include "io.h"

void take_max_time_and_inc(IpcLocal *self, timestamp_t time) {
    if (self->local_time < time) {
        self->local_time = time;
    }
    self->local_time++;
}

timestamp_t get_lamport_time(){
    return local.local_time;
}
