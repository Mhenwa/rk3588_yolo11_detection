#ifndef MODULES_SOURCE_SOURCE_FACTORY_H_
#define MODULES_SOURCE_SOURCE_FACTORY_H_

#include <memory>
#include <string>

#include "app/config/app_config.h"
#include "modules/source/source_base.h"

namespace modules
{
    namespace source
    {

        std::unique_ptr<SourceBase> BuildSource(const SourceConfig &source);

    } // namespace source
} // namespace modules

#endif // MODULES_SOURCE_SOURCE_FACTORY_H_
