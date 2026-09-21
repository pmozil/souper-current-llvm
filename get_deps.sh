#!/bin/bash -e

# Copyright 2014 The Souper Authors. All rights reserved.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

#
# Pull source dependencies required by Souper.
#
# This script only downloads source dependencies. It does not build or
# install anything.
#
# System dependencies:
#   LLVM
#   Z3
#   hiredis
#   GoogleTest
#   zstd
#
# Source dependencies:
#   KLEE
#   Alive2
#   hiredis
#

set -u

readonly THIRD_PARTY_DIR="$(pwd)/third_party"

# KLEE
readonly KLEE_REPO="https://github.com/regehr/klee"
readonly KLEE_BRANCH="klee-for-souper-17-2"
readonly KLEE_DIR="${THIRD_PARTY_DIR}/klee"

# Alive2
readonly ALIVE2_REPO="https://github.com/AliveToolkit/alive2"
readonly ALIVE2_VERSION="master"
readonly ALIVE2_DIR="${THIRD_PARTY_DIR}/alive2"

die() {
    echo "error: $*" >&2
    exit 1
}

require_command() {
    command -v "$1" >/dev/null 2>&1 ||
        die "'$1' is required but was not found"
}

clone_branch() {
    local repo="$1"
    local branch="$2"
    local directory="$3"

    if [ -d "${directory}/.git" ]; then
        echo "Updating ${directory}..."

        git -C "${directory}" fetch origin "${branch}"
        git -C "${directory}" checkout "${branch}"
        git -C "${directory}" reset --hard "origin/${branch}"
    else
        if [ -e "${directory}" ]; then
            die "${directory} exists but is not a git repository"
        fi

        echo "Cloning ${repo} (${branch})..."

        git clone \
            --branch "${branch}" \
            "${repo}" \
            "${directory}"
    fi
}

clone_tag() {
    local repo="$1"
    local tag="$2"
    local directory="$3"

    if [ -d "${directory}/.git" ]; then
        echo "Updating ${directory}..."

        git -C "${directory}" fetch --tags
        git -C "${directory}" checkout --detach "${tag}"
    else
        if [ -e "${directory}" ]; then
            die "${directory} exists but is not a git repository"
        fi

        echo "Cloning ${repo} (${tag})..."

        git clone \
            --branch "${tag}" \
            --depth 1 \
            "${repo}" \
            "${directory}"
    fi
}

# ============================================================================
# Requirements
# ============================================================================

require_command git

# ============================================================================
# third_party
# ============================================================================

mkdir -p "${THIRD_PARTY_DIR}"

# ============================================================================
# KLEE
# ============================================================================

echo
echo "==> KLEE"

clone_branch \
    "${KLEE_REPO}" \
    "${KLEE_BRANCH}" \
    "${KLEE_DIR}"

KLEE_REQUIRED_FILES=(
    "lib/Expr/ArrayCache.cpp"
    "lib/Expr/Constraints.cpp"
    "lib/Expr/ExprBuilder.cpp"
    "lib/Expr/Expr.cpp"
    "lib/Expr/ExprEvaluator.cpp"
    "lib/Expr/ExprPPrinter.cpp"
    "lib/Expr/ExprSMTLIBPrinter.cpp"
    "lib/Expr/ExprUtil.cpp"
    "lib/Expr/ExprVisitor.cpp"
    "lib/Expr/Lexer.cpp"
    "lib/Expr/Parser.cpp"
    "lib/Expr/Updates.cpp"
)

for file in "${KLEE_REQUIRED_FILES[@]}"; do
    [ -f "${KLEE_DIR}/${file}" ] ||
        die "KLEE checkout is missing required file: ${file}"
done

echo "KLEE checkout is valid."

# ============================================================================
# Alive2
# ============================================================================

echo
echo "==> Alive2"

clone_branch \
    "${ALIVE2_REPO}" \
    "${ALIVE2_VERSION}" \
    "${ALIVE2_DIR}"

mkdir -p "${ALIVE2_DIR}/build"
cd "${ALIVE2_DIR}/build"
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
cd ../../..

# ============================================================================
# Hiredis
# ============================================================================
echo
echo "==> Hiredis"

readonly HIREDIS_REPO="https://github.com/redis/hiredis.git"
readonly HIREDIS_DIR="${THIRD_PARTY_DIR}/hiredis"
readonly HIREDIS_COMMIT="19cfd60d92da1fdb958568cdd7d36264ab14e666"

if [ ! -d "${HIREDIS_DIR}/.git" ]; then
    echo "Cloning ${HIREDIS_REPO}..."
    git clone "${HIREDIS_REPO}" "${HIREDIS_DIR}"
fi
echo "Updating ${HIREDIS_DIR} to ${HIREDIS_COMMIT}..."
git -C "${HIREDIS_DIR}" fetch
git -C "${HIREDIS_DIR}" checkout "${HIREDIS_COMMIT}"

echo
echo "========================================================================"
echo "Dependencies pulled successfully."
echo "========================================================================"
echo
echo "The following dependencies are expected from the system:"
echo "  LLVM"
echo "  Z3"
echo "  hiredis"
echo "  GoogleTest"
echo "  zstd"
echo
echo "Alive2 source:"
echo "  ${ALIVE2_DIR}"
echo
echo "Configure with:"
echo "  cmake -B build \\"
echo "    -DALIVE2_ROOT=${ALIVE2_DIR}"
echo
