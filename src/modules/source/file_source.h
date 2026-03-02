#ifndef MODULES_SOURCE_FILE_SOURCE_H_
#define MODULES_SOURCE_FILE_SOURCE_H_

#include <string>

#include <opencv2/opencv.hpp>

#include "modules/source/source_base.h"

namespace modules {
namespace source {

class FileSource : public SourceBase {
public:
    explicit FileSource(std::string path);

    bool Open() override;
    void Close() override;
    bool Read(SourceFrame* out) override;

private:
    std::string path_;
    cv::VideoCapture capture_;
};

}  // namespace source
}  // namespace modules

#endif  // MODULES_SOURCE_FILE_SOURCE_H_
