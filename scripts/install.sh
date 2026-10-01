#!/usr/bin/env bash
# Therenect - Installation für Raspberry Pi OS (Bookworm/Trixie) und Ubuntu (22.04+)
set -euo pipefail
cd "$(dirname "$0")/.."

echo "==> Pakete installieren"
sudo apt-get update
sudo apt-get install -y build-essential cmake git pkg-config \
    libfreenect-dev libsdl2-dev librtmidi-dev

echo "==> udev-Regeln und gspca_kinect-Blacklist"
sudo install -m 644 udev/51-kinect.rules /etc/udev/rules.d/51-kinect.rules
sudo install -m 644 udev/blacklist-gspca-kinect.conf /etc/modprobe.d/blacklist-gspca-kinect.conf
sudo modprobe -r gspca_kinect 2>/dev/null || true
sudo udevadm control --reload-rules
sudo udevadm trigger
sudo usermod -aG plugdev,audio "$USER" || true

echo "==> Bauen"
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j"$(nproc)"

if [[ "${1:-}" == "--install" ]]; then
    echo "==> Installieren nach /usr/local"
    sudo cmake --install build
fi

echo
echo "Fertig. Start: ./build/therenect   (Hilfe: ./build/therenect --help)"
echo "Kinect ab- und wieder anstecken; nach Gruppenänderung ggf. neu anmelden."
