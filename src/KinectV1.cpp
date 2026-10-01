/*
 Therenect - A virtual Theremin for the Kinect
 Linux port (Raspberry Pi OS / Ubuntu), Kinect V1 via libfreenect.

 This program is free software; you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation; either version 2 of the License, or
 (at your option) any later version.
 */

#include "KinectV1.h"

#include <libfreenect.h>

#include <algorithm>
#include <climits>
#include <cstdio>
#include <cstring>
#include <sys/time.h>

namespace {
constexpr int NO_TILT_PENDING = INT_MIN;
}

KinectV1::KinectV1() : pendingTilt_(NO_TILT_PENDING)
{
    backBuffer_.assign(WIDTH * HEIGHT, NO_DATA);
}

KinectV1::~KinectV1()
{
    close();
}

bool KinectV1::open(int deviceIndex)
{
    close();
    lastError_.clear();

    if (freenect_init(&ctx_, nullptr) < 0) {
        lastError_ = "freenect_init failed";
        ctx_ = nullptr;
        return false;
    }
    freenect_set_log_level(ctx_, FREENECT_LOG_WARNING);

    const int count = freenect_num_devices(ctx_);
    if (count <= deviceIndex) {
        lastError_ = count == 0 ? "no Kinect found"
                                : "Kinect #" + std::to_string(deviceIndex) + " not found";
        freenect_shutdown(ctx_);
        ctx_ = nullptr;
        return false;
    }

    // Try camera + motor first; some units (model 1473, Kinect for Windows)
    // only expose the motor after an audio firmware upload, so fall back to
    // camera-only in that case.
    freenect_select_subdevices(ctx_, (freenect_device_flags)(FREENECT_DEVICE_MOTOR | FREENECT_DEVICE_CAMERA));
    hasMotor_ = true;
    if (freenect_open_device(ctx_, &dev_, deviceIndex) < 0) {
        freenect_select_subdevices(ctx_, FREENECT_DEVICE_CAMERA);
        hasMotor_ = false;
        if (freenect_open_device(ctx_, &dev_, deviceIndex) < 0) {
            lastError_ = "could not open Kinect (USB permissions? gspca_kinect loaded?)";
            dev_ = nullptr;
            freenect_shutdown(ctx_);
            ctx_ = nullptr;
            return false;
        }
    }

    freenect_set_user(dev_, this);
    freenect_set_depth_callback(dev_, &KinectV1::depthCallback);
    if (freenect_set_depth_mode(dev_, freenect_find_depth_mode(FREENECT_RESOLUTION_MEDIUM, FREENECT_DEPTH_11BIT)) < 0 ||
        freenect_start_depth(dev_) < 0) {
        lastError_ = "could not start depth stream";
        freenect_close_device(dev_);
        freenect_shutdown(ctx_);
        dev_ = nullptr;
        ctx_ = nullptr;
        return false;
    }
    if (hasMotor_) freenect_set_led(dev_, LED_GREEN);

    running_ = true;
    thread_ = std::thread(&KinectV1::eventLoop, this);
    return true;
}

void KinectV1::close()
{
    if (running_.exchange(false) && thread_.joinable()) thread_.join();
    if (thread_.joinable()) thread_.join();

    if (dev_) {
        freenect_stop_depth(dev_);
        if (hasMotor_) freenect_set_led(dev_, LED_BLINK_GREEN);
        freenect_close_device(dev_);
        dev_ = nullptr;
    }
    if (ctx_) {
        freenect_shutdown(ctx_);
        ctx_ = nullptr;
    }
}

bool KinectV1::getRawDepth(std::vector<uint16_t>& out)
{
    std::lock_guard<std::mutex> lock(frameMutex_);
    if (!frameFresh_) return false;
    out = backBuffer_;
    frameFresh_ = false;
    return true;
}

void KinectV1::setTiltAngle(int degrees)
{
    pendingTilt_ = std::clamp(degrees, TILT_MIN, TILT_MAX);
}

void KinectV1::depthCallback(_freenect_device* dev, void* depth, uint32_t /*timestamp*/)
{
    auto* self = static_cast<KinectV1*>(freenect_get_user(dev));
    if (!self || !depth) return;
    std::lock_guard<std::mutex> lock(self->frameMutex_);
    std::memcpy(self->backBuffer_.data(), depth, WIDTH * HEIGHT * sizeof(uint16_t));
    self->frameFresh_ = true;
}

void KinectV1::eventLoop()
{
    int errors = 0;
    while (running_) {
        timeval tv{0, 100000};   // 100 ms, keeps shutdown responsive
        if (freenect_process_events_timeout(ctx_, &tv) < 0) {
            if (++errors > 50) {
                std::fprintf(stderr, "[Kinect] too many USB errors, stopping stream\n");
                break;
            }
        } else {
            errors = 0;
        }

        const int tilt = pendingTilt_.exchange(NO_TILT_PENDING);
        if (tilt != NO_TILT_PENDING && hasMotor_) {
            if (freenect_set_tilt_degs(dev_, tilt) < 0)
                std::fprintf(stderr, "[Kinect] tilt to %d deg failed\n", tilt);
        }
    }
    running_ = false;
}
