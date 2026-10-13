#ifndef TL_MSG_H
#define TL_MSG_H
/* tl_msg.h — network message formats. Declaration-only (rule 9). */

#include <stdint.h>
#include "tl_types.h"

#ifdef PLATFORM_QNX
  #include <sys/iomsg.h>      /* _IO_BASE, _IO_MAX, _IO_CONNECT */
  #include <sys/neutrino.h>   /* struct _pulse */
#else
  #define _IO_MAX 0x1FF        /* non-QNX build only */
#endif

typedef enum { NODE_I1 = 1, NODE_I2, NODE_X1, NODE_CENTRAL, NODE_DISPLAY } node_id_t;
#define NODE_COUNT (NODE_DISPLAY + 1)   /* arrays indexed by node_id_t; slot 0 unused */

typedef enum { LINK_OFFLINE = 0, LINK_ONLINE } link_t;

typedef enum {
    MSG_STATUS = _IO_MAX + 1,   /* I1/I2  -> central : light change or heartbeat */
    MSG_CMD,                    /* central -> I1/I2  : mode / override / timing request */
    MSG_TRAIN,                  /* X1     -> I1, I2, central : crossing state (also every HEARTBEAT_MS) */
    MSG_SNAPSHOT                /* central -> display : whole-system view */
} msg_type_t;

typedef enum { ST_CHANGE = 1, ST_HEARTBEAT } status_sub_t;
typedef enum { CMD_SET_MODE = 1, CMD_OVERRIDE, CMD_CLEAR_OVERRIDE, CMD_SET_TIMING } cmd_sub_t;
typedef enum { TR_BOOMS_UP = 1, TR_BOOMS_DOWN, TR_BOOM_FAULT } train_sub_t;
typedef enum { RES_OK = 0, RES_REJECTED, RES_BAD_MSG, RES_STALE } result_t;

/* Index into msg_cmd_t.timing_ms[] */
typedef enum {
    TIM_NS_GREEN = 0, TIM_EW_GREEN, TIM_YELLOW, TIM_ALL_RED,
    TIM_EXTEND, TIM_WALK, TIM_PED_CLEAR, TIM_COUNT
} tim_idx_t;

typedef struct {
    uint16_t type;       /* msg_type_t — MUST be first (same position as _IO_CONNECT's type) */
    uint16_t subtype;    /* *_sub_t for this type */
    uint16_t src;        /* node_id_t of the sender */
    uint16_t reserved;
    uint32_t seq;        /* per-sender, increments every message, restarts at 1 */
} msg_header_t;

typedef struct {
    msg_header_t hdr;    /* subtype: ST_CHANGE or ST_HEARTBEAT */
    uint8_t  phase, mode;                 /* phase_t, tl_mode_t */
    uint8_t  ns, ew, ns_ped, ew_ped;      /* colour_t / ped_t */
    uint8_t  train_down, boom_fault;
    uint64_t uptime_ms;
} msg_status_t;

typedef struct {
    msg_header_t hdr;    /* subtype: cmd_sub_t */
    int32_t  value;      /* SET_MODE: tl_mode_t; OVERRIDE: MODE_OVERRIDE_NS / MODE_OVERRIDE_EW */
    uint32_t timing_ms[TIM_COUNT];        /* used by CMD_SET_TIMING */
} msg_cmd_t;

typedef struct {
    msg_header_t hdr;    /* subtype: train_sub_t */
    uint8_t  line;       /* 1 or 2 (two train lines) */
} msg_train_t;

typedef struct {
    msg_header_t hdr;
    uint8_t      link[3];                 /* link_t for I1, I2, X1 */
    msg_status_t i1, i2;
    msg_train_t  x1;
} msg_snapshot_t;

typedef struct {
    uint16_t result;     /* result_t */
    uint16_t src;        /* who is replying */
    uint32_t seq;        /* echoes the seq being answered */
} msg_reply_t;

/* Receive union — each server declares one buffer of this type (rule 5) */
typedef union {
    msg_header_t   hdr;
#ifdef PLATFORM_QNX
    struct _pulse  pulse;
#endif
    msg_status_t   status;
    msg_cmd_t      cmd;
    msg_train_t    train;
    msg_snapshot_t snapshot;
} msg_rx_t;

#endif /* TL_MSG_H */
