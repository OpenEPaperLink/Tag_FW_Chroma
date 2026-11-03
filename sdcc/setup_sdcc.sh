#!/bin/bash
# Source this script to start a new interactive bash shell with a locally
# built sdcc on PATH.  Usage: source setup_sdcc.sh [version]
# Type "exit" to leave the sdcc shell and return to your original environment.

#set -x
SDCC_VER=${1:-r17068}

if [ "${BASH_SOURCE[0]}" -ef "$0" ]; then
   echo "This script must be SOURCED, not run directly (source $0)."
   exit 1
fi

SETUP_PATH=$(readlink -f "${BASH_SOURCE[0]}")
SETUP_DIR=$(dirname "${SETUP_PATH}")

# Pass 1: running in the user's original shell -> launch a new interactive
# shell whose rcfile loads ~/.bashrc first, then re-sources this script.
if [ -z "${SDCC_SHELL}" ]; then
    SDCC_RCFILE=$(mktemp /tmp/sdcc_rc.XXXXXX) || return 1
    cat > "${SDCC_RCFILE}" <<EOF
# load the user's normal interactive settings (aliases, variables, prompt...)
[ -f "\$HOME/.bashrc" ] && source "\$HOME/.bashrc"
# then apply the sdcc environment on top
export SDCC_SHELL=1
source "${SETUP_PATH}" "${SDCC_VER}"
EOF
    bash --rcfile "${SDCC_RCFILE}" -i
    rm -f "${SDCC_RCFILE}"
    unset SDCC_RCFILE
    return 0
fi

# Pass 2: running inside the new interactive shell -> set up the environment.
SDCC_VER_DIR=${SETUP_DIR}/sdcc-${SDCC_VER}
source "${SETUP_DIR}/unset_sdcc.sh"
if [ ! -e "${SDCC_VER_DIR}" ]; then
    (cd "${SETUP_DIR}" && ./build_sdcc.sh "${SDCC_VER}")
fi

SDCC_PATH=${SDCC_VER_DIR}/bin
echo "$PATH" | grep "${SDCC_PATH}" > /dev/null
if [ $? -ne 0 ]; then
   export CC=sdcc
   export SDCC_PATH_SAVE=$PATH
   export PATH=${SDCC_PATH}:$PATH
   echo "Added ${SDCC_PATH} to PATH"
fi

# Prefix the prompt with "(sdcc <version>) ".  The user's original PS1 is
# saved once, so re-sourcing this script to switch versions replaces the
# prefix instead of stacking another one.
if [ -z "${SDCC_PS1_BASE+x}" ]; then
    SDCC_PS1_BASE=${PS1}
fi
PS1="(sdcc ${SDCC_VER}) ${SDCC_PS1_BASE}"
