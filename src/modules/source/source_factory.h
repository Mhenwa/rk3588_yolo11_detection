#ifndef MODULES_SOURCE_SOURCE_FACTORY_H_
#define MODULES_SOURCE_SOURCE_FACTORY_H_

#include <memory>

#include "core/types/source_types.h"
#include "modules/source/source_base.h"

namespace modules
{
    namespace source
    {

        std::unique_ptr<SourceBase> BuildSource(const core::types::SourceOptions& source);

    } // namespace source
} // namespace modules

#endif // MODULES_SOURCE_SOURCE_FACTORY_H_
