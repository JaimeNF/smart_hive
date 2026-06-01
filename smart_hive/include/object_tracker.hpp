/**
 * @file object_tracker.hpp
 * @brief Object Tracking module based on the SORT (Simple Online and Realtime Tracking) algorithm.
 * @details Assigns unique persistent IDs to hardware detections and maintains spatial 
 * memory of their trajectories across frames using Kalman Filters. This module is 
 * designed to filter temporal noise, handle temporary occlusions, and provide smooth 
 * bounding box predictions.
 * @version 1.2.0
 */

#ifndef OBJECT_TRACKER_HPP
#define OBJECT_TRACKER_HPP

#include <vector>
#include <opencv2/opencv.hpp>
#include "hailo_inference.hpp" // Required for the raw 'Detection' struct

/* ========================================================================
 * DATA STRUCTURES
 * ======================================================================== */

/**
 * @brief Enriched data structure returned to the main application pipeline.
 * @details Contains both the physical coordinates and the temporal lifecycle 
 * state of the object.
 */
struct TrackedObject {
    int id;               ///< Unique license plate / tracking ID assigned to the object
    int class_id;         ///< Classification ID (e.g., 0: Bee, 1: Hornet)
    float confidence;     ///< Last recorded inference confidence score
    cv::Rect2f bbox;      ///< [x, y, width, height] - Predicted/Smoothed coordinates
    bool is_new;          ///< TRUE if this is the first time this ID is confirmed
    int frames_missing;   ///< Number of consecutive frames the object has been occluded/lost
};

/* ========================================================================
 * TRACK LIFECYCLE CLASS
 * ======================================================================== */

/**
 * @brief Represents the lifecycle, state, and physics model of a single tracked object.
 * @details Wraps an OpenCV Kalman Filter to estimate position and velocity.
 */
class Track {
public:
    /**
     * @brief Constructs a new Track instance.
     * @param init_det The initial raw detection that spawned this track.
     * @param id The unique ID assigned to this track.
     */
    Track(const Detection& init_det, int id);
    
    /**
     * @brief Predicts the object's new location based on its historical velocity.
     * @details Increments the frames_missing counter.
     */
    void predict();

    /**
     * @brief Corrects the Kalman Filter state using a new hardware detection.
     * @details Resets the frames_missing counter and increments the hit_streak.
     * @param det The matched raw detection from the current frame.
     */
    void update(const Detection& det);
    
    /**
     * @brief Retrieves the current bounding box (either predicted or updated).
     * @return cv::Rect2f representing the spatial boundaries.
     */
    cv::Rect2f get_predicted_bbox() const;

    // Public state variables
    int id;
    int class_id;
    float latest_confidence;
    int frames_missing;
    int hit_streak;       ///< Number of consecutive frames the object has been successfully detected

private:
    cv::KalmanFilter kf;  ///< Internal physics engine for this specific object
    cv::Rect2f current_bbox;
};

/* ========================================================================
 * TRACKER ORCHESTRATOR CLASS
 * ======================================================================== */

/**
 * @brief Main orchestrator class that manages multiple independent Tracks.
 * @details Acts as a "Black Box" for the main loop: takes raw amnesic detections 
 * and returns persistent, identified objects using IoU (Intersection over Union) 
 * data association.
 */
class ObjectTracker {
public:
    /**
     * @brief Initializes the Object Tracker.
     * @param max_lost_frames Maximum frames an object can remain occluded before being permanently deleted.
     * @param min_iou Minimum Intersection over Union overlap required to associate a detection with an existing track.
     */
    ObjectTracker(int max_lost_frames = 15, float min_iou = 0.3f);

    /**
     * @brief Core update loop. Processes new detections and updates all internal states.
     * @param detections Vector of raw detections from the current frame.
     * @return std::vector<TrackedObject> List of active, verified objects with their IDs.
     */
    std::vector<TrackedObject> update(const std::vector<Detection>& detections);

private:
    int max_lost_frames_;
    float min_iou_;
    int next_id_;
    std::vector<Track> tracks_;

    /**
     * @brief Mathematical helper to calculate the bounding box overlap ratio.
     * @param a First bounding box.
     * @param b Second bounding box.
     * @return Float between 0.0 (no overlap) and 1.0 (perfect match).
     */
    float calculate_iou(const cv::Rect2f& a, const cv::Rect2f& b);
};

#endif // OBJECT_TRACKER_HPP