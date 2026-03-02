#ifndef MODULES_SOURCE_USB_CAM_SOURCE_H_
#define MODULES_SOURCE_USB_CAM_SOURCE_H_

#include <memory>
#include <string>

#include "modules/source/source_base.h"

namespace modules {
namespace source {

class UsbV4L2Camera;

class UsbCamSource : public SourceBase {
public:
    UsbCamSource(std::string device,
                 int width,
                 int height,
                 double fps,
                 std::string format);
    ~UsbCamSource() override;

    bool Open() override;
    void Close() override;
    bool Read(SourceFrame* out) override;

private:
    std::string device_;
    int width_;
    int height_;
    double fps_;
    std::string format_;
    std::unique_ptr<UsbV4L2Camera> camera_;
};

}  // namespace source
}  // namespace modules

#endif  // MODULES_SOURCE_USB_CAM_SOURCE_H_
