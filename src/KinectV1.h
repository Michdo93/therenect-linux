/*
 Therenect - A virtual Theremin for the Kinect
 Linux port (Raspberry Pi OS / Ubuntu), Kinect V1 via libfreenect.

 This program is free software; you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation; either version 2 of the License, or
 (at your option) any later version.
 */

#pragma once

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

struct _freenect_context;
struct _freenect_device;

// Thin replacement for the 2010 ofxKinect class: delivers the raw 11-bit
// depth image (640x480, 2047 = no data) of a Kinect V1 (models 1414/1473)
// and controls the tilt motor.
class KinectV1
{
public:
    static constexpr int WIDTH = 640;
    static constexpr int HEIGHT = 480;
    static constexpr uint16_t NO_DATA = 2047;
    static constexpr int TILT_MIN = -27;   // mechanical limits of the V1 motor
    static constexpr int TILT_MAX = 27;

    KinectV1();
    ~KinectV1();

    bool open(int deviceIndex = 0);
    void close();
    bool isOpen() const { return running_.load(); }
    bool hasMotor() const { return hasMotor_; }
    const std::string& lastError() const { return lastError_; }

    // Copies the newest depth frame into 'out' (WIDTH*HEIGHT values).
    // Returns false if no new frame has arrived since the last call.
    bool getRawDepth(std::vector<uint16_t>& out);

    // Thread-safe: the angle is applied inside the USB thread.
    void setTiltAngle(int degrees);

private:
    static void depthCallback(_freenect_device* dev, void* depth, uint32_t timestamp);
    void eventLoop();

    _freenect_context* ctx_ = nullptr;
    _freenect_device* dev_ = nullptr;
    bool hasMotor_ = false;
    std::string lastError_;

    std::thread thread_;
    std::atomic<bool> running_{false};
    std::atomic<int> pendingTilt_;

    std::mutex frameMutex_;
    std::vector<uint16_t> backBuffer_;
    bool frameFresh_ = false;
};
