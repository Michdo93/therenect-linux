/*
 Therenect - A virtual Theremin for the Kinect
 Linux port: scalar replacement for ofxCvKalman (OpenCV 1.x C API).

 The original created cvCreateKalman(1,1,0), i.e. a 1-D filter with
 F = H = 1, Q = R = 1e-8 and P0 = 1e-5. This class reproduces exactly that
 predict/correct cycle without the OpenCV dependency (the legacy C API no
 longer exists in OpenCV 4).

 This program is free software; you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation; either version 2 of the License, or
 (at your option) any later version.
 */

#pragma once

class Kalman1D
{
public:
    explicit Kalman1D(float initial = 0.0f) : x_(initial), p_(1e-5f) {}

    float correct(float measurement)
    {
        // predict (F = 1)
        const float pPred = p_ + q_;
        // correct (H = 1)
        const float k = pPred / (pPred + r_);
        x_ = x_ + k * (measurement - x_);
        p_ = (1.0f - k) * pPred;
        return x_;
    }

private:
    float x_;
    float p_;
    static constexpr float q_ = 1e-8f;   // process noise
    static constexpr float r_ = 1e-8f;   // measurement noise
};
