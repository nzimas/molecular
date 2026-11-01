# SPDX-License-Identifier: GPL-3.0-only
# The Andes nds32le-elf-mculib-v3 tools are 32-bit x86 Linux binaries (some dynamically linked).
FROM --platform=linux/amd64 debian:bookworm-slim
RUN dpkg --add-architecture i386 && apt-get update && apt-get install -y --no-install-recommends \
        libc6:i386 libstdc++6:i386 zlib1g:i386 libncurses6:i386 make python3 && rm -rf /var/lib/apt/lists/*
