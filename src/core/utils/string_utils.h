#ifndef CORE_UTILS_STRING_UTILS_H_
#define CORE_UTILS_STRING_UTILS_H_

#include <algorithm>
#include <cctype>
#include <string>

namespace core
{
    namespace utils
    {

        inline std::string to_lower(std::string value)
        {
            std::transform(value.begin(), value.end(), value.begin(),
                           [](unsigned char ch)
                           {
                               return static_cast<char>(std::tolower(ch));
                           });
            return value;
        }

    } // namespace utils
} // namespace core

#endif // CORE_UTILS_STRING_UTILS_H_
