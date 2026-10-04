#!/usr/bin/env bash
# Unfish Universal Quick Installer
# Usage: curl -fsSL https://ashokwebs.github.io/unfish/install.sh | bash

set -euo pipefail

UNFISH_VERSION="2.1.0"
GITHUB_REPO="ashokwebs/unfish"
INSTALL_DIR="${HOME}/.unfish"
BIN_DIR="${INSTALL_DIR}/bin"

echo ""
echo "  🐡 Installing Unfish Programming Language (v${UNFISH_VERSION})..."
echo ""

# Detect OS and architecture
OS="$(uname -s | tr '[:upper:]' '[:lower:]')"
ARCH="$(uname -m)"

if [ "$OS" != "linux" ] || [ "$ARCH" != "x86_64" ]; then
    echo "Notice: Prebuilt binary is currently packaged for linux-x86_64."
    echo "Falling back to building from source using your local C compiler..."
    
    TMP_DIR="$(mktemp -d)"
    git clone --depth 1 "https://github.com/${GITHUB_REPO}.git" "${TMP_DIR}/unfish"
    cd "${TMP_DIR}/unfish"
    make -j4
    
    mkdir -p "${BIN_DIR}" "${INSTALL_DIR}/stdlib" "${INSTALL_DIR}/include"
    cp bin/unfish "${BIN_DIR}/"
    cp src/stdlib/*.unfish "${INSTALL_DIR}/stdlib/" 2>/dev/null || true
    cp src/codegen/unfish_runtime.h "${INSTALL_DIR}/include/"
    rm -rf "${TMP_DIR}"
else
    TARBALL="unfish-v${UNFISH_VERSION}-linux-x86_64.tar.gz"
    DOWNLOAD_URL="https://github.com/${GITHUB_REPO}/releases/download/v${UNFISH_VERSION}/${TARBALL}"
    
    echo "  -> Downloading prebuilt package from ${DOWNLOAD_URL}..."
    TMP_DIR="$(mktemp -d)"
    curl -fsSL "${DOWNLOAD_URL}" -o "${TMP_DIR}/${TARBALL}"
    
    mkdir -p "${INSTALL_DIR}"
    tar -xzf "${TMP_DIR}/${TARBALL}" -C "${INSTALL_DIR}" --strip-components=1
    rm -rf "${TMP_DIR}"
fi

chmod +x "${BIN_DIR}/unfish"

echo ""
echo "  ✓ Unfish binary installed at ${BIN_DIR}/unfish"
echo ""

# Check if BIN_DIR is in PATH
if ! echo "$PATH" | grep -q "${BIN_DIR}"; then
    SHELL_PROFILE=""
    if [ -n "${ZSH_VERSION:-}" ] || [ -f "${HOME}/.zshrc" ]; then
        SHELL_PROFILE="${HOME}/.zshrc"
    elif [ -f "${HOME}/.bashrc" ]; then
        SHELL_PROFILE="${HOME}/.bashrc"
    elif [ -f "${HOME}/.profile" ]; then
        SHELL_PROFILE="${HOME}/.profile"
    fi

    if [ -n "$SHELL_PROFILE" ]; then
        if ! grep -q "unfish/bin" "$SHELL_PROFILE"; then
            echo "export PATH=\"${BIN_DIR}:\$PATH\"" >> "$SHELL_PROFILE"
            echo "  -> Added ${BIN_DIR} to ${SHELL_PROFILE}"
        fi
    fi
    echo "  To start using unfish now, run:"
    echo "    export PATH=\"${BIN_DIR}:\$PATH\""
fi

echo ""
echo "  🐡 Verify installation with:"
echo "    unfish version"
echo "    unfish repl"
echo "    unfish studio"
echo ""
echo "  Explore docs & tutorials at: https://ashokwebs.github.io/unfish/learn.html"
echo ""
