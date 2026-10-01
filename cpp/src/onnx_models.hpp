#pragma once

#include <opencv2/core.hpp>

#include <string>
#include <vector>

struct YoloBox {
    int x0 = 0;
    int y0 = 0;
    int x1 = 0;
    int y1 = 0;
    int class_id = -1;
    float confidence = 0;
    std::string name;
};

class OnnxModels {
public:
    OnnxModels() = default;
    ~OnnxModels();

    OnnxModels(const OnnxModels&) = delete;
    OnnxModels& operator=(const OnnxModels&) = delete;

    void load(const std::wstring& dll_path, const std::wstring& mark_path, const std::wstring& minimap_path);
    bool has_mark() const;
    bool has_minimap() const;
    const std::string& last_error() const;
    std::vector<YoloBox> detect(const cv::Mat& bgr);
    int classify_minimap(const cv::Mat& bgr);

private:
    struct Session;

    std::vector<float> run(Session& session, const std::vector<float>& input, const std::vector<int64_t>& shape, std::vector<int64_t>& out_shape);
    bool open_session(Session& session, const std::wstring& model_path, int fallback_size);

    void* dll_ = nullptr;
    const struct OrtApi* api_ = nullptr;
    struct OrtEnv* env_ = nullptr;
    Session* mark_ = nullptr;
    Session* minimap_ = nullptr;
    std::string last_error_;
};
