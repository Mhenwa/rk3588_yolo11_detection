#include "modules/source/file_source.h"

#include <utility>

namespace modules
{
    namespace source
    {

        FileSource::FileSource(std::string path)
            : path_(std::move(path))
        {
        }

        bool FileSource::Open()
        {
            capture_.release();
            return capture_.open(path_) && capture_.isOpened();
        }

        void FileSource::Close()
        {
            capture_.release();
        }

        bool FileSource::Read(SourceFrame *out)
        {
            if (!out)
                return false;
            out->capture_tp = std::chrono::steady_clock::now();
            if (!capture_.read(out->frame))
            {
                return false;
            }
            out->data.clear();
            out->format = SourceFrameFormat::kBgr;
            out->width = out->frame.cols;
            out->height = out->frame.rows;
            return !out->frame.empty();
        }

    } // namespace source
} // namespace modules
