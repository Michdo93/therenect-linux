/*
 Therenect - A virtual Theremin for the Kinect
 Linux port: MIDI output via RtMidi (ALSA sequencer), replaces ofxMidi.

 This program is free software; you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation; either version 2 of the License, or
 (at your option) any later version.
 */

#include "MidiOut.h"

#include <algorithm>
#include <cstdio>
#include <exception>

#ifdef THERENECT_HAVE_RTMIDI
#include <RtMidi.h>
#else
class RtMidiOut {};
#endif

[[maybe_unused]] static const char* VIRTUAL_PORT_LABEL = "Virtual port \"Therenect\"";

MidiOut::MidiOut()
{
#ifdef THERENECT_HAVE_RTMIDI
    try {
        out_ = std::make_unique<RtMidiOut>(RtMidi::LINUX_ALSA, "Therenect");
    } catch (const std::exception&) {
        try {
            out_ = std::make_unique<RtMidiOut>(RtMidi::UNSPECIFIED, "Therenect");
        } catch (const std::exception& e2) {
            std::fprintf(stderr, "[MIDI] not available: %s\n", e2.what());
            out_.reset();
        }
    }
#endif
    refreshPorts();
}

MidiOut::~MidiOut()
{
    close();
}

bool MidiOut::available() const
{
    return out_ != nullptr;
}

const std::vector<std::string>& MidiOut::refreshPorts()
{
    names_.clear();
#ifdef THERENECT_HAVE_RTMIDI
    if (!out_) return names_;
    names_.push_back(VIRTUAL_PORT_LABEL);
    try {
        const unsigned int n = out_->getPortCount();
        for (unsigned int i = 0; i < n; ++i) names_.push_back(out_->getPortName(i));
    } catch (const std::exception& e) {
        std::fprintf(stderr, "[MIDI] port scan failed: %s\n", e.what());
    }
    std::printf("[MIDI] available outputs:\n");
    for (size_t i = 0; i < names_.size(); ++i) std::printf("  %zu: %s\n", i, names_[i].c_str());
#endif
    return names_;
}

bool MidiOut::open(int listIndex)
{
    close();
#ifdef THERENECT_HAVE_RTMIDI
    if (!out_ || listIndex < 0 || listIndex >= (int)names_.size()) return false;
    try {
        if (listIndex == 0) out_->openVirtualPort("Therenect");
        else out_->openPort(listIndex - 1, "Therenect");
        opened_ = true;
        std::printf("[MIDI] opened: %s\n", names_[listIndex].c_str());
    } catch (const std::exception& e) {
        std::fprintf(stderr, "[MIDI] could not open port: %s\n", e.what());
        opened_ = false;
    }
#else
    (void)listIndex;
#endif
    return opened_;
}

void MidiOut::close()
{
#ifdef THERENECT_HAVE_RTMIDI
    if (out_ && opened_) {
        try { out_->closePort(); } catch (const std::exception&) {}
    }
#endif
    opened_ = false;
}

void MidiOut::send(unsigned char a, unsigned char b, unsigned char c)
{
#ifdef THERENECT_HAVE_RTMIDI
    if (!out_ || !opened_) return;
    std::vector<unsigned char> msg{a, b, c};
    try { out_->sendMessage(&msg); } catch (const std::exception& e) {
        std::fprintf(stderr, "[MIDI] send failed: %s\n", e.what());
    }
#else
    (void)a; (void)b; (void)c;
#endif
}

static unsigned char chan(int channel) { return (unsigned char)(std::clamp(channel, 1, 16) - 1); }
static unsigned char data7(int v) { return (unsigned char)std::clamp(v, 0, 127); }

void MidiOut::noteOn(int channel, int note, int velocity)
{
    send(0x90 | chan(channel), data7(note), data7(velocity));
}

void MidiOut::noteOff(int channel, int note)
{
    send(0x80 | chan(channel), data7(note), 0);
}

void MidiOut::controlChange(int channel, int controller, int value)
{
    send(0xB0 | chan(channel), data7(controller), data7(value));
}
