/*
    Todo OpenGL ES
*/

#include "modules/postprocess/postprocess_node.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <algorithm>
#include <set>
#include <vector>

#include "core/log/app_log.h"

static char *labels[OBJ_CLASS_NUM];
static char kNullLabel[] = "null";

inline static int clamp(float val, int min, int max)
{
    return val > min ? (val < max ? val : max) : min;
}

static char *readLine(FILE *fp, char *buffer, int *len)
{
    int ch;
    int i = 0;
    size_t buff_len = 0;

    buffer = static_cast<char *>(malloc(buff_len + 1));
    if (!buffer)
        return NULL;

    while ((ch = fgetc(fp)) != '\n' && ch != EOF)
    {
        buff_len++;
        void *tmp = realloc(buffer, buff_len + 1);
        if (tmp == NULL)
        {
            free(buffer);
            return NULL;
        }
        buffer = static_cast<char *>(tmp);
        buffer[i++] = static_cast<char>(ch);
    }
    buffer[i] = '\0';
    *len = static_cast<int>(buff_len);

    if (ch == EOF && (i == 0 || ferror(fp)))
    {
        free(buffer);
        return NULL;
    }
    return buffer;
}

static int readLines(const char *fileName, char *lines[], int max_line)
{
    FILE *file = fopen(fileName, "r");
    char *s = NULL;
    int i = 0;
    int n = 0;

    if (file == NULL)
    {
        LOGE("Open %s fail!\n", fileName);
        return -1;
    }

    while ((s = readLine(file, s, &n)) != NULL)
    {
        lines[i++] = s;
        if (i >= max_line)
            break;
    }
    fclose(file);
    return i;
}

static int loadLabelName(const char *locationFilename, char *label[])
{
    LOGI("load label %s\n", locationFilename);
    return readLines(locationFilename, label, OBJ_CLASS_NUM);
}

static float CalculateOverlap(float xmin0, float ymin0, float xmax0, float ymax0,
                              float xmin1, float ymin1, float xmax1, float ymax1)
{
    const float w = fmax(0.f, fmin(xmax0, xmax1) - fmax(xmin0, xmin1) + 1.0f);
    const float h = fmax(0.f, fmin(ymax0, ymax1) - fmax(ymin0, ymin1) + 1.0f);
    const float i = w * h;
    const float u = (xmax0 - xmin0 + 1.0f) * (ymax0 - ymin0 + 1.0f) +
                    (xmax1 - xmin1 + 1.0f) * (ymax1 - ymin1 + 1.0f) - i;
    return u <= 0.f ? 0.f : (i / u);
}

static int nms(int validCount,
               std::vector<float> &outputLocations,
               const std::vector<int> &classIds,
               std::vector<int> &order,
               int filterId,
               float threshold)
{
    for (int i = 0; i < validCount; ++i)
    {
        const int n = order[i];
        if (n == -1 || classIds[n] != filterId)
            continue;
        for (int j = i + 1; j < validCount; ++j)
        {
            const int m = order[j];
            if (m == -1 || classIds[m] != filterId)
                continue;

            const float xmin0 = outputLocations[n * 4 + 0];
            const float ymin0 = outputLocations[n * 4 + 1];
            const float xmax0 = outputLocations[n * 4 + 0] + outputLocations[n * 4 + 2];
            const float ymax0 = outputLocations[n * 4 + 1] + outputLocations[n * 4 + 3];

            const float xmin1 = outputLocations[m * 4 + 0];
            const float ymin1 = outputLocations[m * 4 + 1];
            const float xmax1 = outputLocations[m * 4 + 0] + outputLocations[m * 4 + 2];
            const float ymax1 = outputLocations[m * 4 + 1] + outputLocations[m * 4 + 3];

            const float iou = CalculateOverlap(
                xmin0, ymin0, xmax0, ymax0, xmin1, ymin1, xmax1, ymax1);
            if (iou > threshold)
            {
                order[j] = -1;
            }
        }
    }
    return 0;
}

static int quick_sort_indice_inverse(std::vector<float> &input, int left, int right,
                                     std::vector<int> &indices)
{
    float key;
    int key_index;
    int low = left;
    int high = right;
    if (left < right)
    {
        key_index = indices[left];
        key = input[left];
        while (low < high)
        {
            while (low < high && input[high] <= key)
                high--;
            input[low] = input[high];
            indices[low] = indices[high];
            while (low < high && input[low] >= key)
                low++;
            input[high] = input[low];
            indices[high] = indices[low];
        }
        input[low] = key;
        indices[low] = key_index;
        quick_sort_indice_inverse(input, left, low - 1, indices);
        quick_sort_indice_inverse(input, low + 1, right, indices);
    }
    return low;
}

inline static int32_t clip_to_i8(float val, float min, float max)
{
    const float f = val <= min ? min : (val >= max ? max : val);
    return static_cast<int32_t>(f);
}

static int8_t qnt_f32_to_affine(float f32, int32_t zp, float scale)
{
    const float dst_val = (f32 / scale) + zp;
    return static_cast<int8_t>(clip_to_i8(dst_val, -128, 127));
}

static float deqnt_affine_to_f32(int8_t qnt, int32_t zp, float scale)
{
    return (static_cast<float>(qnt) - static_cast<float>(zp)) * scale;
}

static void compute_dfl(float *tensor, int dfl_len, float *box)
{
    for (int b = 0; b < 4; b++)
    {
        float exp_t[dfl_len];
        float exp_sum = 0.f;
        float acc_sum = 0.f;
        for (int i = 0; i < dfl_len; i++)
        {
            exp_t[i] = exp(tensor[i + b * dfl_len]);
            exp_sum += exp_t[i];
        }
        for (int i = 0; i < dfl_len; i++)
        {
            acc_sum += exp_t[i] / exp_sum * i;
        }
        box[b] = acc_sum;
    }
}

static int process_i8(int8_t *box_tensor, int32_t box_zp, float box_scale,
                      int8_t *score_tensor, int32_t score_zp, float score_scale,
                      int8_t *score_sum_tensor, int32_t score_sum_zp, float score_sum_scale,
                      int grid_h, int grid_w, int stride, int dfl_len,
                      std::vector<float> &boxes,
                      std::vector<float> &objProbs,
                      std::vector<int> &classId,
                      float threshold)
{
    int validCount = 0;
    const int grid_len = grid_h * grid_w;
    const int8_t score_thres_i8 = qnt_f32_to_affine(threshold, score_zp, score_scale);
    const int8_t score_sum_thres_i8 =
        qnt_f32_to_affine(threshold, score_sum_zp, score_sum_scale);

    for (int i = 0; i < grid_h; i++)
    {
        for (int j = 0; j < grid_w; j++)
        {
            int offset = i * grid_w + j;
            int max_class_id = -1;

            if (score_sum_tensor != nullptr &&
                score_sum_tensor[offset] < score_sum_thres_i8)
            {
                continue;
            }

            int8_t max_score = -score_zp;
            for (int c = 0; c < OBJ_CLASS_NUM; c++)
            {
                if ((score_tensor[offset] > score_thres_i8) &&
                    (score_tensor[offset] > max_score))
                {
                    max_score = score_tensor[offset];
                    max_class_id = c;
                }
                offset += grid_len;
            }

            if (max_score > score_thres_i8)
            {
                offset = i * grid_w + j;
                float box[4];
                float before_dfl[dfl_len * 4];
                for (int k = 0; k < dfl_len * 4; k++)
                {
                    before_dfl[k] =
                        deqnt_affine_to_f32(box_tensor[offset], box_zp, box_scale);
                    offset += grid_len;
                }
                compute_dfl(before_dfl, dfl_len, box);

                const float x1 = (-box[0] + j + 0.5f) * stride;
                const float y1 = (-box[1] + i + 0.5f) * stride;
                const float x2 = (box[2] + j + 0.5f) * stride;
                const float y2 = (box[3] + i + 0.5f) * stride;
                const float w = x2 - x1;
                const float h = y2 - y1;
                boxes.push_back(x1);
                boxes.push_back(y1);
                boxes.push_back(w);
                boxes.push_back(h);

                objProbs.push_back(deqnt_affine_to_f32(max_score, score_zp, score_scale));
                classId.push_back(max_class_id);
                validCount++;
            }
        }
    }
    return validCount;
}

int post_process(rknn_app_context_t *app_ctx,
                 void *outputs,
                 letterbox_t *letter_box,
                 float conf_threshold,
                 float nms_threshold,
                 object_detect_result_list *od_results)
{
    if (!app_ctx || !outputs || !letter_box || !od_results)
        return -1;
    if (!app_ctx->is_quant)
    {
        LOGE("only int8 quantized rknpu2 outputs are supported\n");
        return -1;
    }

    rknn_output *out = static_cast<rknn_output *>(outputs);
    std::vector<float> filterBoxes;
    std::vector<float> objProbs;
    std::vector<int> classId;
    int validCount = 0;
    int model_in_w = app_ctx->model_width;
    int model_in_h = app_ctx->model_height;

    memset(od_results, 0, sizeof(object_detect_result_list));

    const int dfl_len = app_ctx->output_attrs[0].dims[1] / 4;
    const int output_per_branch = app_ctx->io_num.n_output / 3;
    if (output_per_branch < 2)
    {
        LOGE("unexpected output count: %d\n", app_ctx->io_num.n_output);
        return -1;
    }

    for (int i = 0; i < 3; i++)
    {
        void *score_sum = nullptr;
        int32_t score_sum_zp = 0;
        float score_sum_scale = 1.0f;
        if (output_per_branch == 3)
        {
            score_sum = out[i * output_per_branch + 2].buf;
            score_sum_zp = app_ctx->output_attrs[i * output_per_branch + 2].zp;
            score_sum_scale = app_ctx->output_attrs[i * output_per_branch + 2].scale;
        }

        const int box_idx = i * output_per_branch;
        const int score_idx = i * output_per_branch + 1;
        const int grid_h = app_ctx->output_attrs[box_idx].dims[2];
        const int grid_w = app_ctx->output_attrs[box_idx].dims[3];
        if (grid_h <= 0 || grid_w <= 0)
        {
            LOGE("invalid grid size: h=%d w=%d\n", grid_h, grid_w);
            return -1;
        }
        const int stride = model_in_h / grid_h;

        validCount += process_i8(
            static_cast<int8_t *>(out[box_idx].buf),
            app_ctx->output_attrs[box_idx].zp,
            app_ctx->output_attrs[box_idx].scale,
            static_cast<int8_t *>(out[score_idx].buf),
            app_ctx->output_attrs[score_idx].zp,
            app_ctx->output_attrs[score_idx].scale,
            static_cast<int8_t *>(score_sum),
            score_sum_zp,
            score_sum_scale,
            grid_h,
            grid_w,
            stride,
            dfl_len,
            filterBoxes,
            objProbs,
            classId,
            conf_threshold);
    }

    if (validCount <= 0)
        return 0;

    std::vector<int> indexArray;
    indexArray.reserve(validCount);
    for (int i = 0; i < validCount; ++i)
    {
        indexArray.push_back(i);
    }
    quick_sort_indice_inverse(objProbs, 0, validCount - 1, indexArray);

    std::set<int> class_set(std::begin(classId), std::end(classId));
    for (int c : class_set)
    {
        nms(validCount, filterBoxes, classId, indexArray, c, nms_threshold);
    }

    int last_count = 0;
    od_results->count = 0;
    for (int i = 0; i < validCount; ++i)
    {
        if (indexArray[i] == -1 || last_count >= OBJ_NUMB_MAX_SIZE)
        {
            continue;
        }

        const int n = indexArray[i];
        const float x1 = filterBoxes[n * 4 + 0] - letter_box->x_pad;
        const float y1 = filterBoxes[n * 4 + 1] - letter_box->y_pad;
        const float x2 = x1 + filterBoxes[n * 4 + 2];
        const float y2 = y1 + filterBoxes[n * 4 + 3];
        const int id = classId[n];
        const float obj_conf = objProbs[i];

        od_results->results[last_count].box.left =
            static_cast<int>(clamp(x1, 0, model_in_w) / letter_box->scale);
        od_results->results[last_count].box.top =
            static_cast<int>(clamp(y1, 0, model_in_h) / letter_box->scale);
        od_results->results[last_count].box.right =
            static_cast<int>(clamp(x2, 0, model_in_w) / letter_box->scale);
        od_results->results[last_count].box.bottom =
            static_cast<int>(clamp(y2, 0, model_in_h) / letter_box->scale);
        od_results->results[last_count].prop = obj_conf;
        od_results->results[last_count].cls_id = id;
        last_count++;
    }
    od_results->count = last_count;
    return 0;
}

int init_post_process(const char *label_path)
{
    if (label_path == nullptr || strlen(label_path) == 0)
    {
        LOGE("label path is empty\n");
        return -1;
    }

    const int ret = loadLabelName(label_path, labels);
    if (ret < 0)
    {
        LOGE("Load %s failed!\n", label_path);
        return -1;
    }
    return 0;
}

char *coco_cls_to_name(int cls_id)
{
    if (cls_id < 0 || cls_id >= OBJ_CLASS_NUM)
        return kNullLabel;
    if (labels[cls_id])
        return labels[cls_id];
    return kNullLabel;
}

void deinit_post_process()
{
    for (int i = 0; i < OBJ_CLASS_NUM; i++)
    {
        if (labels[i] != nullptr)
        {
            free(labels[i]);
            labels[i] = nullptr;
        }
    }
}

namespace modules
{
    namespace postprocess
    {

        namespace
        {
            void DrawDetections(cv::Mat *frame, const object_detect_result_list &detections)
            {
                if (!frame || frame->empty())
                    return;

                for (int i = 0; i < detections.count; ++i)
                {
                    const object_detect_result *det = &detections.results[i];

                    const int left = std::max(0, det->box.left);
                    const int top = std::max(0, det->box.top);
                    const int right = std::min(frame->cols - 1, det->box.right);
                    const int bottom = std::min(frame->rows - 1, det->box.bottom);
                    if (right <= left || bottom <= top)
                        continue;

                    cv::rectangle(*frame,
                                  cv::Point(left, top),
                                  cv::Point(right, bottom),
                                  cv::Scalar(255, 0, 0), 2);

                    char label[128];
                    snprintf(label, sizeof(label), "%s %.1f%%",
                             coco_cls_to_name(det->cls_id),
                             det->prop * 100.0f);

                    cv::putText(*frame, label,
                                cv::Point(left, std::max(0, top - 5)),
                                cv::FONT_HERSHEY_SIMPLEX,
                                0.5, cv::Scalar(0, 0, 255), 1);
                }
            }
        } // namespace

        bool PostprocessNode::Run(rknn_app_context_t *app_ctx,
                                  inference::InferOutput *infer_output,
                                  const letterbox_t &letterbox,
                                  float conf_threshold,
                                  cv::Mat *frame,
                                  PostprocessOutput *out) const
        {
            if (!app_ctx || !infer_output || !out)
                return false;

            memset(&out->detections, 0, sizeof(out->detections));
            out->detection_count = 0;

            if (infer_output->raw_outputs.empty())
            {
                return false;
            }

            letterbox_t letterbox_copy = letterbox;
            const int ret = post_process(app_ctx,
                                         infer_output->raw_outputs.data(),
                                         &letterbox_copy,
                                         conf_threshold,
                                         NMS_THRESH,
                                         &out->detections);

            rknn_outputs_release(app_ctx->rknn_ctx,
                                 app_ctx->io_num.n_output,
                                 infer_output->raw_outputs.data());
            infer_output->raw_outputs.clear();

            if (ret != 0)
            {
                return false;
            }

            out->detection_count = out->detections.count;
            DrawDetections(frame, out->detections);
            return true;
        }

    } // namespace postprocess
} // namespace modules
