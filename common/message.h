#ifndef MESSAGE_H
#define MESSAGE_H

#include <stdint.h>
#include <sys/iomsg.h>
#include <sys/neutrino.h>

typedef enum {
    NODE_I1 = 1,
    NODE_I2,
    NODE_X1,
    NODE_CENTRAL,
    NODE_DISPLAY
} node_id_t;

typedef enum {
    MODE_FIXED = 0,
    MODE_SENSOR
} operation_mode_t;

typedef enum {
    LIGHT_RED = 0,
    LIGHT_YELLOW,
    LIGHT_GREEN
} light_state_t;

typedef enum {
    INTERSECTION_NORMAL = 0,
    INTERSECTION_TRAIN_PREEMPT,
    INTERSECTION_FAULT
} intersection_state_t;

typedef enum {
    RAIL_CLEAR = 0,
    RAIL_APPROACHING,
    RAIL_GATES_DOWN,
    RAIL_CROSSING,
    RAIL_FAULT
} railway_state_t;

typedef enum {
    GATE_UP = 0,
    GATE_LOWERING,
    GATE_DOWN,
    GATE_RAISING,
    GATE_UNKNOWN
} gate_state_t;

typedef enum {
    TRAIN_SIGNAL_RED = 0,
    TRAIN_SIGNAL_GREEN
} train_signal_t;

typedef enum {
    MSG_SET_MODE = _IO_MAX + 1,
    MSG_INTERSECTION_STATUS,
    MSG_LIGHT_STATUS,
    MSG_RAILWAY_STATUS,
    MSG_FAULT_STATUS,
    MSG_SYSTEM_STATUS,
    MSG_CLEAR_FAULT
} app_msg_type_t;

typedef enum {
    SUBTYPE_NONE = 0,
    SUBTYPE_STATUS_UPDATE,
    SUBTYPE_FAULT_REPORT,
    SUBTYPE_OPERATOR_CLEAR
} app_msg_subtype_t;

typedef struct {
    uint16_t type;
    uint16_t subtype;
    uint8_t sender;
    uint8_t reserved[3];
    uint32_t sequence;
} msg_header_t;

typedef enum {
    RESULT_OK = 0,
    RESULT_REJECTED,
    RESULT_INVALID,
    RESULT_BUSY
} result_code_t;

typedef struct {
    int32_t result;
    uint32_t sequence;
} msg_reply_t;

typedef struct {
    msg_header_t header;
    uint8_t railway_state;
    uint8_t gate_state;
    uint8_t warning_lights_on;
    uint8_t train_signal;
    uint32_t fault_flags;
} railway_status_msg_t;

typedef struct {
    msg_header_t header;
    uint32_t fault_flags;
    uint8_t railway_state;
    uint8_t gate_state;
    uint8_t safe_state_applied;
    uint8_t reserved;
} fault_status_msg_t;

typedef struct {
    msg_header_t header;
} clear_fault_msg_t;

typedef enum {
    RAIL_EVENT_APPROACH = 1,
    RAIL_EVENT_CROSSING,
    RAIL_EVENT_CLEAR,
    RAIL_EVENT_FAULT
} railway_event_t;

#define PULSE_RAILWAY_EVENT (_PULSE_CODE_MINAVAIL)

static inline int32_t pack_railway_pulse(node_id_t sender,
                                         railway_event_t event,
                                         uint16_t sequence)
{
    uint32_t value = 0;
    value |= ((uint32_t)sender & 0xFFu) << 24;
    value |= ((uint32_t)event & 0xFFu) << 16;
    value |= (uint32_t)sequence;
    return (int32_t)value;
}

static inline node_id_t unpack_railway_sender(int32_t value)
{
    return (node_id_t)(((uint32_t)value >> 24) & 0xFFu);
}

static inline railway_event_t unpack_railway_event(int32_t value)
{
    return (railway_event_t)(((uint32_t)value >> 16) & 0xFFu);
}

static inline uint16_t unpack_railway_sequence(int32_t value)
{
    return (uint16_t)((uint32_t)value & 0xFFFFu);
}

#define X1_FAULT_NONE          0u
#define X1_FAULT_GATE_TIMEOUT  (1u << 0)
#define X1_FAULT_GATE_SENSOR   (1u << 1)

#endif
