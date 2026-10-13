#include "communication.h"
#include "config.h"

#include <sys/dispatch.h>
#include <sys/neutrino.h>
#include <time.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

static uint16_t next_pulse_sequence(x1_communication_t *comm)
{
    comm->pulse_sequence++;

    if (comm->pulse_sequence == 0) {
        comm->pulse_sequence++;
    }

    return comm->pulse_sequence;
}

static uint32_t next_message_sequence(x1_communication_t *comm)
{
    comm->message_sequence++;

    if (comm->message_sequence == 0) {
        comm->message_sequence++;
    }

    return comm->message_sequence;
}

static int ensure_connection(int *coid,
                             const char *service_name,
                             const char *hostname)
{
    if (*coid != -1) {
        return *coid;
    }

    *coid = name_open(service_name, NAME_FLAG_ATTACH_GLOBAL);

    if (*coid == -1) {
        fprintf(stderr,
                "[COMMS] Cannot connect to %s (service %s): %s\n",
                hostname, service_name, strerror(errno));
    } else {
        printf("[COMMS] Connected to %s service '%s'.\n",
               hostname, service_name);
    }

    return *coid;
}

static int send_pulse_to(int *coid,
                         const char *service_name,
                         const char *hostname,
                         railway_event_t event,
                         uint16_t sequence)
{
    int32_t value;

    if (ensure_connection(coid, service_name, hostname) == -1) {
        return -1;
    }

    value = pack_railway_pulse(NODE_X1, event, sequence);

    if (MsgSendPulse(*coid, -1, PULSE_RAILWAY_EVENT, value) == -1) {
        fprintf(stderr,
                "[COMMS] Pulse to %s failed: %s\n",
                hostname, strerror(errno));

        name_close(*coid);
        *coid = -1;

        return -1;
    }

    return 0;
}

static int send_message_to_central(x1_communication_t *comm,
                                   const void *message,
                                   int message_size,
                                   uint32_t sequence)
{
    msg_reply_t reply;
    uint64_t timeout;
    int rc;

    if (ensure_connection(&comm->central_coid,
                          CENTRAL_SERVICE,
                          CENTRAL_HOSTNAME) == -1) {
        return -1;
    }

    memset(&reply, 0, sizeof(reply));

    timeout = (uint64_t)IPC_SEND_TIMEOUT_MS * 1000000ULL;

    /*
     * Prevent a failed Central node from blocking X1 forever.
     * The timeout applies to the immediately following MsgSend().
     */
    if (TimerTimeout(CLOCK_MONOTONIC,
                     _NTO_TIMEOUT_SEND | _NTO_TIMEOUT_REPLY,
                     NULL,
                     &timeout,
                     NULL) == -1) {
        fprintf(stderr,
                "[COMMS] TimerTimeout setup failed: %s\n",
                strerror(errno));
    }

    rc = MsgSend(comm->central_coid,
                 message,
                 message_size,
                 &reply,
                 sizeof(reply));

    if (rc == -1) {
        fprintf(stderr,
                "[COMMS] Message to Central failed: %s\n",
                strerror(errno));

        name_close(comm->central_coid);
        comm->central_coid = -1;

        return -1;
    }

    if (reply.result != RESULT_OK) {
        fprintf(stderr,
                "[COMMS] Central rejected sequence %u (result=%d).\n",
                sequence, reply.result);

        return -1;
    }

    return 0;
}

void x1_communication_init(x1_communication_t *comm)
{
    if (comm == NULL) {
        return;
    }

    comm->i1_coid = -1;
    comm->i2_coid = -1;
    comm->central_coid = -1;
    comm->pulse_sequence = 0;
    comm->message_sequence = 0;
}

void x1_communication_close(x1_communication_t *comm)
{
    if (comm == NULL) {
        return;
    }

    if (comm->i1_coid != -1) {
        name_close(comm->i1_coid);
        comm->i1_coid = -1;
    }

    if (comm->i2_coid != -1) {
        name_close(comm->i2_coid);
        comm->i2_coid = -1;
    }

    if (comm->central_coid != -1) {
        name_close(comm->central_coid);
        comm->central_coid = -1;
    }
}

int x1_send_railway_event(x1_communication_t *comm,
                          railway_event_t event)
{
    uint16_t sequence;
    int i1_ok;
    int i2_ok;

    if (comm == NULL) {
        return -1;
    }

    sequence = next_pulse_sequence(comm);

    i1_ok = send_pulse_to(&comm->i1_coid,
                          I1_SERVICE,
                          I1_HOSTNAME,
                          event,
                          sequence);

    i2_ok = send_pulse_to(&comm->i2_coid,
                          I2_SERVICE,
                          I2_HOSTNAME,
                          event,
                          sequence);

    printf("[COMMS] Railway event %u seq=%u -> I1:%s I2:%s\n",
           (unsigned)event,
           (unsigned)sequence,
           i1_ok == 0 ? "OK" : "FAIL",
           i2_ok == 0 ? "OK" : "FAIL");

    return (i1_ok == 0 && i2_ok == 0) ? 0 : -1;
}

int x1_send_railway_status(x1_communication_t *comm,
                           railway_state_t railway_state,
                           gate_state_t gate_state,
                           int warning_lights_on,
                           train_signal_t train_signal,
                           uint32_t fault_flags)
{
    railway_status_msg_t message;
    uint32_t sequence;
    int rc;

    if (comm == NULL) {
        return -1;
    }

    memset(&message, 0, sizeof(message));

    sequence = next_message_sequence(comm);

    message.header.type = MSG_RAILWAY_STATUS;
    message.header.subtype = SUBTYPE_STATUS_UPDATE;
    message.header.sender = NODE_X1;
    message.header.sequence = sequence;

    message.railway_state = (uint8_t)railway_state;
    message.gate_state = (uint8_t)gate_state;
    message.warning_lights_on = warning_lights_on ? 1u : 0u;
    message.train_signal = (uint8_t)train_signal;
    message.fault_flags = fault_flags;

    rc = send_message_to_central(comm,
                                 &message,
                                 sizeof(message),
                                 sequence);

    printf("[COMMS] RAILWAY_STATUS seq=%u -> Central: %s\n",
           (unsigned)sequence,
           rc == 0 ? "OK" : "FAIL");

    return rc;
}

int x1_send_fault_status(x1_communication_t *comm,
                         uint32_t fault_flags,
                         railway_state_t railway_state,
                         gate_state_t gate_state)
{
    fault_status_msg_t message;
    uint32_t sequence;
    int rc;

    if (comm == NULL) {
        return -1;
    }

    memset(&message, 0, sizeof(message));

    sequence = next_message_sequence(comm);

    message.header.type = MSG_FAULT_STATUS;
    message.header.subtype = SUBTYPE_FAULT_REPORT;
    message.header.sender = NODE_X1;
    message.header.sequence = sequence;

    message.fault_flags = fault_flags;
    message.railway_state = (uint8_t)railway_state;
    message.gate_state = (uint8_t)gate_state;
    message.safe_state_applied = 1u;

    rc = send_message_to_central(comm,
                                 &message,
                                 sizeof(message),
                                 sequence);

    printf("[COMMS] FAULT_STATUS seq=%u -> Central: %s\n",
           (unsigned)sequence,
           rc == 0 ? "OK" : "FAIL");

    return rc;
}
