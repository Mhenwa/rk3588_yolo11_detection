#ifndef CORE_TYPES_SOURCE_TYPES_H_
#define CORE_TYPES_SOURCE_TYPES_H_

#include <string>

#include "core/types/app_core_types.h"

namespace core
{
    namespace types
    {

        struct SourceOptions
        {
            InputType type = INPUT_VIDEO;
            std::string input;
            int width = 0;
            int height = 0;
            double fps = 30.0;
            std::string format = "auto";
        };

    } // namespace types
} // namespace core

#endif // CORE_TYPES_SOURCE_TYPES_H_
