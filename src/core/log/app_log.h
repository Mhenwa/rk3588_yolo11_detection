#ifndef CORE_LOG_APP_LOG_H_
#define CORE_LOG_APP_LOG_H_

#ifdef __cplusplus
extern "C" {
#endif

void init_logging();
void shutdown_logging();

void log_info(const char* fmt, ...);
void log_warn(const char* fmt, ...);
void log_error(const char* fmt, ...);
void log_debug(const char* fmt, ...);

#ifdef __cplusplus
}
#endif

#define LOGI(...) log_info(__VA_ARGS__)
#define LOGW(...) log_warn(__VA_ARGS__)
#define LOGE(...) log_error(__VA_ARGS__)
#define LOGD(...) log_debug(__VA_ARGS__)

#endif  // CORE_LOG_APP_LOG_H_
