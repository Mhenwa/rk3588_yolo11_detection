#ifndef APP_APP_CONTROLLER_H_
#define APP_APP_CONTROLLER_H_

#include <vector>

#include "app/config/app_config.h"
#include "core/types/app_core_types.h"

class AppController
{
public:
    bool Run(const AppConfig &cfg,
             const char *model_path,
             RunReport *report,
             std::vector<SourceRunReport> *source_reports);
};

#endif // APP_APP_CONTROLLER_H_
