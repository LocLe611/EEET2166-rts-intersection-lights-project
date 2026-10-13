/* log.c — log file writer. Any thread calls log_msg(), which only formats and
 * enqueues; the logger thread is the ONLY one that touches the file, so slow
 * file I/O never delays the RX server or the link threads. */
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "central.h"

#define LOG_Q 128

static pthread_mutex_t m  = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  cv = PTHREAD_COND_INITIALIZER;
static char     q[LOG_Q][LOG_LINE];
static int      head, count;
static unsigned dropped;
static char     recent[LOG_RECENT][LOG_LINE];  /* last few lines for the dashboard */
static int      recent_head, recent_n;
static FILE    *fp;

int log_init(const char *path)
{
    fp = fopen(path, "a");
    return fp ? 0 : -1;
}

void log_msg(const char *fmt, ...)
{
    char line[LOG_LINE], body[LOG_LINE - 16];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(body, sizeof body, fmt, ap);
    va_end(ap);

    time_t t = time(NULL);
    struct tm tm;
    localtime_r(&t, &tm);
    snprintf(line, sizeof line, "%02d:%02d:%02d %s", tm.tm_hour, tm.tm_min, tm.tm_sec, body);

    pthread_mutex_lock(&m);
    if (count < LOG_Q) {
        memcpy(q[(head + count) % LOG_Q], line, LOG_LINE);
        count++;
        pthread_cond_signal(&cv);
    } else {
        dropped++;
    }
    memcpy(recent[recent_head], line, LOG_LINE);
    recent_head = (recent_head + 1) % LOG_RECENT;
    if (recent_n < LOG_RECENT) recent_n++;
    pthread_mutex_unlock(&m);

    table_changed();   /* refresh the events panel */
}

/* Copy the newest lines, oldest first. */
int log_recent(char out[][LOG_LINE], int max)
{
    pthread_mutex_lock(&m);
    int n = recent_n < max ? recent_n : max;
    for (int i = 0; i < n; i++) {
        int idx = (recent_head - n + i + LOG_RECENT) % LOG_RECENT;
        memcpy(out[i], recent[idx], LOG_LINE);
    }
    pthread_mutex_unlock(&m);
    return n;
}

void *logger_thread(void *arg)
{
    (void)arg;
    char line[LOG_LINE];
    for (;;) {
        unsigned lost;
        pthread_mutex_lock(&m);
        while (count == 0) pthread_cond_wait(&cv, &m);
        memcpy(line, q[head], LOG_LINE);
        head = (head + 1) % LOG_Q;
        count--;
        lost = dropped; dropped = 0;
        pthread_mutex_unlock(&m);

        if (!fp) continue;
        if (lost) fprintf(fp, "(%u log lines dropped)\n", lost);
        fprintf(fp, "%s\n", line);
        fflush(fp);
    }
    return NULL;
}
