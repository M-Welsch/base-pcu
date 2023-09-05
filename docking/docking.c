#include "docking.h"
#include "hal.h"
#include "measurement.h"
#include "bcuCommunication.h"
#include "chprintf.h"

static bool timeoutFlag = false;

static void _set_timeoutFlag(GPTDriver *gptp) {
    UNUSED_PARAM(gptp);
    timeoutFlag = true;
}

const GPTConfig tim6_cfg = {
    .frequency = 1000,                  /**< Frequency 1kHz */
    .callback = _set_timeoutFlag,
    .cr2 = 0U,
    .dier = 0U
};

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

static inline pcu_returncode_e _dockingState(uint16_t sense) {
    if (sense > 3000) {
        return pcu_dockingState1_Undocked;
    }
    return pcu_dockingState2_allDockedPwrOff;
}

pcu_returncode_e dock_(void) {
    _motorDocking();
    measurementValues_t values;
    pcu_returncode_e retval = pcuFAIL;
    pcu_returncode_e dockingState = pcu_dockingState1_Undocked;
    uint16_t counter = 1000;
    while (counter) {  // check for timeout here
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
        dockingState = _dockingState(values.stator_supply_sense);
        if (dockingState != pcu_dockingState1_Undocked) {
            break;
        }
        counter--;
        chThdSleepMilliseconds(10);
    }
    _motorBreak();

    if (dockingState == pcu_dockingState2_allDockedPwrOff) {
        retval = pcuSUCCESS;
    }
    else {
        retval = dockingState;
    }

    return retval;
}


pcu_returncode_e dock(void) {
    _motorDocking();
    pcu_returncode_e retval = pcuSUCCESS;
    uint16_t counter = 130;
    pcu_returncode_e dockingState = pcu_dockingState1_Undocked;
    while (counter) {
        measurementValues_t values;
        measurement_getValues(&values);
        dockingState = _dockingState(values.stator_supply_sense);
        if (dockingState != pcu_dockingState1_Undocked) {
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
    uint16_t counter = 130;
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
    return _dockingState(values.stator_supply_sense);
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

