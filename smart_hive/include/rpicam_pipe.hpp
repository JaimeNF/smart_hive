/**
 * @file rpicam_pipe.hpp
 */

#pragma once

#include <opencv2/opencv.hpp>
#include <string>
#include <cstdio>
#include <vector>
#include <iostream>

/**
 * @class RpiCamPipe
 * @brief Manages the lifecycle and frame extraction of a native camera pipe.
 * @details Bypasses standard V4L2 overhead by spawning a child process 
 * (via `popen`) and directly reading the raw byte stream from standard output.
 * Copy semantics are disabled to prevent file descriptor conflicts.
 */
class RpiCamPipe {
public:
    /**
     * @brief Constructs the camera pipe controller.
     * @param width Target horizontal resolution in pixels.
     * @param height Target vertical resolution in pixels.
     * @param fps Target capture rate in frames per second.
     */
    RpiCamPipe(int width, int height, int fps);

    /**
     * @brief Destroys the camera controller and releases IPC resources.
     * @details Ensures the child process is safely terminated and the pipe is closed.
     */
    ~RpiCamPipe();

    /* Disable copy constructor and assignment operator to enforce strict file descriptor ownership */
    RpiCamPipe(const RpiCamPipe&) = delete;
    RpiCamPipe& operator=(const RpiCamPipe&) = delete;

    /**
     * @brief Initializes the camera child process and opens the IPC pipe.
     * @details Allocates internal buffers based on the requested resolution and color depth.
     * @return true if the camera process starts successfully; false if pipe creation fails.
     */
    bool start();

    /**
     * @brief Extracts a single frame from the active video stream.
     * @details Blocks execution until a complete frame payload is read from the pipe buffer.
     * @param frame_bgr Reference to an OpenCV matrix where the BGR payload will be decoded.
     * @return true if a complete frame was successfully read and parsed; false on EOF or I/O error.
     */
    bool read(cv::Mat& frame_bgr);

    /**
     * @brief Terminates the active camera process and flushes buffers.
     * @details Safely closes the file descriptor. Can be called manually prior to destruction.
     */
    void release();

private:
    int width_;                  /**< Internal cache of target width */
    int height_;                 /**< Internal cache of target height */
    int fps_;                    /**< Internal cache of target frame rate */
    size_t frame_size_;          /**< Precalculated byte size of a single raw frame */
    
    FILE* pipe_ = nullptr;       /**< POSIX file pointer for the IPC stream */
    std::vector<uint8_t> buffer_;/**< Pre-allocated heap buffer for raw byte extraction */
};