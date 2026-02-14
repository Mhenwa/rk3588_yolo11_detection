#ifndef APP_UTILS_H_
#define APP_UTILS_H_

#include <csignal>
#include <mutex>
#include <string>
#include <vector>

#include "app_core_types.h"

extern volatile sig_atomic_t g_stop;
extern std::mutex g_ui_mutex;

void handle_sigint(int);
void print_usage(const char* prog);
void print_run_report(const RunReport& report,
                      const std::vector<SourceRunReport>* sources);
void show_error_window(const char* window_name, const std::string& message);

bool is_number_str(const char* s);

#endif  // APP_UTILS_H_
