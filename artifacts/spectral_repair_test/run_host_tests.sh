#!/usr/bin/env bash
#
# Host-less build & run for the SpectralRepair DSP core.
#
# The full Audacity application cannot be configured in this environment
# (the muse framework submodule and Qt are not provisioned), so this script
# compiles the dependency-free STFT engine together with a standalone test
# program using only the C++ standard library. The same algorithm and
# assertions are mirrored in the gtest suite under
# src/effects/builtin_collection/tests/SpectralRepairTests.cpp, which runs
# inside the normal AU_BUILD_EFFECTS_TESTS build.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
DSP_DIR="${REPO_ROOT}/src/effects/builtin_collection/spectralrepair"

c++ -std=c++17 -O2 -Wall -Wextra \
    "${SCRIPT_DIR}/spectral_repair_tests.cpp" \
    "${DSP_DIR}/spectralrepair.cpp" \
    -o "${SCRIPT_DIR}/sr_tests"

"${SCRIPT_DIR}/sr_tests"
