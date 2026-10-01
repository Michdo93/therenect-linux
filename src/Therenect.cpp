/*
 Therenect - A virtual Theremin for the Kinect
 Copyright (c) 2010 Martin Kaltenbrunner <martin@tuio.org>

 Linux port (Raspberry Pi OS / Ubuntu, Kinect V1):
 openFrameworks/ofxKinect/ofxOpenCv/ofxMidi/ofxControlPanel replaced by
 libfreenect, SDL2 (video + audio), RtMidi and Dear ImGui. The tracking,
 smoothing and synthesis code follows the original 0.9.2 sources.

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

#include "Therenect.h"

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace {

constexpr int KW = KinectV1::WIDTH;
constexpr int KH = KinectV1::HEIGHT;
constexpr float PI_F = 3.14159265358979f;
constexpr float TWO_PI_F = 6.28318530717959f;
constexpr const char* VERSION_TITLE = "Therenect 0.9.2 (Linux)";

const char* WAVE_NAMES[] = {"Theremin", "Sinewave", "Sawtooth", "Squarewave"};
const char* SCALE_NAMES[] = {"Continuous", "Chromatic", "Ionian/Major", "Pentatonic"};

// Quantises a frequency to the selected scale (1 = chromatic, 2 = ionian,
// 3 = pentatonic). Returns the new frequency, optionally the MIDI note.
float quantizeFrequency(float f, int scale, int* noteOut)
{
    static const int ionian_table[12]     = {0, 0, 2, 2, 4, 5, 5, 7, 7, 9, 9, 11};
    //                                         C C D D E F F G G A A H
    static const int pentatonic_table[12] = {0, 0, 2, 2, 4, 4, 7, 7, 7, 9, 9, 9};
    //                                         C C D D E E G G G A A A
    if (f <= 0.0f) f = 1.0f;
    int note = 69 + (int)std::lround(12.0 * std::log2(f / 440.0));
    if (scale == 2 || scale == 3) {
        const int m = ((note % 12) + 12) % 12;   // safe for negative notes
        const int base = note - m;
        note = base + (scale == 2 ? ionian_table[m] : pentatonic_table[m]);
    }
    if (noteOut) *noteOut = note;
    return 440.0f * std::pow(2.0f, (note - 69) / 12.0f);
}

void fillRect(SDL_Renderer* r, int x, int y, int w, int h, Uint8 cr, Uint8 cg, Uint8 cb)
{
    SDL_SetRenderDrawColor(r, cr, cg, cb, 255);
    SDL_Rect rc{x, y, w, h};
    SDL_RenderFillRect(r, &rc);
}

// Appends a filled circle (triangle fan) to a geometry batch.
void addCircle(std::vector<SDL_Vertex>& v, std::vector<int>& idx, float cx, float cy, float radius,
               SDL_Color c, int segments)
{
    const int center = (int)v.size();
    v.push_back(SDL_Vertex{SDL_FPoint{cx, cy}, c, SDL_FPoint{0, 0}});
    for (int s = 0; s < segments; ++s) {
        const float a = TWO_PI_F * s / segments;
        v.push_back(SDL_Vertex{SDL_FPoint{cx + radius * std::cos(a), cy + radius * std::sin(a)}, c, SDL_FPoint{0, 0}});
    }
    for (int s = 0; s < segments; ++s) {
        idx.push_back(center);
        idx.push_back(center + 1 + s);
        idx.push_back(center + 1 + (s + 1) % segments);
    }
}

void fillCircle(SDL_Renderer* r, float cx, float cy, float radius, SDL_Color c)
{
    std::vector<SDL_Vertex> v;
    std::vector<int> idx;
    addCircle(v, idx, cx, cy, radius, c, 24);
    SDL_RenderGeometry(r, nullptr, v.data(), (int)v.size(), idx.data(), (int)idx.size());
}

// Polyline as triangle strip: stays visible when the window is scaled
// down (SDL_RenderDrawLines draws 1 physical pixel and loses segments).
void drawPolyline(SDL_Renderer* r, const std::vector<SDL_FPoint>& pts, float width, SDL_Color c)
{
    if (pts.size() < 2) return;
    std::vector<SDL_Vertex> v;
    std::vector<int> idx;
    v.reserve((pts.size() - 1) * 4);
    idx.reserve((pts.size() - 1) * 6);
    const float hw = width * 0.5f;
    for (size_t i = 0; i + 1 < pts.size(); ++i) {
        const float dx = pts[i + 1].x - pts[i].x, dy = pts[i + 1].y - pts[i].y;
        const float len = std::sqrt(dx * dx + dy * dy);
        if (len < 1e-4f) continue;
        const float nx = -dy / len * hw, ny = dx / len * hw;
        const int b = (int)v.size();
        v.push_back(SDL_Vertex{SDL_FPoint{pts[i].x + nx, pts[i].y + ny}, c, SDL_FPoint{0, 0}});
        v.push_back(SDL_Vertex{SDL_FPoint{pts[i].x - nx, pts[i].y - ny}, c, SDL_FPoint{0, 0}});
        v.push_back(SDL_Vertex{SDL_FPoint{pts[i + 1].x + nx, pts[i + 1].y + ny}, c, SDL_FPoint{0, 0}});
        v.push_back(SDL_Vertex{SDL_FPoint{pts[i + 1].x - nx, pts[i + 1].y - ny}, c, SDL_FPoint{0, 0}});
        idx.insert(idx.end(), {b, b + 1, b + 2, b + 1, b + 3, b + 2});
    }
    if (!v.empty()) SDL_RenderGeometry(r, nullptr, v.data(), (int)v.size(), idx.data(), (int)idx.size());
}

const SDL_Color MAGENTA{200, 0, 200, 255};
const SDL_Color BLUE{0, 0, 200, 255};

} // namespace

//--------------------------------------------------------------
Therenect::Therenect(SDL_Window* window, SDL_Renderer* renderer) : window_(window), renderer_(renderer) {}

Therenect::~Therenect()
{
    exit();
}

//--------------------------------------------------------------
bool Therenect::setup(const TherenectOptions& opt)
{
    opt_ = opt;

    rawDepth.assign(KW * KH, KinectV1::NO_DATA);
    depthImage.assign(KW * KH, 0);
    controlImage.assign(KW * KH, 0);
    rgbaScratch_.assign(KW * KH, 0);

    depthTex_ = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, KW, KH);
    controlTex_ = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, KW, KH);
    if (!depthTex_ || !controlTex_) {
        std::fprintf(stderr, "Could not create textures: %s\n", SDL_GetError());
        return false;
    }

    // ---- settings (replaces TherenectSettings.xml) ----
    settings_.load(Settings::defaultPath());
    guiWave       = std::clamp(settings_.getInt("wave", 0), 0, 3);
    guiFreqRange  = std::clamp(settings_.getFloat("frequency_range", (75.0f - 50.0f) * 2.0f), 0.0f, 100.0f);
    guiScale      = std::clamp(settings_.getInt("scale", 0), 0, 3);
    position      = 255 - std::clamp(settings_.getInt("antenna_distance", 255 - 165), 0, 255);
    tiltAngle     = std::clamp(settings_.getInt("kinect_angle", 15), KinectV1::TILT_MIN, KinectV1::TILT_MAX);
    midi_channel  = std::clamp(settings_.getInt("midi_channel", 1), 1, 16);
    oscmode = guiWave;
    scale = guiScale;
    range = 50.0f + guiFreqRange / 2.0f;

    volumePoint.set(KW / 4.0f, 4.0f * KH / 5.0f, (float)position);
    pitchPoint.set(2.0f * KW / 3.0f, KH / 2.0f, (float)position);
    vReferencePoint.set(-100000, -100000, -100000);
    pReferencePoint.set(-100000, -100000, -100000);
    vControlPoint = vReferencePoint;
    pControlPoint = pReferencePoint;

    // ---- MIDI ----
    const std::string savedDevice = settings_.getString("midi_device", "");
    const auto& names = midi.portNames();
    for (size_t i = 0; i < names.size(); ++i)
        if (names[i] == savedDevice) midi_device = (int)i;

    // ---- Kinect ----
    if (!opt_.noKinect) openKinect();
    else std::printf("[Kinect] disabled (--no-kinect), mouse mode only\n");

    // ---- audio (replaces ofSoundStreamSetup) ----
    SDL_AudioSpec want{}, have{};
    want.freq = 44100;
    want.format = AUDIO_F32SYS;
    want.channels = 1;
    want.samples = (Uint16)std::clamp(opt_.audioBuffer, 64, 8192);
    want.callback = &Therenect::audioCallback;
    want.userdata = this;
    audioDev_ = SDL_OpenAudioDevice(nullptr, 0, &want, &have,
                                    SDL_AUDIO_ALLOW_FREQUENCY_CHANGE | SDL_AUDIO_ALLOW_SAMPLES_CHANGE);
    if (audioDev_ == 0) {
        std::fprintf(stderr, "[Audio] could not open output: %s\n", SDL_GetError());
    } else {
        sampleRate = have.freq;
        bufferSize = have.samples;
        std::printf("[Audio] %d Hz, %d samples/buffer (%s)\n", sampleRate, bufferSize,
                    SDL_GetCurrentAudioDriver());
    }
    sound_data.assign(std::max(bufferSize, 1), 0.0f);
    if (audioDev_) SDL_PauseAudioDevice(audioDev_, 0);

    updateWindowTitle();
    lastFrameTicks_ = SDL_GetTicks();
    return true;
}

//--------------------------------------------------------------
void Therenect::openKinect()
{
    if (kinect.open(opt_.deviceIndex)) {
        std::printf("[Kinect] device #%d opened%s\n", opt_.deviceIndex,
                    kinect.hasMotor() ? "" : " (no motor access, tilt disabled)");
        kinect.setTiltAngle(tiltAngle);
    } else {
        std::fprintf(stderr, "[Kinect] %s - running in mouse mode\n", kinect.lastError().c_str());
    }
}

//--------------------------------------------------------------
void Therenect::processDepth()
{
    vReferencePoint.set(-100000, -100000, -100000);
    pReferencePoint.set(-100000, -100000, -100000);

    float closestPitch = 100000.0f;
    float closestVolume = 100000.0f;

    double psum = 0, pxsum = 0, pysum = 0, pzsum = 0;
    double vsum = 0, vxsum = 0, vysum = 0, vzsum = 0;

    const int numPixels = KW * KH - 1;
    const uint16_t* depth = rawDepth.data();

    for (int i = numPixels; i >= 0; i--) {
        const int y = i / KW;
        const int x = i - y * KW;
        const int index = y * KW + (KW - x) - 1;     // horizontal mirror

        const int raw = depth[index];
        float dpt = 255.0f - ((raw - 200) / 920.0f) * 255.0f;
        if (raw == KinectV1::NO_DATA) dpt = 0.0f;

        // 8-bit depth image, near = white (ofxKinect::enableDepthNearValueWhite)
        const uint8_t pix = (uint8_t)std::clamp(dpt, 0.0f, 255.0f);
        depthImage[i] = pix;

        if (pix < 32) {
            controlImage[i] = 0;
            continue;
        }

        float dx = (pitchPoint.x - x) / (float)KW;
        float dy = (pitchPoint.y - y) / (float)KW;
        float dz = (pitchPoint.z - dpt) / 255.0f;
        const float pitchDistance = dx * dx + dy * dy + dz * dz;

        dx = (volumePoint.x - x) / (float)KW;
        dy = (volumePoint.y - y) / (float)KH;
        dz = (volumePoint.z - dpt) / 255.0f;
        const float volumeDistance = dx * dx + dy * dy + dz * dz;

        if (pitchDistance < 0.064f) {
            const int v = std::max(0, 255 - (int)std::floor(pitchDistance * 4000.0f));
            controlImage[i] = (uint8_t)v;
            const double weight = (v * v) / 255.0;

            psum += weight;
            pxsum += x * weight;
            pysum += y * weight;
            pzsum += dpt * weight;

            if (pitchDistance < closestPitch) {
                pReferencePoint.set((float)x, (float)y, dpt);
                closestPitch = pitchDistance;
            }
        } else if (volumeDistance < 0.064f) {
            const int v = std::max(0, 255 - (int)std::floor(volumeDistance * 4000.0f));
            controlImage[i] = (uint8_t)v;
            const double weight = (v * v) / 127.0;

            vsum += weight;
            vxsum += x * weight;
            vysum += y * weight;
            vzsum += dpt * weight;

            if (volumeDistance <= closestVolume) {
                vReferencePoint.set((float)x, (float)y, dpt);
                closestVolume = volumeDistance;
            }
        } else {
            controlImage[i] = 0;
        }
    }

    // closest point gets a strong extra weight (as in the original)
    vsum += 16384;
    vxsum += vReferencePoint.x * 16384.0;
    vysum += vReferencePoint.y * 16384.0;
    vzsum += vReferencePoint.z * 16384.0;

    psum += 16384;
    pxsum += pReferencePoint.x * 16384.0;
    pysum += pReferencePoint.y * 16384.0;
    pzsum += pReferencePoint.z * 16384.0;

    if (psum > 0) pReferencePoint.set((float)(pxsum / psum), (float)(pysum / psum), (float)(pzsum / psum));
    if (vsum > 0) vReferencePoint.set((float)(vxsum / vsum), (float)(vysum / vsum), (float)(vzsum / vsum));
}

//--------------------------------------------------------------
void Therenect::update()
{
    const uint32_t now = SDL_GetTicks();
    const uint32_t dt = now - lastFrameTicks_;
    lastFrameTicks_ = now;
    if (dt > 0) fps_ = fps_ * 0.9f + (1000.0f / dt) * 0.1f;

    if (kinect.isOpen() && kinect.getRawDepth(rawDepth)) {
        processDepth();
        texturesDirty_ = true;
    }

    auto smooth = [](Point3& control, const Point3& ref, std::unique_ptr<std::array<Kalman1D, 3>>& k) {
        if (control.z > 127) {
            control = ref;
        } else if (ref.x < 0) {
            k.reset();
            control = ref;
        } else if (!k) {
            k = std::make_unique<std::array<Kalman1D, 3>>(
                std::array<Kalman1D, 3>{Kalman1D(ref.x), Kalman1D(ref.y), Kalman1D(ref.z)});
            control = ref;
        } else {
            control.x = (*k)[0].correct(ref.x);
            control.y = (*k)[1].correct(ref.y);
            control.z = (*k)[2].correct(ref.z);
        }
    };
    smooth(vControlPoint, vReferencePoint, vPointSmoothed);
    smooth(pControlPoint, pReferencePoint, pPointSmoothed);

    float dx = (pControlPoint.x - pitchPoint.x) / (float)KW;
    float dy = (pControlPoint.y - pitchPoint.y) / (float)KW;
    float dz = (pControlPoint.z - pitchPoint.z) / 255.0f;

    if (!manually) {
        const double pitch_dist = dx * dx + dy * dy + dz * dz;
        const double pitch = ((range / 10.0f) - 1) - (range * pitch_dist);
        float fs = (float)(8.175 * std::pow(2.0, pitch));
        if (fs < 8.175f) fs = 1.0f;
        freqset = fs;
    }

    dx = (vControlPoint.x - volumePoint.x) / (float)KW;
    dy = (vControlPoint.y - volumePoint.y) / (float)KW;
    dz = (vControlPoint.z - volumePoint.z) / 127.0f;

    if (!manually) {
        const double volume_dist = dx * dx + dy * dy + dz * dz;
        float a = (float)(volume_dist * 16.0);
        a = std::clamp(a, 0.0f, 0.5f);
        if (freqset.load() == 1.0f) a = 0.0f;
        amplset = a;
    }

    updateMidi();
}

//--------------------------------------------------------------
// MIDI runs in the main thread (the original sent from the audio callback).
void Therenect::updateMidi()
{
    const int sc = scale.load();
    const bool active = midi_on && sc != 0 && midi.isOpen();
    midiSounding = active;

    if (!active) {
        if (midi_note && midi.isOpen()) midi.noteOff(midi_channel, midi_note);
        midi_note = 0;
        lastVolumeCC = -1;
        return;
    }

    int note = 0;
    const float f = quantizeFrequency(freqset.load(), sc, &note);
    if (f < 32.7f || note < 1 || note > 127) note = 0;

    const int velocity = std::clamp((int)std::floor((std::min(amplset.load(), 0.5f) / 0.5f) * 127), 0, 127);
    if (velocity != lastVolumeCC) {
        midi.controlChange(midi_channel, 7, velocity);   // channel volume
        lastVolumeCC = velocity;
    }
    if (note != midi_note) {
        if (midi_note) midi.noteOff(midi_channel, midi_note);
        if (note) midi.noteOn(midi_channel, note, 127);
        midi_note = note;
    }
}

//--------------------------------------------------------------
void Therenect::draw()
{
    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer_, 128, 128, 128, 255);
    SDL_RenderClear(renderer_);

    ImDrawList* text = ImGui::GetBackgroundDrawList();

    if (paused) {
        text->AddText(ImVec2(30, 30), IM_COL32(255, 255, 255, 255), "Display off - press 'd' to resume");
        drawGui();
        return;
    }

    // ---- 3D point cloud (top right) ----
    fillRect(renderer_, 425, 15, 400, 300, 0, 0, 0);
    SDL_Rect clip{425, 15, 400, 300};
    SDL_RenderSetClipRect(renderer_, &clip);
    drawPointCloud();
    SDL_RenderSetClipRect(renderer_, nullptr);

    // ---- depth image and control image (left) ----
    if (texturesDirty_) {
        auto upload = [this](SDL_Texture* tex, const std::vector<uint8_t>& gray) {
            for (size_t i = 0; i < gray.size(); ++i) {
                const uint32_t g = gray[i];
                rgbaScratch_[i] = 0xFF000000u | (g << 16) | (g << 8) | g;
            }
            SDL_UpdateTexture(tex, nullptr, rgbaScratch_.data(), KW * sizeof(uint32_t));
        };
        upload(depthTex_, depthImage);
        upload(controlTex_, controlImage);
        texturesDirty_ = false;
    }
    SDL_Rect dDepth{15, 15, 400, 300};
    SDL_Rect dControl{15, 325, 400, 300};
    SDL_RenderCopy(renderer_, depthTex_, nullptr, &dDepth);
    SDL_RenderCopy(renderer_, controlTex_, nullptr, &dControl);

    SDL_Rect clipLeft{15, 325, 400, 300};
    SDL_RenderSetClipRect(renderer_, &clipLeft);
    fillCircle(renderer_, 15 + pControlPoint.x / KW * 400, 325 + pControlPoint.y / KH * 300, pControlPoint.z / 64 + 4, MAGENTA);
    fillCircle(renderer_, 15 + vControlPoint.x / KW * 400, 325 + vControlPoint.y / KH * 300, vControlPoint.z / 64 + 4, MAGENTA);
    SDL_RenderSetClipRect(renderer_, nullptr);

    // ---- oscilloscope (bottom right) ----
    fillRect(renderer_, 425, 325, 400, 300, 0, 0, 0);
    SDL_Rect clipScope{425, 325, 400, 300};
    SDL_RenderSetClipRect(renderer_, &clipScope);
    {
        std::vector<float> snapshot;
        {
            std::lock_guard<std::mutex> lock(scopeMutex_);
            snapshot = sound_data;
        }
        std::vector<SDL_FPoint> line(snapshot.size());
        const float n = (float)std::max<size_t>(snapshot.size(), 1);
        for (size_t i = 0; i < snapshot.size(); ++i)
            line[i] = SDL_FPoint{425 + i / n * 400, 475 + snapshot[i] * 300};
        drawPolyline(renderer_, line, 1.5f, SDL_Color{255, 255, 255, 255});
    }

    if (midi_note) {
        char midiStr[16];
        std::snprintf(midiStr, sizeof midiStr, "%d", midi_note);
        text->AddText(ImVec2(430, 330), IM_COL32(255, 255, 255, 255), midiStr);
    }

    auto sx = [](float x) { return 425 + x / KW * 400; };
    auto sy = [](float y) { return 325 + y / KH * 300; };

    if (pControlPoint.z < pitchPoint.z) {
        fillCircle(renderer_, sx(pControlPoint.x), sy(pControlPoint.y), pControlPoint.z / 16 + 4, MAGENTA);
        fillCircle(renderer_, sx(pitchPoint.x), sy(pitchPoint.y), volumePoint.z / 16 + 4, BLUE);
    } else {
        fillCircle(renderer_, sx(pitchPoint.x), sy(pitchPoint.y), volumePoint.z / 16 + 4, BLUE);
        fillCircle(renderer_, sx(pControlPoint.x), sy(pControlPoint.y), pControlPoint.z / 16 + 4, MAGENTA);
    }

    if (vControlPoint.z < volumePoint.z) {
        fillCircle(renderer_, sx(vControlPoint.x), sy(vControlPoint.y), vControlPoint.z / 16 + 4, MAGENTA);
        fillCircle(renderer_, sx(volumePoint.x), sy(volumePoint.y), volumePoint.z / 16 + 4, BLUE);
    } else {
        fillCircle(renderer_, sx(volumePoint.x), sy(volumePoint.y), volumePoint.z / 16 + 4, BLUE);
        fillCircle(renderer_, sx(vControlPoint.x), sy(vControlPoint.y), vControlPoint.z / 16 + 4, MAGENTA);
    }
    SDL_RenderSetClipRect(renderer_, nullptr);

    text->AddText(ImVec2(850, 575), IM_COL32(255, 255, 255, 255),
                  "(c) 2010 Martin Kaltenbrunner\nInterface Culture Lab\nKunstuniversitaet Linz, Austria\nLinux/Kinect V1 port");

    drawGui();
}

//--------------------------------------------------------------
// Software re-implementation of the oF 0.06 perspective (fov 60, eye on the
// screen centre) and of the original ofTranslate/ofRotate sequence.
void Therenect::drawPointCloud()
{
    const int step = 8;
    const float dist = (WINDOW_H / 2.0f) / std::tan(30.0f * PI_F / 180.0f);
    const float cyR = std::cos(rotY * PI_F / 180.0f), syR = std::sin(rotY * PI_F / 180.0f);
    const float cxR = std::cos(rotX * PI_F / 180.0f), sxR = std::sin(rotX * PI_F / 180.0f);

    auto project = [&](float px, float py, float pz, float& outX, float& outY, float& outS) -> bool {
        float x = px + 380, y = py + 120, z = pz - 740;          // ofTranslate(380,120,-740)
        float nx = x * cyR + z * syR, nz = -x * syR + z * cyR;   // ofRotateY(rotY)
        x = nx; z = nz;
        float ny = y * cxR - z * sxR; nz = y * sxR + z * cxR;    // ofRotateX(rotX)
        y = ny; z = nz;
        x += 380 + 425; y += 120 + 15; z += -740;                // ofTranslate(380,120,-740), ofTranslate(425,15)
        const float denom = dist - z;
        if (denom < 1.0f) return false;
        outS = dist / denom;
        outX = WINDOW_W / 2.0f + (x - WINDOW_W / 2.0f) * outS;
        outY = WINDOW_H / 2.0f + (y - WINDOW_H / 2.0f) * outS;
        return true;
    };

    std::vector<SDL_Vertex> v;
    std::vector<int> idx;
    v.reserve((KW / step) * (KH / step) * 9);
    idx.reserve((KW / step) * (KH / step) * 24);

    for (int j = 0; j < KH; j += step) {
        for (int i = 0; i < KW; i += step) {
            const float distance = depthImage[j * KW + i];
            if (distance == 0) continue;
            float X, Y, S;
            if (!project((float)(i - KW), (float)(j - KH), distance * 5, X, Y, S)) continue;
            const Uint8 g = (Uint8)distance;
            addCircle(v, idx, X, Y, step * S, SDL_Color{g, g, g, 255}, 8);
        }
    }

    auto marker = [&](const Point3& p, SDL_Color c) {
        if (p.x < 0) return;
        float X, Y, S;
        if (project(p.x - KW, p.y - KH, p.z * 5, X, Y, S)) addCircle(v, idx, X, Y, step * S, c, 12);
    };
    marker(pitchPoint, BLUE);
    marker(volumePoint, BLUE);
    marker(pReferencePoint, MAGENTA);
    marker(vReferencePoint, MAGENTA);

    if (!v.empty())
        SDL_RenderGeometry(renderer_, nullptr, v.data(), (int)v.size(), idx.data(), (int)idx.size());
}

//--------------------------------------------------------------
// Dear ImGui panel, replaces ofxControlPanel
void Therenect::drawGui()
{
    ImGui::SetNextWindowPos(ImVec2(840, 15), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(265, 545), ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(200.0f / 255.0f);
    ImGui::Begin("Settings", nullptr,
                 ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse |
                     ImGuiWindowFlags_NoSavedSettings);
    ImGui::PushItemWidth(-1);

    ImGui::TextUnformatted("Waveform");
    if (ImGui::Combo("##wave", &guiWave, WAVE_NAMES, 4)) oscmode = guiWave;

    ImGui::TextUnformatted("Frequency Range");
    if (ImGui::SliderFloat("##range", &guiFreqRange, 0.0f, 100.0f, "%.0f")) range = 50.0f + guiFreqRange / 2.0f;

    ImGui::TextUnformatted("Musical Scale");
    if (ImGui::Combo("##scale", &guiScale, SCALE_NAMES, 4)) scale = guiScale;

    ImGui::TextUnformatted("Antenna distance");
    int antenna = 255 - position;
    if (ImGui::SliderInt("##antenna", &antenna, 0, 255)) setPosition(255 - antenna);

    ImGui::TextUnformatted("Kinect angle");
    int angle = tiltAngle;
    if (ImGui::SliderInt("##tilt", &angle, KinectV1::TILT_MIN, KinectV1::TILT_MAX)) setTilt(angle);

    ImGui::Separator();
    if (midi.available()) {
        bool on = midi_on;
        if (ImGui::Checkbox("MIDI enabled", &on)) setMidiEnabled(on);

        ImGui::TextUnformatted("MIDI device");
        const auto& names = midi.portNames();
        const char* current = (midi_device >= 0 && midi_device < (int)names.size()) ? names[midi_device].c_str() : "-";
        if (ImGui::BeginCombo("##mididev", current)) {
            for (int i = 0; i < (int)names.size(); ++i) {
                if (ImGui::Selectable(names[i].c_str(), i == midi_device)) openMidiDevice(i);
            }
            ImGui::EndCombo();
        }

        ImGui::TextUnformatted("MIDI channel");
        int ch = midi_channel;
        if (ImGui::SliderInt("##midich", &ch, 1, 16)) {
            if (midi_note) midi.noteOff(midi_channel, midi_note);
            midi_note = 0;
            lastVolumeCC = -1;
            midi_channel = ch;
        }
        if (ImGui::Button("Rescan MIDI ports", ImVec2(-1, 0))) {
            const std::string cur = (midi_device < (int)midi.portNames().size()) ? midi.portNames()[midi_device] : "";
            midi.refreshPorts();
            midi_device = 0;
            for (size_t i = 0; i < midi.portNames().size(); ++i)
                if (midi.portNames()[i] == cur) midi_device = (int)i;
        }
    } else {
        ImGui::TextDisabled("MIDI not available");
    }

    ImGui::Separator();
    if (kinect.isOpen()) {
        ImGui::Text("Kinect #%d: connected%s", opt_.deviceIndex, kinect.hasMotor() ? "" : " (no motor)");
    } else {
        ImGui::TextColored(ImVec4(1, 0.6f, 0.3f, 1), "Kinect: not connected");
        if (ImGui::Button("Connect Kinect", ImVec2(-1, 0))) openKinect();
    }
    ImGui::Text("Frequency: %7.1f Hz", freqset.load());
    ImGui::Text("Volume:    %5.0f %%", std::min(amplset.load(), 0.5f) * 200.0f);
    ImGui::Text("FPS:       %5.1f", fps_);

    if (ImGui::CollapsingHeader("Keys")) {
        ImGui::TextWrapped(", .  antenna distance\n+ -  Kinect angle\n0-3  waveform\nf c i p  scale\nm  MIDI on/off\nd  display on/off\nF11  fullscreen, Esc  quit");
    }

    ImGui::PopItemWidth();
    ImGui::End();
}

//--------------------------------------------------------------
void Therenect::audioCallback(void* user, Uint8* stream, int len)
{
    static_cast<Therenect*>(user)->audioRequested(reinterpret_cast<float*>(stream), len / (int)sizeof(float));
}

void Therenect::audioRequested(float* output, int n)
{
    const float aset = std::min(amplset.load(), 0.5f);
    float fset = freqset.load();
    const int sc = scale.load();

    if (sc) {
        fset = quantizeFrequency(fset, sc, nullptr);
        frequency = fset;

        if (midiSounding) {   // MIDI replaces the internal oscillator
            for (int i = 0; i < n; i++) output[i] = 0.0f;
            std::unique_lock<std::mutex> lock(scopeMutex_, std::try_to_lock);
            if (lock.owns_lock()) std::fill(sound_data.begin(), sound_data.end(), 0.0f);
            return;
        }
    }

    float sample = 0;
    const float step = std::pow(2.0f, std::fabs(frequency - fset) / (sampleRate / 6.0f)) - 0.96f;
    const int mode = oscmode.load();

    for (int i = 0; i < n; i++) {
        if (frequency != fset) {
            const float freqdiff = fset - frequency;
            if (std::fabs(freqdiff) < 0.04f) frequency = fset;
            else if (freqdiff > step) frequency += step;
            else frequency -= step;
        }

        if (amplitude != aset) {
            const float ampldiff = aset - amplitude;
            if (std::fabs(ampldiff) < 0.00005f) amplitude = aset;
            else if (ampldiff > 0) amplitude += 0.00005f;
            else amplitude -= 0.00005f;
        }

        switch (mode) {
            case 0:   // "Theremin": rectified sine at half frequency
                rotation += (((frequency / 2.0f) / sampleRate) * TWO_PI_F);
                while (rotation > TWO_PI_F) rotation -= TWO_PI_F;
                sample = (std::fabs(std::sin(rotation)) - 0.5f) * amplitude * 2.0f;
                break;
            case 1:
                rotation += (frequency / sampleRate) * TWO_PI_F;
                while (rotation > TWO_PI_F) rotation -= TWO_PI_F;
                sample = std::sin(rotation) * amplitude;
                break;
            case 2:
                rotation += (frequency / sampleRate) * TWO_PI_F;
                while (rotation > TWO_PI_F) rotation -= TWO_PI_F;
                sample = (rotation / PI_F - 1.0f) * amplitude;
                break;
            case 3:
                rotation += (frequency / sampleRate) * TWO_PI_F;
                while (rotation > TWO_PI_F) rotation -= TWO_PI_F;
                sample = (std::floor(rotation / PI_F) - 0.5f) * amplitude;
                break;
        }
        output[i] = sample;
    }

    // oscilloscope copy; skipped if the GUI thread is reading right now
    std::unique_lock<std::mutex> lock(scopeMutex_, std::try_to_lock);
    if (lock.owns_lock()) {
        if ((int)sound_data.size() != n) sound_data.assign(n, 0.0f);
        std::copy(output, output + n, sound_data.begin());
    }
}

//--------------------------------------------------------------
void Therenect::exit()
{
    if (!renderer_) return;   // already shut down

    settings_.set("wave", guiWave);
    settings_.set("frequency_range", guiFreqRange);
    settings_.set("scale", guiScale);
    settings_.set("antenna_distance", 255 - position);
    settings_.set("kinect_angle", tiltAngle);
    settings_.set("midi_channel", midi_channel);
    if (midi_device >= 0 && midi_device < (int)midi.portNames().size())
        settings_.set("midi_device", midi.portNames()[midi_device]);
    if (!settings_.save(Settings::defaultPath()))
        std::fprintf(stderr, "Could not save settings to %s\n", Settings::defaultPath().c_str());

    if (midi_note && midi.isOpen()) midi.noteOff(midi_channel, midi_note);
    midi.close();

    if (audioDev_) {
        SDL_CloseAudioDevice(audioDev_);
        audioDev_ = 0;
    }
    kinect.close();

    if (depthTex_) SDL_DestroyTexture(depthTex_);
    if (controlTex_) SDL_DestroyTexture(controlTex_);
    depthTex_ = controlTex_ = nullptr;
    renderer_ = nullptr;
}

//--------------------------------------------------------------
void Therenect::setPosition(int p)
{
    position = std::clamp(p, 0, 255);
    volumePoint.z = (float)position;
    pitchPoint.z = (float)position;
}

void Therenect::setTilt(int angle)
{
    tiltAngle = std::clamp(angle, KinectV1::TILT_MIN, KinectV1::TILT_MAX);
    kinect.setTiltAngle(tiltAngle);   // the original GUI slider forgot this call
}

void Therenect::setMidiEnabled(bool on)
{
    if (on) {
        if (!midi.open(midi_device)) {
            midi_on = false;
            return;
        }
        midi_on = true;
        midi_note = 0;
        lastVolumeCC = -1;
        if (!scale) {
            scale = 1;
            guiScale = 1;
        }
    } else {
        if (midi_note && midi.isOpen()) midi.noteOff(midi_channel, midi_note);
        midi_on = false;
        midiSounding = false;
        midi.close();
        midi_note = 0;
        scale = 0;
        guiScale = 0;
    }
}

void Therenect::openMidiDevice(int idx)
{
    midi_device = idx;
    if (midi_on) {
        if (midi_note) midi.noteOff(midi_channel, midi_note);
        midi_note = 0;
        lastVolumeCC = -1;
        if (!midi.open(midi_device)) midi_on = false;
    }
}

void Therenect::updateWindowTitle()
{
    SDL_SetWindowTitle(window_, paused ? "Therenect - display off" : VERSION_TITLE);
}

//--------------------------------------------------------------
void Therenect::keyPressed(int key)
{
    switch (key) {
        case '<':
        case ',':
            setPosition(position + 1);
            break;
        case '>':
        case '.':
            setPosition(position - 1);
            break;

        case '+':
        case '=':
            setTilt(tiltAngle + 1);
            break;
        case '-':
            setTilt(tiltAngle - 1);
            break;

        case 'd':
            paused = !paused;
            updateWindowTitle();
            break;

        case '0': case '1': case '2': case '3':
            oscmode = key - '0';
            guiWave = oscmode;
            break;

        case 'f': scale = 0; guiScale = 0; break;
        case 'c': scale = 1; guiScale = 1; break;
        case 'i': scale = 2; guiScale = 2; break;
        case 'p': scale = 3; guiScale = 3; break;

        case 'm':
            if (midi.available()) setMidiEnabled(!midi_on);
            break;
    }
}

//--------------------------------------------------------------
void Therenect::mouseDragged(int x, int y)
{
    if ((x > 425) && (x < 825) && (y > 15) && (y < 315)) {
        rotY = 70 - (x - 425) / 400.0f * 150;
    } else if (manually && (x > 425) && (x < 825) && (y > 325) && (y < 625)) {
        amplset = (1 - (float)(y - 325) / 300.0f) * .5f;
        freqset = 32.7f * std::pow(2.0f, 6.0f * ((x - 425) / 400.0f));
    }
}

void Therenect::mousePressed(int x, int y, int button)
{
    if (button == SDL_BUTTON_LEFT && (x > 425) && (x < 825) && (y > 325) && (y < 625)) {
        manually = true;
        mouseDragged(x, y);
    }
}

void Therenect::mouseReleased(int /*x*/, int /*y*/, int button)
{
    if (button == SDL_BUTTON_LEFT) manually = false;
}
