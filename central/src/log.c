#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "central.h"
#include "console.h"

#define LOG_Q 128

static pthread_mutex_t m = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t console_m = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t cv = PTHREAD_COND_INITIALIZER;
static char q[LOG_Q][LOG_LINE];
static int head;
static int count;
static unsigned dropped;
static FILE *fp;

int log_init(const char *path)
{
    fp = fopen(path, "a");
    return fp ? 0 : -1;
}

void log_msg(const char *fmt, ...)
{
    char line[LOG_LINE];
    char body[LOG_LINE - 16];
    va_list ap;
    time_t t;
    struct tm tm;

    va_start(ap, fmt);
    vsnprintf(body, sizeof body, fmt, ap);
    va_end(ap);

    t = time(NULL);
    localtime_r(&t, &tm);
    snprintf(line, sizeof line, "%02d:%02d:%02d %s",
             tm.tm_hour, tm.tm_min, tm.tm_sec, body);

    pthread_mutex_lock(&console_m);
    if (strncmp(body, "ALARM", 5) == 0)
        printf(UI_RED "[CENTRAL] %s\n" UI_RESET, line);
    else
        printf("[CENTRAL] %s\n", line);
    fflush(stdout);
    pthread_mutex_unlock(&console_m);

    pthread_mutex_lock(&m);
    if (count < LOG_Q) {
        memcpy(q[(head + count) % LOG_Q], line, LOG_LINE);
        count++;
        pthread_cond_signal(&cv);
    } else {
        dropped++;
    }
    pthread_mutex_unlock(&m);
}

void *logger_thread(void *arg)
{
    (void)arg;
    char line[LOG_LINE];

    for (;;) {
        unsigned lost;

        pthread_mutex_lock(&m);
        while (count == 0)
            pthread_cond_wait(&cv, &m);

        memcpy(line, q[head], LOG_LINE);
        head = (head + 1) % LOG_Q;
        count--;
        lost = dropped;
        dropped = 0;
        pthread_mutex_unlock(&m);

        if (!fp)
            continue;

        if (lost)
            fprintf(fp, "(%u log lines dropped)\n", lost);
        fprintf(fp, "%s\n", line);
        fflush(fp);
    }

    return NULL;
}
