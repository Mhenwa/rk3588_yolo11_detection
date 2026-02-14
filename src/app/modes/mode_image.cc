#include "app_modes.h"

#include <chrono>
#include <cstdio>
#include <cstring>

#include "app_log.h"
#include "image_drawing.h"
#include "image_utils.h"

bool run_image_mode(const char* input_arg,
                    rknn_app_context_t* ctx,
                    RunReport* report)
{
    if (!ctx || !report) return false;

    auto pipeline_t0 = std::chrono::steady_clock::now();

    image_buffer_t img;
    memset(&img, 0, sizeof(img));

    if (read_image(input_arg, &img) != 0) {
        LOGE("read image failed\n");
        return false;
    }
    report->input_width = img.width;
    report->input_height = img.height;

    object_detect_result_list results;

    auto t0 = std::chrono::steady_clock::now();
    inference_yolo11_model(ctx, &img, &results);
    auto t1 = std::chrono::steady_clock::now();

    double infer_ms =
        std::chrono::duration<double, std::milli>(t1 - t0).count();

    LOGI("[IMAGE] Inference time: %.2f ms\n", infer_ms);

    char text[256];
    for (int i = 0; i < results.count; i++) {
        auto* det = &results.results[i];
        draw_rectangle(&img,
            det->box.left, det->box.top,
            det->box.right - det->box.left,
            det->box.bottom - det->box.top,
            COLOR_BLUE, 3);

        snprintf(text, sizeof(text), "%s %.1f%%",
            coco_cls_to_name(det->cls_id),
            det->prop * 100);

        draw_text(&img, text,
            det->box.left,
            det->box.top - 20,
            COLOR_RED, 10);
    }

    write_image("out.png", &img);
    auto pipeline_t1 = std::chrono::steady_clock::now();
    free(img.virt_addr);

    double pipeline_ms =
        std::chrono::duration<double, std::milli>(pipeline_t1 - pipeline_t0).count();

    report->total_frames = 1;
    report->total_detections = results.count;
    report->total_infer_ms = infer_ms;
    report->total_pipeline_ms = pipeline_ms;
    report->output_path = "out.png";
    report->ok = true;
    return true;
}
