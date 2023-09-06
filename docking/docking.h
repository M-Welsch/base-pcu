#ifndef BASE_PCU_DOCKING_H
#define BASE_PCU_DOCKING_H

#include "core_defines.h"

#define MAXIMUM_DOCKING_TIME_10MS_TICKS 130

pcu_returncode_e dock(void);
pcu_returncode_e undock(void);

pcu_returncode_e powerHdd(void);
pcu_returncode_e unpowerHdd(void);

pcu_returncode_e powerBcu(void);
pcu_returncode_e unpowerBcu(void);

pcu_dockingstate_e getDockingState(void);

#endif //BASE_PCU_DOCKING_H
