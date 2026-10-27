#ifndef STI5512_PWM_H
#define STI5512_PWM_H

#include "../../core/memory/memory.h"

/* STi5512 PWM and counter module (PWMBaseAddress 0x2000b000, interrupt 11). */
MemoryDevice sti5512PwmDevice();
void resetSti5512Pwm();
/* advances the counters to the current CPU cycle; returns the interrupt line */
bool tickSti5512Pwm();
/* an edge on pin CaptureIn0-3: latches PWM_CaptureCount if PWM_nCaptureEdge selects it */
void sti5512PwmCaptureEdge(int input, bool rising);

#endif
