#ifndef LINE_READER_H
#define LINE_READER_H

#pragma once

#include <string>

namespace cli {

/*
 * Lines from std::cin, read on a background thread so that a free run can check for a stop
 * request without blocking. The thread starts with the first call.
 */

/* waits for the next line; false at the end of input */
bool read_line(std::string &line);
/* removes a pending "g" line (a request to stop the free run); true if there was one */
bool take_stop_request();

} // namespace cli

#endif
