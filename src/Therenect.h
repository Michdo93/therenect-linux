/*
 Therenect - A virtual Theremin for the Kinect
 Copyright (c) 2010 Martin Kaltenbrunner <martin@tuio.org>

 Linux port (Raspberry Pi OS / Ubuntu, Kinect V1):
 openFrameworks/ofxKinect/ofxOpenCv/ofxMidi/ofxControlPanel replaced by
 libfreenect, SDL2 (video + audio), RtMidi and Dear ImGui.

 This program is free software; you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation; either version 2 of the License, or
 (at your option) any later version.

 This program is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with this program; if not, write to the Free Software
 Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#ifndef _THERENECT
#define _THERENECT

#include <SDL.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "Kalman1D.h"
#include "KinectV1.h"
#include "MidiOut.h"
#include "Settings.h"

struct Point3
{
    float x = 0, y = 0, z = 0;
    void set(float nx, float ny, float nz) { x = nx; y = ny; z = nz; }
};

struct TherenectOptions
{
    int deviceIndex = 0;
    int audioBuffer = 512;
    bool fullscreen = false;
    bool noKinect = false;
};

class Therenect
{
public:
    static constexpr int WINDOW_W = 1120;
    static constexpr int WINDOW_H = 640;

    Therenect(SDL_Window* window, SDL_Renderer* renderer);
    ~Therenect();

    bool setup(const TherenectOptions& opt);
    void update();
    void draw();      // SDL drawing + ImGui widgets (between ImGui::NewFrame and ImGui::Render)
    void exit();

    void keyPressed(int key);
    void mouseDragged(int x, int y);
    void mousePressed(int x, int y, int button);
    void mouseReleased(int x, int y, int button);

    bool quitRequested() const { return quit_; }

private:
    void processDepth();
    void updateMidi();
    void drawPointCloud();
    void drawGui();
    void setPosition(int p);
    void setTilt(int angle);
    void setMidiEnabled(bool on);
    void openMidiDevice(int idx);
    void openKinect();
    void updateWindowTitle();

    static void audioCallback(void* user, Uint8* stream, int len);
    void audioRequested(float* output, int bufferSize);

    // --- SDL ---
    SDL_Window* window_;
    SDL_Renderer* renderer_;
    SDL_Texture* depthTex_ = nullptr;
    SDL_Texture* controlTex_ = nullptr;
    SDL_AudioDeviceID audioDev_ = 0;
    std::vector<uint32_t> rgbaScratch_;

    // --- audio (shared between main and audio thread) ---
    int sampleRate = 44100;
    int bufferSize = 512;
    std::atomic<float> amplset{0.0f};
    std::atomic<float> freqset{0.0f};
    std::atomic<int> oscmode{0};
    std::atomic<int> scale{0};
    std::atomic<bool> midiSounding{false};   // MIDI replaces audio output
    // audio thread only
    float amplitude = 0.0f, frequency = 0.0f, rotation = 0.0f;
    // oscilloscope
    std::mutex scopeMutex_;
    std::vector<float> sound_data;

    float range = 75;

    Point3 volumePoint, pitchPoint;
    Point3 vReferencePoint, pReferencePoint;
    Point3 vControlPoint, pControlPoint;
    std::unique_ptr<std::array<Kalman1D, 3>> vPointSmoothed, pPointSmoothed;

    // --- Kinect ---
    KinectV1 kinect;
    TherenectOptions opt_;
    std::vector<uint16_t> rawDepth;
    std::vector<uint8_t> depthImage;     // mirrored 8-bit depth (near = white)
    std::vector<uint8_t> controlImage;   // antenna field visualisation
    bool texturesDirty_ = true;

    // --- MIDI ---
    MidiOut midi;
    int midi_note = 0, midi_channel = 1, midi_device = 0, lastVolumeCC = -1;
    bool midi_on = false;

    // --- state ---
    int position = 165;
    int tiltAngle = 15;
    float rotX = -5, rotY = -30;
    bool paused = false;
    bool manually = false;
    bool quit_ = false;

    // GUI mirrors
    int guiWave = 0, guiScale = 0;
    float guiFreqRange = 50.0f;

    Settings settings_;
    float fps_ = 0.0f;
    uint32_t lastFrameTicks_ = 0;
};

#endif
