/**
 * @file hailo_inference.cpp
 */

#include "hailo_inference.hpp"
#include <sys/mman.h> // Required for POSIX mmap (Memory Mapping)

/* ========================================================================
 * 1. HARDWARE MEMORY ALLOCATOR
 * ======================================================================== */

/**
 * @brief Official Hailo page-aligned memory allocator.
 * @details Allocates memory strictly aligned to OS pages using mmap. 
 * This is mandatory for stable PCIe DMA transfers to avoid memory corruption
 * when the NPU writes data back to the Raspberry Pi's RAM.
 * @param size The number of bytes to allocate.
 * @return std::shared_ptr<uint8_t> Pointer to the aligned memory block.
 */
static std::shared_ptr<uint8_t> page_aligned_alloc(size_t size) {
    auto addr = mmap(NULL, size, PROT_WRITE | PROT_READ, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
    if (MAP_FAILED == addr) throw std::bad_alloc();
    
    // Custom deleter ensures munmap is called when the shared_ptr goes out of scope
    return std::shared_ptr<uint8_t>(reinterpret_cast<uint8_t*>(addr), [size](void *addr) { 
        munmap(addr, size); 
    });
}

/* ========================================================================
 * 2. NATIVE NMS DATA STRUCTURE (LEGACY/REFERENCE)
 * ======================================================================== */

/**
 * @brief Hardware-level representation of a Hailo NMS bounding box.
 * @details A standard Hailo NMS tensor outputs 6 floats (24 bytes) per detection.
 * The #pragma pack ensures the compiler does not add padding bytes, matching 
 * the hardware memory footprint exactly. 
 * Note: The current parsing algorithm uses sequential float reading for better flexibility.
 */
#pragma pack(push, 1)
struct HailoDetectionNMS {
    float ymin;
    float xmin;
    float ymax;
    float xmax;
    float score;
    float class_id; 
};
#pragma pack(pop)

/* ========================================================================
 * CLASS IMPLEMENTATION
 * ======================================================================== */

HailoInference::HailoInference(const std::string& hef_path) {
    
    // 1. Initialize Virtual Device & Load Model
    vdevice_ = hailort::VDevice::create().expect("Failed to create Hailo VDevice");
    infer_model_ = vdevice_->create_infer_model(hef_path).expect("Failed to create InferModel");

    // 2. Configure Input Geometry & Data Types (Aligned with OpenCV standard)
    infer_model_->input()->set_format_type(HAILO_FORMAT_TYPE_UINT8);
    infer_model_->input()->set_format_order(HAILO_FORMAT_ORDER_NHWC);

    configured_infer_model_ = std::make_shared<hailort::ConfiguredInferModel>(
        infer_model_->configure().expect("Failed to configure the inference model")
    );

    bindings_ = configured_infer_model_->create_bindings().expect("Failed to create memory bindings");

    /* ------------------------------------------------------------------------
     * INPUT TENSOR MANAGEMENT (DMA-Aligned)
     * ------------------------------------------------------------------------ */
    input_height_ = infer_model_->input()->shape().height;
    input_width_  = infer_model_->input()->shape().width;
    input_frame_size_ = infer_model_->input()->get_frame_size();
    
    // Allocate page-aligned memory for the input image to prevent PCIe corruption
    input_buffer_ = page_aligned_alloc(input_frame_size_);
    bindings_->input()->set_buffer(hailort::MemoryView(input_buffer_.get(), input_frame_size_));

    /* ------------------------------------------------------------------------
     * OUTPUT TENSOR MANAGEMENT (DMA-Aligned)
     * ------------------------------------------------------------------------ */
    for (const auto& output : infer_model_->outputs()) {
        std::string name = output.name();
        size_t frame_size = infer_model_->output(name)->get_frame_size();

        // Allocate and bind a page-aligned buffer for every output tensor
        auto out_buf = page_aligned_alloc(frame_size);
        output_pointers_.push_back({name, out_buf});
        bindings_->output(name)->set_buffer(hailort::MemoryView(out_buf.get(), frame_size));
    }
    
    initialized_ = true;
}

bool HailoInference::run_inference(const cv::Mat& frame) {
    if (frame.empty()) return false;

    // 1. Pre-processing: Resize and Color Conversion
    cv::Mat resized_image;
    cv::resize(frame, resized_image, cv::Size(input_width_, input_height_));
    cv::cvtColor(resized_image, resized_image, cv::COLOR_BGR2RGB);

    // 2. Memory Continuity Check: Ensure no hidden padding bytes exist
    if (!resized_image.isContinuous()) {
        resized_image = resized_image.clone();
    }

    // 3. Inject data into the DMA-mapped hardware buffer
    std::memcpy(input_buffer_.get(), resized_image.data, input_frame_size_);
    
    // 4. Trigger Hardware Inference (Blocking with 1000ms timeout)
    return (configured_infer_model_->run(bindings_.value(), std::chrono::milliseconds(1000)) == HAILO_SUCCESS);
}

std::vector<Detection> HailoInference::get_detections() {
    std::vector<Detection> detections;
    
    for (const auto& output_pair : output_pointers_) {
        // Map the raw hardware memory block as a continuous array of floating-point numbers
        float* raw_floats = reinterpret_cast<float*>(output_pair.second.get());
        
        const int num_classes = 2; // 0: Bee, 1: Hornet
        int offset = 0; // Dynamic pointer to traverse the sequential tensor payload

        for (int c = 0; c < num_classes; ++c) {
            
            // 1. Read the bounding box count header for the current class
            int boxes_count = static_cast<int>(std::round(raw_floats[offset]));
            offset++; // Advance the pointer past the header

            // Sanity check to prevent out-of-bounds memory reading
            if (boxes_count <= 0 || boxes_count > 100) {
                continue; 
            }

            float current_class_threshold = (c == 1) ? 0.50f : 0.10f; 

            // 2. Sequentially parse the bounding box coordinates and score
            for (int i = 0; i < boxes_count; ++i) {
                float ymin  = raw_floats[offset++]; 
                float xmin  = raw_floats[offset++]; 
                float ymax  = raw_floats[offset++]; 
                float xmax  = raw_floats[offset++]; 
                float score = raw_floats[offset++]; 

                // Apply the class-specific threshold filter
                if (score >= current_class_threshold) {
                    detections.push_back({
                        c,       // Class ID dynamically assigned based on the loop index
                        score, 
                        xmin, 
                        ymin, 
                        xmax, 
                        ymax
                    });
                }
            }
        }
    }
    
    return detections;
}