#ifndef DEFINES_H
#define DEFINES_H

#ifdef __cplusplus
// #pragma once
extern "C" {
#endif

#define TRUE 1
#define FALSE 0

// TODO: remove when constexpr can be moved
#define UNDEFINED_WORD_OLD 0xCCCCCCCC

#define NAME_SIZE 100
#define PARM_SIZE 30
#define MAX_PARMS 50

#ifndef PARMS_H
#define PARMS_H
typedef struct parm_struct {
  int nParms;
  char parameter[PARM_SIZE][MAX_PARMS];
  char value[PARM_SIZE][MAX_PARMS];
} PARMS;
#endif


#ifdef __cplusplus
}
#endif

#endif
