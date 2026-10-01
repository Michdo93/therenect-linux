/*
 Therenect - A virtual Theremin for the Kinect
 Linux port: tiny key=value settings file, replaces ofxControlPanel's XML.
 Location: $XDG_CONFIG_HOME/therenect/settings.ini (default ~/.config/...)

 This program is free software; you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation; either version 2 of the License, or
 (at your option) any later version.
 */

#pragma once

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>

class Settings
{
public:
    static std::filesystem::path defaultPath()
    {
        namespace fs = std::filesystem;
        if (const char* xdg = std::getenv("XDG_CONFIG_HOME"); xdg && *xdg)
            return fs::path(xdg) / "therenect" / "settings.ini";
        if (const char* home = std::getenv("HOME"); home && *home)
            return fs::path(home) / ".config" / "therenect" / "settings.ini";
        return "therenect-settings.ini";
    }

    bool load(const std::filesystem::path& p)
    {
        std::ifstream in(p);
        if (!in) return false;
        std::string line;
        while (std::getline(in, line)) {
            if (line.empty() || line[0] == '#') continue;
            const auto eq = line.find('=');
            if (eq == std::string::npos) continue;
            kv_[line.substr(0, eq)] = line.substr(eq + 1);
        }
        return true;
    }

    bool save(const std::filesystem::path& p) const
    {
        std::error_code ec;
        std::filesystem::create_directories(p.parent_path(), ec);
        std::ofstream out(p);
        if (!out) return false;
        out << "# Therenect settings\n";
        for (const auto& [k, v] : kv_) out << k << '=' << v << '\n';
        return true;
    }

    int getInt(const std::string& key, int def) const
    {
        auto it = kv_.find(key);
        if (it == kv_.end()) return def;
        try { return std::stoi(it->second); } catch (...) { return def; }
    }

    float getFloat(const std::string& key, float def) const
    {
        auto it = kv_.find(key);
        if (it == kv_.end()) return def;
        try { return std::stof(it->second); } catch (...) { return def; }
    }

    std::string getString(const std::string& key, const std::string& def) const
    {
        auto it = kv_.find(key);
        return it == kv_.end() ? def : it->second;
    }

    void set(const std::string& key, int v) { kv_[key] = std::to_string(v); }
    void set(const std::string& key, float v) { kv_[key] = std::to_string(v); }
    void set(const std::string& key, const std::string& v) { kv_[key] = v; }

private:
    std::map<std::string, std::string> kv_;
};
