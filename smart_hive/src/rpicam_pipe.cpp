/**
 * @file rpicam_pipe.cpp
 * @brief Implementation of the Raspberry Pi camera IPC pipe abstraction.
 */

#include "rpicam_pipe.hpp"

/**
 * @brief Initializes camera parameters and pre-allocates the memory buffer.
 */
RpiCamPipe::RpiCamPipe(int width, int height, int fps)
    : width_(width), height_(height), fps_(fps) {
    // A standard YUV420 frame requires (width * height * 1.5) bytes of memory
    frame_size_ = static_cast<size_t>(width_ * height_ * 1.5);
    buffer_.resize(frame_size_);
}

RpiCamPipe::~RpiCamPipe() {
    release();
}

/**
 * @brief Constructs the CLI command and spawns the rpicam-vid child process.
 */
bool RpiCamPipe::start() {
    std::string cmd = "rpicam-vid -t 0 --inline --nopreview --codec yuv420 "
                      "--width " + std::to_string(width_) + " "
                      "--height " + std::to_string(height_) + " "
                      "--framerate " + std::to_string(fps_) + " -o -";

    std::cout << "[INFO] Starting rpicam-vid pipe: " << cmd << std::endl;

    // Open a POSIX pipe to read the standard output of the child process
    pipe_ = popen(cmd.c_str(), "r");
    if (!pipe_) {
        std::cerr << "[ERROR] Failed to execute rpicam-vid process." << std::endl;
        return false;
    }
    return true;
}

/**
 * @brief Extracts a raw YUV frame from the pipe and decodes it to a BGR OpenCV matrix.
 */
bool RpiCamPipe::read(cv::Mat& frame_bgr) {
    if (!pipe_) return false;

    // Read the exact byte footprint of a single frame
    size_t bytes_read = fread(buffer_.data(), 1, frame_size_, pipe_);

    if (bytes_read != frame_size_) {
        std::cerr << "[WARN] Incomplete pipe read or stream closed. Bytes read: " << bytes_read << std::endl;
        return false;
    }

    // Map the flat byte buffer to a single-channel YUV OpenCV matrix
    // Height is increased by 1.5 to accommodate the U and V chroma planes
    cv::Mat yuv(height_ + height_ / 2, width_, CV_8UC1, buffer_.data());

    // Decode from YUV (I420) to standard BGR color space
    cv::cvtColor(yuv, frame_bgr, cv::COLOR_YUV2BGR_I420);

    return true;
}

/**
 * @brief Safely terminates the IPC connection.
 */
void RpiCamPipe::release() {
    if (pipe_) {
        pclose(pipe_);
        pipe_ = nullptr;
        std::cout << "[INFO] Camera pipe closed successfully." << std::endl;
    }
}