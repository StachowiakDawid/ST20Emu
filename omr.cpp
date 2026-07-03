/* OMR stand for 'Other Machine Registers' */
// There are several other registers which the programmer should know about,
// but which are not part of the process state. See #include file
//  -------------------------------------------

#include <ctime>
#include "omr.h"

OMRSTATE omrState;

// TODO: probably unused
int initTimer(void) {

  // init the fields with the current system time:
  time(&omrState.ClockRegHP);
  time(&omrState.ClockRegLP);
  return (0);
}
