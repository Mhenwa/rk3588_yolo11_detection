#ifndef APP_APP_CONTROLLER_H_
#define APP_APP_CONTROLLER_H_

#include <vector>

#include "app/config/app_config.h"
#include "core/types/app_core_types.h"

class AppController
{
public:
    /*
        AppController::Run 方法仅负责协调各阶段的执行流程：
        1) 源数据 + 推理工作器初始化
        2) 启动每个源数据对应的处理线程
        3) 等待线程结束 + 停止工作器
        4) 将各源数据的报告汇总为全局报告
    */
    bool Run(const AppConfig &cfg,
             const char *model_path,
             RunReport *report,
             std::vector<SourceRunReport> *source_reports);
};

#endif // APP_APP_CONTROLLER_H_
