#ifndef APP_UTILS_H_
#define APP_UTILS_H_

#include <csignal>
#include <vector>

#include "core/types/app_core_types.h"

void handle_sigint(int);
void request_stop();
void reset_stop_flag();
bool stop_requested();

void print_usage(const char* prog);
void print_run_report(const RunReport& report,
                      const std::vector<SourceRunReport>* sources);

bool is_number_str(const char* s);

#endif  // APP_UTILS_H_
