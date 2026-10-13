#ifndef RECEIVE_UNION_H
#define RECEIVE_UNION_H

#include <sys/neutrino.h>
#include <sys/iomsg.h>
#include "message.h"

typedef union {
    struct _pulse pulse;
    struct _io_connect io_connect;
    msg_header_t header;
    clear_fault_msg_t clear_fault;
} x1_receive_t;

#endif
