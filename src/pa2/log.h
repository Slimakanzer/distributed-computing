#pragma once
#ifndef __LOG_H
#define __LOG_H

#include <stdio.h>

static const char * const error_open_file_fmt =
    "Error: cannot open file %s\n";
static const char * const error_log_message_fmt =
    "Error: cannot log to the invalid file message \"%s\"";

static FILE *events_log_file;
static FILE *pipes_log_file;

void flog_argsfmt(FILE* file, const char *format, va_list args);

void flogfmt(FILE* file, const char *format, ...);

void log_argsfmt(const char *format, va_list args);

void logfmt(const char *format, ...);

void log_errorfmt(const char *format, ...);

void log_events(const char *format, ...);

void log_pipes(const char *format, ...);

void log_init();

void started();

void done();

void received_all_started();

void received_all_done();

void pipe_opened(int from, int to);

void log_end();

#endif  // __LOG_H
