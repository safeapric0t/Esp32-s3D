#!/bin/bash
# Build web UI and embed as gzipped C arrays for ESP32

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"
WEB_UI_SRC="$PROJECT_ROOT/web_ui_src"
WEB_UI_BUILD="$PROJECT_ROOT/data/webui"
EMBED_DIR="$PROJECT_ROOT/main/web_ui_embed"

echo "=== Building Web UI ==="

# Install dependencies if needed
cd "$WEB_UI_SRC"
if [ ! -d "node_modules" ]; then
    echo "Installing npm dependencies..."
    npm install
fi

# Build production bundle
echo "Building production bundle..."
npm run build

# Create embed directory
mkdir -p "$EMBED_DIR"

# Function to create C array from gzipped file
create_embed() {
    local input_file="$1"
    local var_name="$2"
    local output_file="$EMBED_DIR/${var_name}.h"
    
    if [ -f "$input_file" ]; then
        echo "Creating embed for $var_name..."
        xxd -i "$input_file" | sed "s/unsigned char/const uint8_t/g" | sed "s/unsigned int/const size_t/g" > "$output_file"
        # Rename variables to match our naming convention
        sed -i "s/^const uint8_t .*/const uint8_t ${var_name}_gz[] __attribute__((aligned(4))) = {/" "$output_file"
        sed -i "s/^const size_t .*/const size_t ${var_name}_gz_len = /" "$output_file"
        echo "  -> $output_file"
    else
        echo "Warning: $input_file not found, creating empty placeholder"
        cat > "$output_file" << EOF
const uint8_t ${var_name}_gz[] __attribute__((aligned(4))) = { 0x1f, 0x8b, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
const size_t ${var_name}_gz_len = 20;
EOF
    fi
}

# Create embeds for each file
create_embed "$WEB_UI_BUILD/index.html.gz" "index_html"
create_embed "$WEB_UI_BUILD/app.js.gz" "app_js"
create_embed "$WEB_UI_BUILD/styles.css.gz" "styles_css"
create_embed "$WEB_UI_BUILD/favicon.ico.gz" "favicon_ico"

# Create master header
cat > "$EMBED_DIR/web_ui_embed.h" << 'EOF'
#pragma once

// Auto-generated embedded web UI assets
// DO NOT EDIT MANUALLY - run scripts/build_web_ui.sh to regenerate

#include <stdint.h>
#include <stddef.h>

// index.html.gz
extern const uint8_t index_html_gz[] __attribute__((aligned(4)));
extern const size_t index_html_gz_len;

// app.js.gz
extern const uint8_t app_js_gz[] __attribute__((aligned(4)));
extern const size_t app_js_gz_len;

// styles.css.gz
extern const uint8_t styles_css_gz[] __attribute__((aligned(4)));
extern const size_t styles_css_gz_len;

// favicon.ico.gz
extern const uint8_t favicon_ico_gz[] __attribute__((aligned(4)));
extern const size_t favicon_ico_gz_len;

EOF

echo "=== Web UI embed complete ==="
echo "Files generated in $EMBED_DIR:"
ls -la "$EMBED_DIR"