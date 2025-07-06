#!/bin/bash

set -e  # Exit on any error

echo "🛠️  Building ApesLang..."
make clean
make

if [ ! -f apeslang ]; then
  echo " Build failed. apeslang binary not found."
  exit 1
fi

echo " Installing apeslang to /usr/local/bin (requires sudo)..."
sudo cp apeslang /usr/local/bin/
sudo chmod +x /usr/local/bin/apeslang

echo "Verifying installation..."
if command -v apeslang >/dev/null 2>&1; then
  echo "apeslang is installed globally. WELCOME TO THE JUNGLE!!"
  echo "Try: apeslang run test.apb"
else
  echo " Installation failed. apeslang not found in PATH."
  exit 1
fi
