#include "docking.h"
#include "hal.h"
#include "measurement.h"
#include "bcuCommunication.h"
#include "chprintf.h"

static bool timeoutFlag = false;
static pcu_dockingstate_e dockingState = pcu_dockingState1_undocked;
static float currentLog[256];

static void _set_timeoutFlag(GPTDriver *gptp) {
    UNUSED_PARAM(gptp);
    timeoutFlag = true;
}

static inline void _motorDocking(void) {
    palSetLine(LINE_MOTOR_DRV1);
    palClearLine(LINE_MOTOR_DRV2);
}

static inline void _motorUndocking(void) {
    palClearLine(LINE_MOTOR_DRV1);
    palSetLine(LINE_MOTOR_DRV2);
}

static inline void _motorBreak(void) {
    palClearLine(LINE_MOTOR_DRV1);
    palClearLine(LINE_MOTOR_DRV2);
}

static inline bool _motorOvercurrent(uint16_t motor_current_measurement_value) {
    return motor_current_measurement_value > 250;
}

static inline pcu_dockingstate_e _dockingState(uint16_t sense) {
    if (sense > 3300) {
        if (measurement_getEndswitch() == PRESSED) {
            return pcu_dockingState1_undocked;
        }
        else {
            return pcu_dockingState9_inbetween;
        }
    }
    else if (sense > 3000) {
        return pcu_dockingState3_allDockedPwrOn;
    }
    else if (sense > 2500) {
        return pcu_dockingState4_allDocked12vOn;
    }
    else if (sense > 1700) {
        return pcu_dockingState5_allDocked5vOn;
    }
    else if (sense < 1100) {
        return pcu_dockingState2_allDockedPwrOff;
    }
    else {
        return pcu_dockingState_unknown;
    }
}

static void _updateDockingState(float *currentValue) {
    measurementValues_t values;
    measurement_getValues(&values);
    if (currentValue != NULL) {
        *currentValue = values.stator_supply_sense;
    }
    dockingState = _dockingState(values.stator_supply_sense);
}


pcu_returncode_e dock(void) {
    _motorDocking();
    pcu_returncode_e retval = pcuSUCCESS;
    uint16_t counter = MAXIMUM_DOCKING_TIME_10MS_TICKS;
    while (counter) {
        measurementValues_t values;
        measurement_getValues(&values);
        dockingState = _dockingState(values.stator_supply_sense);
        if ((dockingState != pcu_dockingState1_undocked) && (dockingState != pcu_dockingState9_inbetween)) {
            break;
        }
        static char buffer[32];
        chsnprintf(buffer, 32, "i=%i", values.imotor_prot);
        sendToBcu(buffer);
        static uint16_t overcurrent_count = 0;
        if (_motorOvercurrent(values.imotor_prot)) {
            overcurrent_count++;
            if (overcurrent_count > 3) {
                retval = pcuOVERCURRENT;
                break;
            }
        }
        else {
            overcurrent_count = 0;
        }
        chThdSleepMilliseconds(10);
        counter--;
    }
    _motorBreak();
    if (!counter) {
        return pcuTIMEOUT;
    }
    return retval;
}

pcu_returncode_e undock(void) {
    _motorUndocking();
    pcu_returncode_e retval = pcuSUCCESS;
    uint16_t counter = MAXIMUM_DOCKING_TIME_10MS_TICKS;
    while (counter) {
        if (measurement_getEndswitch() == PRESSED) {
            break;
        }
        measurementValues_t values;
        measurement_getValues(&values);
        static char buffer[32];
        chsnprintf(buffer, 32, "i=%i", values.imotor_prot);
        sendToBcu(buffer);
        static uint16_t overcurrent_count = 0;
        if (_motorOvercurrent(values.imotor_prot)) {
            overcurrent_count++;
            if (overcurrent_count > 3) {
                retval = pcuOVERCURRENT;
                break;
            }
        }
        else {
            overcurrent_count = 0;
        }
        chThdSleepMilliseconds(10);
        counter--;
    }
    _motorBreak();
    if (!counter) {
        return pcuTIMEOUT;
    }
    return retval;
}

pcu_returncode_e powerHdd(void) {
    palSetLine(LINE_SW_HDD_ON);
    chThdSleepMilliseconds(100);
    palClearLine(LINE_SW_HDD_ON);
    measurementValues_t values;
    measurement_getValues(&values);
    dockingState = _dockingState(values.stator_supply_sense);
    if (dockingState == pcu_dockingState3_allDockedPwrOn) {
        return pcuSUCCESS;
    }
    else {
        return pcuFAIL;
    }
}

pcu_returncode_e unpowerHdd(void) {
    palSetLine(LINE_SW_HDD_OFF);
    chThdSleepMilliseconds(100);
    palClearLine(LINE_SW_HDD_OFF);
    measurementValues_t values;
    measurement_getValues(&values);
    return _dockingState(values.stator_supply_sense);
}

pcu_returncode_e powerBcu(void) {
    palSetLine(LINE_SW_SBC_ON);
    chThdSleepMilliseconds(100);
    palClearLine(LINE_SW_SBC_ON);
    return pcuSUCCESS;
}

pcu_returncode_e unpowerBcu(void) {
    palSetLine(LINE_SW_SBC_OFF);
    chThdSleepMilliseconds(100);
    palClearLine(LINE_SW_SBC_OFF);
    return pcuSUCCESS;
}

pcu_dockingstate_e getDockingState(void) {
    _updateDockingState(NULL);
    return dockingState;
}
