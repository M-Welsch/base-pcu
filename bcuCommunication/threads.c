#include "ch.h"
#include "hal.h"
#include "usbcfg.h"
#include "chprintf.h"
#include "threads.h"

/** @brief mailbox for messages to BaSe BCU */
mailbox_t bcu_comm_mb;
msg_t bcu_comm_mb_buffer[BCU_COMM_MB_BUFFER_SIZE];


static void _bcuCommunicationOutputMainloop(BaseSequentialStream *stream) {
  msg_t msg = 0;
  chMBFetchTimeout(&bcu_comm_mb, &msg, TIME_INFINITE);
  const char *s_msg = (const char *)msg;
  chprintf(stream, "%s\n", s_msg);
}

static THD_WORKING_AREA(bcuCommunicationOutputThread, 128);
static THD_FUNCTION(bcuCommunicationOutput, arg) {
  (void) arg;
  chRegSetThreadName("BCU Communication Output Thread");
  BaseSequentialStream *stream = (BaseSequentialStream *) &SDU1;

  while (true) {
    _bcuCommunicationOutputMainloop(stream);
  }
}


static void _bcuCommunicationInputMainloop(void) {
  char buffer[BCU_COMM_INPUT_BUFFER_SIZE];
  uint32_t bytes = chnReadTimeout(&SDU1, (uint8_t *) buffer, BCU_COMM_INPUT_BUFFER_SIZE, TIME_INFINITE); /* hint: https://forum.chibios.org/viewtopic.php?t=824#p8071 */
  if (bytes != 0) {
    chMBPostTimeout(&bcu_comm_mb, (msg_t) buffer, TIME_MS2I(200));
  }
}

static THD_WORKING_AREA(bcuCommunicationInputThread, 128);
static THD_FUNCTION(bcuCommunicationInput, arg) {
  (void) arg;
  chRegSetThreadName("BCU Communication Input Thread");
  while (true) {
    _bcuCommunicationInputMainloop();
  }
}

void _initializeMailbox(void) {
  chMBObjectInit(&bcu_comm_mb, bcu_comm_mb_buffer, BCU_COMM_MB_BUFFER_SIZE);
}

void bcuCommunicationThreads_init(void) {
  _initializeMailbox();
  chThdCreateStatic(bcuCommunicationOutputThread, sizeof(bcuCommunicationOutputThread), NORMALPRIO, bcuCommunicationOutput, NULL);
  // this thread crashes the program
  //chThdCreateStatic(bcuCommunicationInputThread, sizeof(bcuCommunicationInputThread), NORMALPRIO, bcuCommunicationInput, NULL);
}
