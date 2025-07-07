#!/bin/bash

set -e # Exit on any error

REPO_URL="https://raw.githubusercontent.com/duongddinh/banana-modules/main"

install_interpreter() {
    echo "🛠️  Building ApesLang..."
    make clean
    make

    if [ ! -f apeslang ]; then
        echo " Build failed. apeslang binary not found."
        exit 1
    fi

    echo "🦍 Installing apeslang to /usr/local/bin (requires sudo)..."
    if sudo cp apeslang /usr/local/bin/ && sudo chmod +x /usr/local/bin/apeslang; then
        echo " Apeslang interpreter installed successfully."
    else
        echo "Failed to copy apeslang to /usr/local/bin. Check permissions."
        exit 1
    fi

    echo "Verifying installation..."
    if command -v apeslang >/dev/null 2>&1; then
        echo "apeslang is installed globally. WELCOME TO THE JUNGLE"
        echo "Try: apeslang repl"
    else
        echo "Installation failed. apeslang not found in PATH."
        exit 1
    fi
}

install_package_manager() {
    echo
    echo " Creating the 'apebanana' package manager..."

    cat << 'EOF' > apebanana
#!/bin/bash
REPO_URL="https://raw.githubusercontent.com/duongddinh/banana-modules/main"

if [ "$1" != "install" ] || [ -z "$2" ]; then
    echo "🙈 Unknown command or missing module."
    echo "   Usage: apebanana install <module-name>"
    exit 1
fi

MODULE_NAME="$2"
MODULE_FILE="${MODULE_NAME}.ape"

echo "🌴 Fetching '$MODULE_NAME' from the jungle..."

if curl -fsSL "$REPO_URL/$MODULE_FILE" -o "$MODULE_FILE"; then
    echo " Installed $MODULE_FILE successfully."
else
    echo " Could not fetch '$MODULE_FILE'. Make sure the module exists in the banana-modules repository."
    exit 1
fi
EOF

    chmod +x apebanana
    echo "🦍 Installing apebanana to /usr/local/bin..."
    if sudo cp apebanana /usr/local/bin/; then
        echo " 'apebanana' package manager installed successfully. GET MORE BANANAS"
        rm apebanana # Clean up the temporary script
    else
        echo " Failed to copy apebanana to /usr/local/bin. Check permissions."
        rm apebanana # Still clean up
        exit 1
    fi
    
    echo " Try: apebanana install <module-name>"
}


if [ $# -ne 0 ]; then
    echo "This script does not take arguments."
    echo "Run './install.sh' to install both the apeslang interpreter and the apebanana package manager."
    exit 1
fi

install_interpreter
install_package_manager

