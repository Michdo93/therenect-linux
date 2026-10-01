/*
 Therenect - A virtual Theremin for the Kinect
 Linux port: MIDI output via RtMidi (ALSA sequencer), replaces ofxMidi.

 This program is free software; you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation; either version 2 of the License, or
 (at your option) any later version.
 */

#pragma once

#include <memory>
#include <string>
#include <vector>

class RtMidiOut;

class MidiOut
{
public:
    MidiOut();
    ~MidiOut();

    // false if compiled without RtMidi or no MIDI subsystem is available
    bool available() const;

    // Rescans the ports. Entry 0 is always the virtual port "Therenect"
    // (other programs such as FluidSynth/Qsynth can connect to it).
    const std::vector<std::string>& refreshPorts();
    const std::vector<std::string>& portNames() const { return names_; }

    bool open(int listIndex);
    void close();
    bool isOpen() const { return opened_; }

    // channel: 1..16
    void noteOn(int channel, int note, int velocity);
    void noteOff(int channel, int note);
    void controlChange(int channel, int controller, int value);

private:
    void send(unsigned char a, unsigned char b, unsigned char c);

    std::unique_ptr<RtMidiOut> out_;
    std::vector<std::string> names_;
    bool opened_ = false;
};
