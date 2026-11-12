/* <stdbool.h> for gcc 2.7.2, which has none (busybox's syslogd includes it) */
#ifndef _STDBOOL_H
#define _STDBOOL_H
#define bool	int
#define true	1
#define false	0
#define __bool_true_false_are_defined	1
#endif
