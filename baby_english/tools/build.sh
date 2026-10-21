#!/bin/bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
export IDF_PYTHON_ENV_PATH="${IDF_PYTHON_ENV_PATH:-$HOME/.espressif/python_env/idf5.5_py3.11_env}"
source "${BABY_IDF_PATH:-$HOME/esp/esp-idf-v5.5.3}/export.sh" >/dev/null
cd "$ROOT"
idf.py build
idf.py size
