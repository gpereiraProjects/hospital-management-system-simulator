#ifndef PATIENT_THREAD_H
#define PATIENT_THREAD_H

#include "command.h"

int start_patient_thread(const command_t *command);
void wait_for_patient_threads(void);

#endif
