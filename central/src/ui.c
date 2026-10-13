/* ui.c — operator console. Two threads:
 *   dashboard_thread  the ONLY thread that draws. Redraws when the node table changes
 *                     (condition variable) or once a second. Fits an 80x25 console.
 *   menu_thread       reads single keys (raw terminal), drives the menu, queues
 *                     commands. Never prints: it sets the prompt and asks for a redraw.
 * stdout and the prompt text are protected by out_m. */
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>
#include "central.h"

static pthread_mutex_t out_m = PTHREAD_MUTEX_INITIALIZER;
static char prompt[160] = "Select an option.";

/* ---------- enum -> text (switch on names, so enum order never matters) ---------- */
const char *node_str(int n)
{
    switch (n) {
    case NODE_I1: return "I1";  case NODE_I2: return "I2";  case NODE_X1: return "X1";
    case NODE_CENTRAL: return "CENTRAL";  case NODE_DISPLAY: return "DISPLAY";
    default: return "?";
    }
}
const char *side_road(int n) { return n == NODE_I1 ? "R1" : n == NODE_I2 ? "R2" : "?"; }
const char *phase_str(int p)
{
    switch (p) {
    case PHASE_ALL_RED:     return "ALL_RED";
    case PHASE_MAIN_GREEN:  return "MAIN_GREEN";
    case PHASE_MAIN_YELLOW: return "MAIN_YELLOW";
    case PHASE_SIDE_GREEN:  return "SIDE_GREEN";
    case PHASE_SIDE_YELLOW: return "SIDE_YELLOW";
    case PHASE_PED_WALK:    return "PED_WALK";
    case PHASE_PED_CLEAR:   return "PED_CLEAR";
    case PHASE_FAULT:       return "FAULT";
    default:                return "?";
    }
}
const char *light_str(int l)
{
    switch (l) { case LIGHT_RED: return "R"; case LIGHT_YELLOW: return "Y"; case LIGHT_GREEN: return "G"; default: return "?"; }
}
const char *ped_str(int p)
{
    switch (p) { case PED_DONT_WALK: return "DONT"; case PED_WALK: return "WALK"; case PED_FLASHING: return "FLSH"; default: return "?"; }
}
const char *mode_str(int m)
{
    switch (m) { case MODE_FIXED: return "FIX"; case MODE_SENSOR: return "SEN"; default: return "?"; }
}
const char *override_str(int o)
{
    switch (o) { case OVERRIDE_NONE: return "none"; case OVERRIDE_MAIN: return "R3"; case OVERRIDE_SIDE: return "side"; default: return "?"; }
}
const char *istate_str(int s)
{
    switch (s) {
    case INTERSECTION_NORMAL: return "NORM"; case INTERSECTION_TRAIN_PREEMPT: return "TRAIN";
    case INTERSECTION_FAULT: return "FAULT"; default: return "?";
    }
}
const char *rail_str(int r)
{
    switch (r) {
    case RAIL_CLEAR: return "CLEAR"; case RAIL_APPROACHING: return "APPROACHING";
    case RAIL_GATES_DOWN: return "GATES_DOWN"; case RAIL_CROSSING: return "CROSSING";
    case RAIL_FAULT: return "FAULT"; default: return "?";
    }
}
const char *gate_str(int g)
{
    switch (g) {
    case GATE_UP: return "UP"; case GATE_LOWERING: return "LOWERING"; case GATE_DOWN: return "DOWN";
    case GATE_RAISING: return "RAISING"; case GATE_UNKNOWN: return "UNKNOWN"; default: return "?";
    }
}
const char *msgtype_str(int t)
{
    switch (t) { case MSG_SET_MODE: return "MODE"; case MSG_OVERRIDE: return "OVR"; case MSG_CLEAR_FAULT: return "CLR"; default: return "?"; }
}
const char *result_str(int r)
{
    switch (r) {
    case RESULT_OK: return "OK"; case RESULT_REJECTED: return "REJECTED";
    case RESULT_INVALID: return "INVALID"; case RESULT_BUSY: return "BUSY"; default: return "?";
    }
}
static const char *cmdres_str(int r)
{
    switch (r) {
    case CMDRES_PENDING: return "PEND"; case CMDRES_ACK: return "ACK";
    case CMDRES_REJECTED: return "REJ"; case CMDRES_RETRY: return "RETRY";
    case CMDRES_WAIT: return "WAIT"; default: return "";
    }
}

/* ---------- screen building ---------- */
typedef struct { char buf[6144]; size_t len; } sb_t;
static void sb(sb_t *s, const char *fmt, ...)
{
    if (s->len >= sizeof s->buf - 1) return;
    va_list ap;
    va_start(ap, fmt);
    int k = vsnprintf(s->buf + s->len, sizeof s->buf - s->len, fmt, ap);
    va_end(ap);
    if (k < 0) return;
    s->len += (size_t)k;
    if (s->len > sizeof s->buf - 1) s->len = sizeof s->buf - 1;
}
#define EOL   "\033[K\n"            /* clear to end of line, newline */
#define RED   "\033[31m"
#define GREEN "\033[32m"
#define NORM  "\033[0m"

/* "0.4s" / "42s" / "3m" since the node's last message, "-" if never heard from */
static void fmt_seen(char *b, size_t n, const node_info_t *ni, uint64_t now)
{
    if (!ni->have_data) { snprintf(b, n, "-"); return; }
    unsigned long long age = now - ni->last_seen_ms;
    if (age < 10000)       snprintf(b, n, "%llu.%llus", age / 1000, age % 1000 / 100);
    else if (age < 600000) snprintf(b, n, "%llus", age / 1000);
    else                   snprintf(b, n, "%llum", age / 60000);
}

static void fmt_lastcmd(char *b, size_t n, const node_info_t *ni)
{
    if (!ni->last_cmd_seq) { snprintf(b, n, "-"); return; }
    snprintf(b, n, "#%u %s %s", ni->last_cmd_seq, msgtype_str(ni->last_cmd_type), cmdres_str(ni->last_cmd_res));
}

/* Columns (79 wide): Nd Link Seen State Mode Phase R3 Side Ped Request Last-cmd */
static void row_intersection(sb_t *s, int n, const node_info_t *ni, uint64_t now)
{
    char seen[12], last[24], req[12], side[8];
    fmt_seen(seen, sizeof seen, ni, now);
    fmt_lastcmd(last, sizeof last, ni);
    snprintf(req, sizeof req, "%s%s%s", mode_str(ni->desired_mode),
             ni->desired_override == OVERRIDE_NONE ? "" : "+",
             ni->desired_override == OVERRIDE_NONE ? "" : override_str(ni->desired_override));

    if (ni->link == LINK_OFFLINE || !ni->have_data) {
        sb(s, " %-3s " RED "%-7s" NORM " %-5s %-5s %-3s %-11s %-2s %-4s %-4s %-7s %s" EOL,
           node_str(n), ni->have_data ? "OFFLINE" : "NO DATA", seen, "-", "-", "-", "-", "-", "-", req, last);
        return;
    }
    const intersection_status_msg_t *st = &ni->ist;
    snprintf(side, sizeof side, "%s:%s", side_road(n), light_str(st->side_light));
    int fault = st->intersection_state == INTERSECTION_FAULT;
    sb(s, " %-3s " GREEN "%-7s" NORM " %-5s %s%-5s" NORM " %-3s %-11s %-2s %-4s %-4s %-7s %s" EOL,
       node_str(n), "ONLINE", seen, fault ? RED : NORM, istate_str(st->intersection_state),
       mode_str(st->mode), phase_str(st->phase), fault ? "FL" : light_str(st->main_light),
       side, ped_str(st->ped_light), req, last);
}

static void row_x1(sb_t *s, const node_info_t *x, uint64_t now)
{
    char seen[12], last[24];
    fmt_seen(seen, sizeof seen, x, now);
    fmt_lastcmd(last, sizeof last, x);
    if (!x->have_data) {
        sb(s, " X1  " RED "%-7s" NORM " %-5s railway state UNKNOWN            %s" EOL, "NO DATA", seen, last);
        return;
    }
    const railway_status_msg_t *r = &x->rail;
    sb(s, " X1  %s%-7s" NORM " %-5s %s%-11s" NORM " gate %-8s sig %-5s lights %-3s flt 0x%X" EOL,
       x->link == LINK_ONLINE ? GREEN : RED, x->link == LINK_ONLINE ? "ONLINE" : "OFFLINE", seen,
       r->railway_state == RAIL_FAULT ? RED : NORM, rail_str(r->railway_state), gate_str(r->gate_state),
       r->train_signal == TRAIN_SIGNAL_GREEN ? "GREEN" : "RED", r->warning_lights_on ? "ON" : "OFF",
       (unsigned)r->fault_flags);
    sb(s, "     last cmd: %s" EOL, last);
}

static void render(sb_t *s)
{
    node_info_t n[NODE_SLOTS];
    unsigned alarms, m; int sched, pat;
    uint64_t now = now_ms();

    /* Copy under the read lock, format outside it: short critical section. */
    pthread_rwlock_rdlock(&g_c.lock);
    memcpy(n, g_c.node, sizeof n);
    alarms = g_c.alarms; sched = g_c.sched_enabled; m = g_c.sim_min; pat = g_c.pattern;
    pthread_rwlock_unlock(&g_c.lock);

    sb(s, "\033[H");
    sb(s, "=== CENTRAL CONTROL ===  sim %02u:%02u  %-8s  schedule %s" EOL,
       m / 60, m % 60, pattern_str(pat), sched ? "ON" : "OFF");
    sb(s, EOL " %-3s %-7s %-5s %-5s %-3s %-11s %-2s %-4s %-4s %-7s %s" EOL,
       "Nd", "Link", "Seen", "State", "Mod", "Phase", "R3", "Side", "Ped", "Request", "Last cmd");
    row_intersection(s, NODE_I1, &n[NODE_I1], now);
    row_intersection(s, NODE_I2, &n[NODE_I2], now);
    row_x1(s, &n[NODE_X1], now);

    if (alarms) {
        char who[40] = "";
        for (int i = NODE_I1; i <= NODE_X1; i++)
            if (alarms & (1u << i)) { strcat(who, " "); strcat(who, node_str(i)); }
        sb(s, "\033[41;97m *** ALARM: FAULT at%s - [3] clear fault, [a] acknowledge *** " NORM EOL, who);
    } else {
        sb(s, EOL);
    }
    sb(s, "-------------------------------------------------------------------------------" EOL);
    sb(s, " [1] Mode  [2] Override  [3] Clear fault  [4] Schedule on/off  [+] Skip 1 hour" EOL);
    sb(s, " [a] Acknowledge alarm  [q] Quit   (Esc cancels)" EOL);
    sb(s, "-------------------------------------------------------------------------------" EOL);
    sb(s, " Recent events (full log: %s):" EOL, CENTRAL_LOG_PATH);

    char ev[LOG_RECENT][LOG_LINE];
    int k = log_recent(ev, LOG_RECENT);
    for (int i = 0; i < LOG_RECENT; i++) sb(s, "  %.76s" EOL, i < k ? ev[i] : "");   /* never wrap */
    sb(s, EOL);
}

void *dashboard_thread(void *arg)
{
    (void)arg;
    unsigned seen = 0;
    sb_t *s = malloc(sizeof *s);
    if (!s) return NULL;

    pthread_mutex_lock(&out_m);
    fputs("\033[2J", stdout);                 /* clear once; redraws overwrite in place */
    pthread_mutex_unlock(&out_m);

    for (;;) {
        seen = table_wait_change(seen, 1000);
        s->len = 0;
        render(s);
        pthread_mutex_lock(&out_m);
        fwrite(s->buf, 1, s->len, stdout);
        printf(" > %.75s\033[K\033[J", prompt);
        fflush(stdout);
        pthread_mutex_unlock(&out_m);
    }
    return NULL;
}

/* ---------- menu ---------- */
static struct termios saved_tio;
static int raw_on;

static void restore_terminal(void)
{
    if (raw_on) { tcsetattr(STDIN_FILENO, TCSANOW, &saved_tio); raw_on = 0; }
    fputs("\033[0m\n", stdout);
}
static void on_signal(int sig) { restore_terminal(); signal(sig, SIG_DFL); raise(sig); }

static void set_prompt(const char *fmt, ...)
{
    va_list ap;
    pthread_mutex_lock(&out_m);
    va_start(ap, fmt);
    vsnprintf(prompt, sizeof prompt, fmt, ap);
    va_end(ap);
    pthread_mutex_unlock(&out_m);
    table_changed();                          /* ask the dashboard to redraw */
}

/* The two steps every command takes: record what we want (for resync), then queue it. */
static void do_mode(node_id_t n, operation_mode_t mode)
{
    pthread_rwlock_wrlock(&g_c.lock);
    g_c.node[n].desired_mode = mode;
    g_c.node[n].sched_paused = 1;             /* operator takes over this intersection */
    pthread_rwlock_unlock(&g_c.lock);
    log_msg("operator: %s mode %s (schedule paused for %s)", node_str(n),
            mode == MODE_FIXED ? "FIXED" : "SENSOR", node_str(n));
    cmd_t c;
    build_set_mode(&c, mode);
    cmdq_push(n, &c);
}

static void do_override(node_id_t n, override_t ovr)
{
    pthread_rwlock_wrlock(&g_c.lock);
    g_c.node[n].desired_override = ovr;
    pthread_rwlock_unlock(&g_c.lock);
    log_msg("operator: %s override %s", node_str(n), override_str(ovr));
    cmd_t c;
    build_override(&c, ovr);
    cmdq_push(n, &c);
}

static void do_clear_fault(node_id_t n)
{
    log_msg("operator: clear fault at %s", node_str(n));
    cmd_t c;
    build_clear_fault(&c);
    cmdq_push(n, &c);
}

enum { M_TOP, M_NODE, M_MODE, M_OVR, M_FAULT };
enum { A_MODE, A_OVR };

void *menu_thread(void *arg)
{
    (void)arg;
    if (tcgetattr(STDIN_FILENO, &saved_tio) == 0) {
        struct termios raw = saved_tio;
        raw.c_lflag &= ~(ICANON | ECHO);      /* one key at a time; keep OPOST so \n works */
        raw.c_cc[VMIN] = 1; raw.c_cc[VTIME] = 0;
        if (tcsetattr(STDIN_FILENO, TCSANOW, &raw) == 0) raw_on = 1;
    }
    atexit(restore_terminal);
    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);

    int state = M_TOP, action = A_MODE, mask = 0;
    static const char *pick = "Which intersection? [1] I1  [2] I2  [b] both";

    for (;;) {
        unsigned char ch;
        ssize_t r = read(STDIN_FILENO, &ch, 1);
        if (r == 0) {                          /* no keyboard (e.g. run in background) */
            log_msg("menu: stdin closed - operator menu disabled, monitoring continues");
            for (;;) pause();                  /* block for good; don't spin */
        }
        if (r != 1) continue;
        if (ch == 27) { state = M_TOP; set_prompt("Cancelled."); continue; }   /* Esc */

        switch (state) {
        case M_TOP:
            switch (ch) {
            case '1': action = A_MODE; state = M_NODE; set_prompt("Set mode. %s", pick); break;
            case '2': action = A_OVR;  state = M_NODE; set_prompt("Override. %s", pick); break;
            case '3': state = M_FAULT; set_prompt("Clear fault at? [1] I1  [2] I2  [x] X1"); break;
            case '4': {
                int on;
                pthread_rwlock_wrlock(&g_c.lock);
                on = g_c.sched_enabled = !g_c.sched_enabled;
                if (on) {                      /* resume: re-apply the pattern on the next tick */
                    g_c.pattern = -1;
                    g_c.node[NODE_I1].sched_paused = g_c.node[NODE_I2].sched_paused = 0;
                }
                pthread_rwlock_unlock(&g_c.lock);
                log_msg("operator: schedule %s", on ? "ON (manual pauses cleared)" : "OFF");
                set_prompt("Schedule %s.", on ? "ON" : "OFF");
                break;
            }
            case '+':
                pthread_rwlock_wrlock(&g_c.lock);
                g_c.sim_min = (g_c.sim_min + 60) % (24 * 60);
                pthread_rwlock_unlock(&g_c.lock);
                set_prompt("Skipped 1 sim hour.");
                break;
            case 'a': case 'A':
                pthread_rwlock_wrlock(&g_c.lock);
                g_c.alarms = 0;
                pthread_rwlock_unlock(&g_c.lock);
                log_msg("operator: alarm acknowledged");
                set_prompt("Alarm acknowledged.");
                break;
            case 'q': case 'Q':
                log_msg("operator: central shutting down");
                exit(0);                       /* atexit restores the terminal */
            default: break;
            }
            break;

        case M_NODE:
            if      (ch == '1') mask = 1;
            else if (ch == '2') mask = 2;
            else if (ch == 'b' || ch == 'B') mask = 3;
            else break;
            if (action == A_MODE) { state = M_MODE; set_prompt("Mode? [f] FIXED  [s] SENSOR"); }
            else { state = M_OVR; set_prompt("Green to? [m] R3 (main)  [s] side road  [n] none (clear)"); }
            break;

        case M_MODE:
            if (ch != 'f' && ch != 's') break;
            if (mask & 1) do_mode(NODE_I1, ch == 'f' ? MODE_FIXED : MODE_SENSOR);
            if (mask & 2) do_mode(NODE_I2, ch == 'f' ? MODE_FIXED : MODE_SENSOR);
            state = M_TOP; set_prompt("Mode change sent (applies at the next all-red).");
            break;

        case M_OVR: {
            override_t o;
            if      (ch == 'm') o = OVERRIDE_MAIN;
            else if (ch == 's') o = OVERRIDE_SIDE;
            else if (ch == 'n') o = OVERRIDE_NONE;
            else break;
            if (mask & 1) do_override(NODE_I1, o);
            if (mask & 2) do_override(NODE_I2, o);
            state = M_TOP;
            set_prompt(o == OVERRIDE_NONE ? "Override cleared." : "Override sent - stays until [2] ... [n].");
            break;
        }

        case M_FAULT:
            if      (ch == '1') do_clear_fault(NODE_I1);
            else if (ch == '2') do_clear_fault(NODE_I2);
            else if (ch == 'x' || ch == 'X') do_clear_fault(NODE_X1);
            else break;
            state = M_TOP; set_prompt("Clear-fault request sent.");
            break;
        }
    }
    return NULL;
}