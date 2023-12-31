#include "statemachine.h"
#include "power.h"
#include "pcu_events.h"
#include "bcuCommunication.h"

static wakeup_reason_e wakeup_reason = WAKEUP_REASON_POWER_ON;

wakeup_reason_e statemachine_getWakeupReason(void) {
    return wakeup_reason;
}

/**
 * The general architecture is that every state function is blocking and waits for events to happen.
 * It then reacts to those events and probably transitions to another state.
 */

/**
 * @return STATE_SHUTDOWN_REQUESTED if BCU requests shutdown, otherwise STATE_ACTIVE
 */
int STATE_ACTIVE_state(void) {
    state_codes_e next_state = STATE_ACTIVE;
    sendToBcu("active state");
    eventmask_t evt = chEvtWaitAny(ALL_EVENTS);
    if (evt & EVENT_SHUTDOWN_REQUESTED) {
        next_state = STATE_SHUTDOWN_REQUESTED;
    }
    return next_state;
}

/**
 * @brief state function for Shutdown-Requested state
 * @details runs a timer that will power off all rails
 * @return STATE_DEEP_SLEEP if shutdown timer expires, STATE_ACTIVE if BCU aborts shutdown
 */
int STATE_SHUTDOWN_REQUESTED_state(void) {
    state_codes_e next_state = STATE_DEEP_SLEEP;
    sendToBcu("shutdown_requested state");
    eventmask_t evt = chEvtWaitAnyTimeout(ALL_EVENTS, TIME_MS2I(5000));
    if (evt & (EVENT_SHUTDOWN_ABORTED | EVENT_WAKEUP_REQUESTED_BY_ALARMCLOCK )) {
        next_state = STATE_ACTIVE;
    }
    return next_state;
}

/**
 * @return
 */
int STATE_DEEP_SLEEP_state(void) {
    state_codes_e next_state = STATE_DEEP_SLEEP;
    sendToBcu("deep_sleep state");
    eventmask_t evt = chEvtWaitAny(ALL_EVENTS);
    if (evt & EVENT_WAKEUP_REQUESTED_BY_ALARMCLOCK) {
        wakeup_reason = WAKEUP_REASON_SCHEDULED;
        next_state = STATE_ACTIVE;
    }
    else if (evt & EVENT_WAKEUP_REQUESTED_BY_USER) {
        wakeup_reason = WAKEUP_REASON_USER_REQUEST;
        next_state = STATE_ACTIVE;
    }
    else if (evt & (EVENT_BUTTON_0_PRESSED | EVENT_BUTTON_1_PRESSED)) {
        next_state = STATE_HMI;
    }
    return next_state;
}

int STATE_HMI_state(void) {
    state_codes_e next_state = STATE_DEEP_SLEEP;
    sendToBcu("hmi state");
    eventmask_t evt = chEvtWaitAnyTimeout(ALL_EVENTS, TIME_MS2I(60000));
    if (evt & EVENT_WAKEUP_REQUESTED_BY_ALARMCLOCK) {
        wakeup_reason = WAKEUP_REASON_SCHEDULED;
        next_state = STATE_ACTIVE;
    }
    else if (evt & EVENT_BUTTON_0_PRESSED) {
        sendToBcu("Button 0 pressed");
    }
    else if (evt & EVENT_BUTTON_1_PRESSED) {
        sendToBcu("Button 1 pressed");
    }
    else if (evt & EVENT_WAKEUP_REQUESTED_BY_USER) {
        wakeup_reason = WAKEUP_REASON_USER_REQUEST;
        next_state = STATE_ACTIVE;
    }
    return next_state;
}

/* array and enum must be in sync! */
int (* state[])(void) = {
    STATE_ACTIVE_state,
    STATE_SHUTDOWN_REQUESTED_state,
    STATE_DEEP_SLEEP_state,
    STATE_HMI_state
};

struct transition {
    state_codes_e src_state;
    state_codes_e dst_state;
};

struct transition state_transitions[] = {
        {STATE_ACTIVE, STATE_SHUTDOWN_REQUESTED},
        {STATE_SHUTDOWN_REQUESTED,   STATE_ACTIVE},
        {STATE_SHUTDOWN_REQUESTED,   STATE_DEEP_SLEEP},
        {STATE_DEEP_SLEEP,   STATE_ACTIVE},
        {STATE_DEEP_SLEEP,   STATE_HMI},
        {STATE_HMI,   STATE_DEEP_SLEEP},
        {STATE_HMI,   STATE_ACTIVE}
};

#define EXIT_STATE STATE_DEEP_SLEEP
#define ENTRY_STATE STATE_ACTIVE

#define DIMENSION(x) (uint8_t)(sizeof(x)/sizeof(x[0]))

bool transition_valid(state_codes_e cur_state, state_codes_e desired_state) {
    for (uint8_t i = 0; i < DIMENSION(state_transitions); i++) {
        if (state_transitions[i].src_state == cur_state && state_transitions[i].dst_state == desired_state) {
            return true;
        }
    }
    return false;
}

/**
 * @brief transitions to the next state. If a transition is valid, it moreover conducts actions that need to be done
 * at state leaving and state entry respectively
 * @param desired_state
 * @return current state AFTER function was called. Is the current state in case the transition is invalid
 */
state_codes_e statemachine_transitionToState(state_codes_e current_state, state_codes_e desired_state) {
    if (!transition_valid(current_state, desired_state)) {
        return current_state;
    }

    /* perform actions on leaving a state */
    switch (current_state) {
        case STATE_DEEP_SLEEP:
            power5v();
            break;
        default:
            break;
    }

    /* perform actions on entering a state */
    switch (desired_state) {
        case STATE_DEEP_SLEEP:
            unpowerBcu();
            unpower5v();
            break;

        case STATE_ACTIVE:
            powerBcu();
            break;

        default:
            break;
    }
    return desired_state;
}

/**
 * @brief statemachine entry point
 */
void statemachine_mainloop(void) {
    int (* state_fun)(void);
    state_codes_e current_state = ENTRY_STATE;
    state_codes_e desired_state = ENTRY_STATE;

    power5v();  // necessary to turn it on initially

    while(true) {
        state_fun = state[current_state];
        desired_state = state_fun();
        if (desired_state != current_state) {
            current_state = statemachine_transitionToState(current_state, desired_state);
        }
        chThdSleepMilliseconds(100);
    }
}

thread_t *uart_thread;

static THD_WORKING_AREA(waUARTThread, 1024);
static THD_FUNCTION(UARTThread, arg) {
    UNUSED_PARAM(arg);
    while (true) {
        statemachine_mainloop();
    }
}

void statemachine_init(void) {
    uart_thread = chThdCreateStatic(waUARTThread, sizeof(waUARTThread),
                                    NORMALPRIO + 1, UARTThread, NULL);
}

void statemachine_sendEvent(eventmask_t events) {
    chEvtSignal(uart_thread, events);
}

void statemachine_sendEventFromIsr(eventmask_t events) {
    chSysLockFromISR();
    chEvtSignalI(uart_thread, events);
    chSysUnlockFromISR();
}
