#include <sys/types.h>
#include <unistd.h>

#include "log.h"
#include "common.h"
#include "pa2345.h"
#include "io.h"

void flog_argsfmt(FILE* file, const char *format, va_list args) {
    if (file == NULL) {
        fprintf(stderr, error_log_message_fmt, format);
        return;
    }

    vfprintf(file, format, args);
}

void flogfmt(FILE* file, const char *format, ...) {
    va_list args;

    va_start(args, format);
    flog_argsfmt(file, format, args);
    va_end(args);
}

void log_argsfmt(const char *format, va_list args) {
    vprintf(format, args);
}

void logfmt(const char *format, ...) {
    va_list args;

    va_start(args, format);
    log_argsfmt(format, args);
    va_end(args);
}

void log_errorfmt(const char *format, ...) {
    va_list args;

    va_start(args, format);
    fprintf(stderr, format, args);
    va_end(args);
}

void log_events(const char *format, ...) {
    va_list args;

    va_start(args, format);
    log_argsfmt(format, args);
    flog_argsfmt(events_log_file, format, args);
    va_end(args);
}

void log_pipes(const char *format, ...) {
    va_list args;

    va_start(args, format);
    log_argsfmt(format, args);
    flog_argsfmt(pipes_log_file, format, args);
    va_end(args);
}

void log_init() {
    if ((events_log_file = fopen(events_log, "w")) == NULL)
        log_errorfmt(error_open_file_fmt, events_log);

    if ((pipes_log_file = fopen(pipes_log, "w")) == NULL)
        log_errorfmt(error_open_file_fmt, pipes_log);
}

void started() {
    pid_t pid = getpid();
    pid_t parent_pid = getppid();
    log_events(log_started_fmt, local, pid, parent_pid);
}

void done() {
    log_events(log_done_fmt, local.ipc_id);
}

void received_all_started() {
    log_events(log_received_all_started_fmt, local.ipc_id);
}

void received_all_done() {
    log_events(log_received_all_done_fmt, local.ipc_id);
}

void pipe_opened(int from, int to) {
    log_pipes("Pipe from process %1d to %1d OPENED\n", from, to);
}

void log_end() {
    fclose(events_log_file);
    fclose(pipes_log_file);
}
