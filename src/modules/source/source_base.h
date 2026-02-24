/*
    Todo：零拷贝
    目前完全没有考虑
*/

#ifndef MODULES_SOURCE_SOURCE_BASE_H_
#define MODULES_SOURCE_SOURCE_BASE_H_

#include "core/types/frame_types.h"

namespace modules
{
    namespace source
    {

        using SourceFrame = core::types::SourceFrame;
        using SourceFrameFormat = core::types::SourceFrameFormat;

        class SourceBase
        {
        public:
            virtual ~SourceBase() = default;

            virtual bool Open() = 0;
            virtual void Close() = 0;
            virtual bool Read(SourceFrame *out) = 0;
        };

    } // namespace source
} // namespace modules

#endif // MODULES_SOURCE_SOURCE_BASE_H_
