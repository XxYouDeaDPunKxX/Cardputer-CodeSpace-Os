#!/usr/bin/env bash
set -euo pipefail

sudo apt-get update
sudo apt-get install --no-install-recommends \
    libreoffice-writer libreoffice-calc libreoffice-impress libreoffice-gtk3 \
    libreoffice-l10n-it hunspell-it fonts-crosextra-carlito fonts-crosextra-caladea \
    atril ristretto drawing parole \
    gstreamer1.0-libav gstreamer1.0-plugins-good xarchiver thunar-archive-plugin \
    p7zip-full unrar-free galculator xfce4-screenshooter
