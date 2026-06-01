#ifndef HAILO_INFERENCE_HPP
#define HAILO_INFERENCE_HPP

#include <string>
#include <vector>
#include <memory>
#include <optional>
#include <sys/mman.h>
#include <opencv2/opencv.hpp>
#include <hailo/hailort.hpp>

struct Detection {
    int class_id;
    float confidence;
    float xmin;
    float ymin;
    float xmax;
    float ymax;
};

class HailoInference {
public:
    HailoInference(const std::string& hef_path);
    bool run_inference(const cv::Mat& frame);
    std::vector<Detection> get_detections();
    bool is_initialized() const { return initialized_; }

private:
    bool initialized_ = false;
    size_t input_width_;
    size_t input_height_;
    size_t input_channels_;
    size_t input_frame_size_;

    std::unique_ptr<hailort::VDevice> vdevice_;
    std::shared_ptr<hailort::InferModel> infer_model_;
    std::shared_ptr<hailort::ConfiguredInferModel> configured_infer_model_;
    std::optional<hailort::ConfiguredInferModel::Bindings> bindings_;

    // --- ARQUITECTURA DE MEMORIA DMA (NUEVO) ---
    std::shared_ptr<uint8_t> input_buffer_; // Buffer de entrada alineado
    std::vector<std::shared_ptr<uint8_t>> output_buffers_guards_; // Protege la memoria RAM
    std::vector<hailort::DmaMappedBuffer> buffer_mappings_; // Mantiene el túnel DMA abierto
    std::vector<std::pair<std::string, std::shared_ptr<uint8_t>>> output_pointers_; 
};

#endif