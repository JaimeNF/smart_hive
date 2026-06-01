/**
 * @file object_tracker.cpp
 */

#include "object_tracker.hpp"
#include <algorithm>
#include <numeric>

/* ========================================================================
 * TRACK: INDIVIDUAL OBJECT LIFECYCLE & PHYSICS MODEL
 * ======================================================================== */

Track::Track(const Detection& init_det, int id) : id(id), frames_missing(0), hit_streak(1) {
    class_id = init_det.class_id;
    latest_confidence = init_det.confidence;

    // Convert hardware coordinates [xmin, ymin, xmax, ymax] to OpenCV standard [x, y, width, height]
    current_bbox = cv::Rect2f(
        init_det.xmin, 
        init_det.ymin, 
        init_det.xmax - init_det.xmin, 
        init_det.ymax - init_det.ymin
    );

    /* --------------------------------------------------------------------
     * KALMAN FILTER INITIALIZATION
     * --------------------------------------------------------------------
     * State Vector (8 variables): [x, y, w, h, vx, vy, vw, vh]
     * Measurement Vector (4 variables): [x, y, w, h] (We can only measure position/size, not velocity)
     * Control Vector: 0 (No external control inputs)
     * -------------------------------------------------------------------- */
    kf = cv::KalmanFilter(8, 4, 0);

    // Transition Matrix (F): Defines the Constant Velocity Model.
    // Position = Position + Velocity * dt (Assuming dt = 1 frame)
    kf.transitionMatrix = (cv::Mat_<float>(8, 8) <<
        1, 0, 0, 0,  1, 0, 0, 0,  // x_new = x + vx
        0, 1, 0, 0,  0, 1, 0, 0,  // y_new = y + vy
        0, 0, 1, 0,  0, 0, 1, 0,  // w_new = w + vw
        0, 0, 0, 1,  0, 0, 0, 1,  // h_new = h + vh
        0, 0, 0, 0,  1, 0, 0, 0,  // vx_new = vx
        0, 0, 0, 0,  0, 1, 0, 0,  // vy_new = vy
        0, 0, 0, 0,  0, 0, 1, 0,  // vw_new = vw
        0, 0, 0, 0,  0, 0, 0, 1); // vh_new = vh

    // Measurement Matrix (H): Maps the internal state to the measurement vector.
    kf.measurementMatrix = cv::Mat::eye(4, 8, CV_32F);

    // System Noise Covariances: Tuned specifically for YOLO/Hailo edge bounding box stability
    cv::setIdentity(kf.processNoiseCov, cv::Scalar::all(1e-2));     // Q: How much we trust the physics model
    cv::setIdentity(kf.measurementNoiseCov, cv::Scalar::all(1e-1));  // R: How much we trust the AI detection
    cv::setIdentity(kf.errorCovPost, cv::Scalar::all(1));            // P: Initial uncertainty

    // Initialize the starting state
    kf.statePost.at<float>(0) = current_bbox.x;
    kf.statePost.at<float>(1) = current_bbox.y;
    kf.statePost.at<float>(2) = current_bbox.width;
    kf.statePost.at<float>(3) = current_bbox.height;
}

void Track::predict() {
    // Extrapolate the next position based on current velocity
    cv::Mat prediction = kf.predict();
    current_bbox.x = prediction.at<float>(0);
    current_bbox.y = prediction.at<float>(1);
    
    // Prevent bounding boxes from inverting or shrinking to zero mathematically
    current_bbox.width = std::max(0.01f, prediction.at<float>(2));
    current_bbox.height = std::max(0.01f, prediction.at<float>(3));
    
    frames_missing++; // Increment absence counter. Will be reset if associated later.
}

void Track::update(const Detection& det) {
    latest_confidence = det.confidence;
    frames_missing = 0; // Object found, reset absence counter
    hit_streak++;       // Increase the consecutive detection streak

    // Construct the measurement vector from the new AI detection
    cv::Mat measurement = (cv::Mat_<float>(4, 1) << 
        det.xmin, 
        det.ymin, 
        det.xmax - det.xmin, 
        det.ymax - det.ymin
    );

    // Correct the Kalman Filter predictions using the actual measurement
    cv::Mat estimated = kf.correct(measurement);
    current_bbox.x = estimated.at<float>(0);
    current_bbox.y = estimated.at<float>(1);
    current_bbox.width = std::max(0.01f, estimated.at<float>(2));
    current_bbox.height = std::max(0.01f, estimated.at<float>(3));
}

cv::Rect2f Track::get_predicted_bbox() const {
    return current_bbox;
}


/* ========================================================================
 * OBJECT TRACKER: THE SORT ORCHESTRATOR
 * ======================================================================== */

ObjectTracker::ObjectTracker(int max_lost_frames, float min_iou)
    : max_lost_frames_(max_lost_frames), min_iou_(min_iou), next_id_(1) {}

float ObjectTracker::calculate_iou(const cv::Rect2f& a, const cv::Rect2f& b) {
    // Determine the coordinates of the intersection rectangle
    float x_left = std::max(a.x, b.x);
    float y_top = std::max(a.y, b.y);
    float x_right = std::min(a.x + a.width, b.x + b.width);
    float y_bottom = std::min(a.y + a.height, b.y + b.height);

    // Check if the bounding boxes actually intersect
    if (x_right < x_left || y_bottom < y_top) return 0.0f;

    // Calculate Area of Intersection
    float intersection_area = (x_right - x_left) * (y_bottom - y_top);
    
    // Calculate Area of Union
    float a_area = a.width * a.height;
    float b_area = b.width * b.height;

    return intersection_area / float(a_area + b_area - intersection_area);
}

std::vector<TrackedObject> ObjectTracker::update(const std::vector<Detection>& detections) {
    // 1. Predict the motion of all currently known objects
    for (auto& track : tracks_) {
        track.predict();
    }

    // 2. Data Association: Match new detections to existing tracks (Greedy Algorithm)
    std::vector<bool> matched_detections(detections.size(), false);
    std::vector<bool> matched_tracks(tracks_.size(), false);

    for (size_t d = 0; d < detections.size(); ++d) {
        
        cv::Rect2f det_rect(detections[d].xmin, detections[d].ymin, 
                            detections[d].xmax - detections[d].xmin, detections[d].ymax - detections[d].ymin);
        
        int best_track_idx = -1;
        float best_iou = min_iou_; // Must be higher than the configured baseline

        for (size_t t = 0; t < tracks_.size(); ++t) {
            // Skip if track is already claimed, or if the classes mismatch (A bee cannot become a hornet)
            if (matched_tracks[t] || tracks_[t].class_id != detections[d].class_id) continue;

            float iou = calculate_iou(tracks_[t].get_predicted_bbox(), det_rect);
            if (iou > best_iou) {
                best_iou = iou;
                best_track_idx = static_cast<int>(t);
            }
        }

        // If a valid match was found, update the corresponding Kalman Filter
        if (best_track_idx >= 0) {
            tracks_[best_track_idx].update(detections[d]);
            matched_detections[d] = true;
            matched_tracks[best_track_idx] = true;
        }
    }

    // 3. Spawning: Create brand new tracks for unmatched detections
    std::vector<int> newly_created_ids;
    for (size_t d = 0; d < detections.size(); ++d) {
        if (!matched_detections[d]) {
            tracks_.emplace_back(detections[d], next_id_);
            newly_created_ids.push_back(next_id_);
            next_id_++;
        }
    }

    // 4. Pruning: Remove stale tracks that have been occluded/lost for too long
    tracks_.erase(std::remove_if(tracks_.begin(), tracks_.end(),
        [this](const Track& t) { return t.frames_missing > max_lost_frames_; }),
        tracks_.end());

    // 5. Output Preparation & Anti-Ghosting Filter
    std::vector<TrackedObject> results;
    for (const auto& track : tracks_) {
        
        // Anti-Ghosting Logic: Only broadcast objects that have been seen in at least 2 consecutive frames,
        // or established objects that are temporarily occluded.
        if (track.hit_streak >= 2 || track.frames_missing > 0) {
            
            // Flag as "new" only when it successfully graduates past the anti-ghosting filter
            bool is_new = std::find(newly_created_ids.begin(), newly_created_ids.end(), track.id) != newly_created_ids.end() 
                          || (track.hit_streak == 2 && track.frames_missing == 0); 

            results.push_back({
                track.id,
                track.class_id,
                track.latest_confidence,
                track.get_predicted_bbox(),
                is_new,
                track.frames_missing
            });
        }
    }

    return results;
}