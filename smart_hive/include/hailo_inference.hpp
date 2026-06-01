/**
 * @file hailo_inference.hpp
 */

#ifndef HAILO_INFERENCE_HPP
#define HAILO_INFERENCE_HPP

#include <string>
#include <vector>
#include <memory>
#include <optional>
#include <sys/mman.h>         // Required for POSIX mmap (Memory Mapping)
#include <opencv2/opencv.hpp>
#include <hailo/hailort.hpp>

/* ========================================================================
 * DATA STRUCTURES
 * ======================================================================== */

/**
 * @brief Represents a single bounding box parsed from the NPU's output tensor.
 * @details Coordinates are typically normalized between 0.0 and 1.0, relative 
 * to the inference input resolution.
 */
struct Detection {
    int class_id;       ///< 0: Bee, 1: Hornet
    float confidence;   ///< Detection certainty score (0.0 to 1.0)
    float xmin;         ///< Top-left X coordinate
    float ymin;         ///< Top-left Y coordinate
    float xmax;         ///< Bottom-right X coordinate
    float ymax;         ///< Bottom-right Y coordinate
};

/* ========================================================================
 * CORE AI INFERENCE CLASS
 * ======================================================================== */

/**
 * @brief Manages the lifecycle, memory, and execution of the Hailo NPU.
 */
class HailoInference {
public:
    /**
     * @brief Constructs and initializes the hardware pipeline.
     * @param hef_path Absolute or relative path to the compiled Hailo Executable Format (.hef) model.
     */
    HailoInference(const std::string& hef_path);
    
    /**
     * @brief Injects an OpenCV frame into the DMA buffer and triggers hardware inference.
     * @param frame The BGR image captured by the camera. Will be resized and converted to RGB internally.
     * @return true if the inference completed successfully; false otherwise.
     */
    bool run_inference(const cv::Mat& frame);
    
    /**
     * @brief Parses the raw memory blocks containing the NMS (Non-Maximum Suppression) results.
     * @return std::vector<Detection> A list of filtered and verified bounding boxes.
     */
    std::vector<Detection> get_detections();
    
    /**
     * @brief Health check to verify if the VDevice and model loaded correctly.
     * @return true if the NPU is armed and ready.
     */
    bool is_initialized() const { return initialized_; }

private:
    bool initialized_ = false;
    
    // Model Input Geometry (Automatically extracted from the HEF file)
    size_t input_width_;
    size_t input_height_;
    size_t input_channels_;
    size_t input_frame_size_; ///< Total byte size of the input tensor

    // HailoRT Core Objects
    std::unique_ptr<hailort::VDevice> vdevice_;
    std::shared_ptr<hailort::InferModel> infer_model_;
    std::shared_ptr<hailort::ConfiguredInferModel> configured_infer_model_;
    std::optional<hailort::ConfiguredInferModel::Bindings> bindings_;

    /* ------------------------------------------------------------------------
     * ZERO-COPY DMA MEMORY ARCHITECTURE
     * ------------------------------------------------------------------------ */
    
    /// @brief Page-aligned input buffer. Pre-allocated memory strictly tied to OS pages.
    std::shared_ptr<uint8_t> input_buffer_; 
    
    /// @brief RAII guards to ensure proper memory unmapping (munmap) and prevent RAM leaks.
    std::vector<std::shared_ptr<uint8_t>> output_buffers_guards_; 
    
    /// @brief Hardware-level bindings that maintain the active PCIe DMA tunnels open.
    std::vector<hailort::DmaMappedBuffer> buffer_mappings_; 
    
    /// @brief Registry mapping the output tensor names to their exact raw memory addresses.
    std::vector<std::pair<std::string, std::shared_ptr<uint8_t>>> output_pointers_; 
};

#endif // HAILO_INFERENCE_HPP