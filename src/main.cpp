/*
 Therenect - A virtual Theremin for the Kinect
 Linux port (Raspberry Pi OS / Ubuntu, Kinect V1): SDL2 main loop.

 This program is free software; you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation; either version 2 of the License, or
 (at your option) any later version.
 */

#include <SDL.h>

#include <imgui.h>
#include <imgui_impl_sdlrenderer2.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "Therenect.h"

static void printUsage(const char* prog)
{
    std::printf(
        "Therenect 0.9.2 - virtual theremin for the Kinect V1 (Linux port)\n\n"
        "Usage: %s [options]\n"
        "  -f, --fullscreen       start in fullscreen (toggle with F11)\n"
        "  -d, --device N         Kinect device index (default 0)\n"
        "  -b, --buffer N         audio buffer size in samples (default 512,\n"
        "                         use 1024 on a Raspberry Pi if audio crackles)\n"
        "  -n, --no-kinect        run without Kinect (mouse mode in the scope)\n"
        "  -h, --help             this help\n",
        prog);
}

static bool parseArgs(int argc, char** argv, TherenectOptions& opt)
{
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        auto next = [&](int& out) {
            if (i + 1 >= argc) return false;
            out = std::atoi(argv[++i]);
            return true;
        };
        if (a == "-f" || a == "--fullscreen") opt.fullscreen = true;
        else if (a == "-n" || a == "--no-kinect") opt.noKinect = true;
        else if ((a == "-d" || a == "--device") && next(opt.deviceIndex)) {}
        else if ((a == "-b" || a == "--buffer") && next(opt.audioBuffer)) {}
        else {
            printUsage(argv[0]);
            return false;
        }
    }
    return true;
}

int main(int argc, char** argv)
{
    TherenectOptions opt;
    if (!parseArgs(argc, argv, opt)) return 1;

    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "1");       // smooth 640x480 -> 400x300 scaling
    SDL_SetHint(SDL_HINT_VIDEO_X11_NET_WM_BYPASS_COMPOSITOR, "0");
    SDL_SetHint(SDL_HINT_AUDIO_DEVICE_APP_NAME, "Therenect");

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_EVENTS) != 0) {
        std::fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }

    Uint32 winFlags = SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI;
    if (opt.fullscreen) winFlags |= SDL_WINDOW_FULLSCREEN_DESKTOP;
    SDL_Window* window = SDL_CreateWindow("Therenect 0.9.2 (Linux)", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                          Therenect::WINDOW_W, Therenect::WINDOW_H, winFlags);
    if (!window) {
        std::fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    SDL_Renderer* renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
    if (!renderer) {
        std::fprintf(stderr, "No accelerated renderer (%s), falling back to software\n", SDL_GetError());
        renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE);
    }
    if (!renderer) {
        std::fprintf(stderr, "SDL_CreateRenderer failed: %s\n", SDL_GetError());
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }
    SDL_RendererInfo info;
    if (SDL_GetRendererInfo(renderer, &info) == 0) std::printf("[Video] renderer: %s\n", info.name);

    // The whole UI is laid out in 1120x640 (as the original); SDL scales it
    // to any window size (e.g. 800x480 Raspberry Pi touch display) and
    // converts mouse/touch coordinates back to this logical size.
    SDL_RenderSetLogicalSize(renderer, Therenect::WINDOW_W, Therenect::WINDOW_H);

    // ---- Dear ImGui: renderer backend + minimal platform glue ----
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.BackendPlatformName = "therenect_sdl2_logical";
    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 0.0f;
    style.Colors[ImGuiCol_WindowBg] = ImVec4(30 / 255.0f, 30 / 255.0f, 30 / 255.0f, 1.0f);
    ImGui_ImplSDLRenderer2_Init(renderer);

    int exitCode = 0;
    {
        Therenect app(window, renderer);
        if (!app.setup(opt)) {
            exitCode = 1;
        } else {
            bool running = true;
            const Uint64 freq = SDL_GetPerformanceFrequency();
            const double frameTime = 1.0 / 30.0;   // ofSetFrameRate(30)
            Uint64 last = SDL_GetPerformanceCounter();
            bool leftDown = false;

            while (running && !app.quitRequested()) {
                const Uint64 frameStart = SDL_GetPerformanceCounter();

                SDL_Event e;
                while (SDL_PollEvent(&e)) {
                    switch (e.type) {
                        case SDL_QUIT:
                            running = false;
                            break;
                        case SDL_WINDOWEVENT:
                            if (e.window.event == SDL_WINDOWEVENT_FOCUS_LOST) io.AddFocusEvent(false);
                            if (e.window.event == SDL_WINDOWEVENT_FOCUS_GAINED) io.AddFocusEvent(true);
                            break;
                        case SDL_MOUSEMOTION:
                            io.AddMousePosEvent((float)e.motion.x, (float)e.motion.y);
                            if (leftDown && !io.WantCaptureMouse) app.mouseDragged(e.motion.x, e.motion.y);
                            break;
                        case SDL_MOUSEBUTTONDOWN:
                        case SDL_MOUSEBUTTONUP: {
                            const bool down = e.type == SDL_MOUSEBUTTONDOWN;
                            int b = -1;
                            if (e.button.button == SDL_BUTTON_LEFT) b = 0;
                            else if (e.button.button == SDL_BUTTON_RIGHT) b = 1;
                            else if (e.button.button == SDL_BUTTON_MIDDLE) b = 2;
                            io.AddMousePosEvent((float)e.button.x, (float)e.button.y);
                            if (b >= 0) io.AddMouseButtonEvent(b, down);
                            if (e.button.button == SDL_BUTTON_LEFT) leftDown = down;
                            if (down && !io.WantCaptureMouse) app.mousePressed(e.button.x, e.button.y, e.button.button);
                            if (!down) app.mouseReleased(e.button.x, e.button.y, e.button.button);
                            break;
                        }
                        case SDL_MOUSEWHEEL:
                            io.AddMouseWheelEvent((float)e.wheel.x, (float)e.wheel.y);
                            break;
                        case SDL_KEYDOWN:
                            if (e.key.keysym.sym == SDLK_ESCAPE) running = false;
                            else if (e.key.keysym.sym == SDLK_F11) {
                                const bool fs = SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN_DESKTOP;
                                SDL_SetWindowFullscreen(window, fs ? 0 : SDL_WINDOW_FULLSCREEN_DESKTOP);
                            }
                            break;
                        case SDL_TEXTINPUT:
                            // layout-aware characters ('+', '<', ...) as in ofBaseApp::keyPressed
                            if (!io.WantTextInput && e.text.text[0] && !e.text.text[1])
                                app.keyPressed((unsigned char)e.text.text[0]);
                            break;
                        default:
                            break;
                    }
                }

                const Uint64 now = SDL_GetPerformanceCounter();
                io.DeltaTime = std::max(1e-4f, (float)((now - last) / (double)freq));
                last = now;
                io.DisplaySize = ImVec2((float)Therenect::WINDOW_W, (float)Therenect::WINDOW_H);
                io.DisplayFramebufferScale = ImVec2(1.0f, 1.0f);

                app.update();

                ImGui_ImplSDLRenderer2_NewFrame();
                ImGui::NewFrame();
                app.draw();
                ImGui::Render();
                ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData(), renderer);
                SDL_RenderPresent(renderer);

                const double elapsed = (SDL_GetPerformanceCounter() - frameStart) / (double)freq;
                if (elapsed < frameTime) SDL_Delay((Uint32)((frameTime - elapsed) * 1000.0));
            }
        }
        app.exit();
    }

    ImGui_ImplSDLRenderer2_Shutdown();
    ImGui::DestroyContext();
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return exitCode;
}
