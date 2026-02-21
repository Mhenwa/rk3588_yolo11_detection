#ifndef APP_UTILS_H_
#define APP_UTILS_H_

#include <csignal>
#include <vector>

#include "core/types/app_core_types.h"

/*
    SIGINT信号处理函数
*/
void handle_sigint(int);

/*
    程序主动置停止位结束自身
*/
void request_stop();

/*
    清楚停止状态，防止上次的SIGINT信号影响这次
*/
void reset_stop_flag();

/*
    程序查询是否有终止信号
*/
bool stop_requested();

/*
    打印使用方法
*/
void print_usage(const char* prog);

/*
    运行结束后打印运行报告
*/
void print_run_report(const RunReport& report,
                      const std::vector<SourceRunReport>* sources);

#endif  // APP_UTILS_H_
