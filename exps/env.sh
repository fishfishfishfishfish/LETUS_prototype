#===== Parameters to Fill =====

DB_NAME="letus"
TIMESTAMP=$(date +%Y%m%d_%H%M%S)
CLEAN_DB="${CLEAN_DB:-true}"  # 默认为 true

# project root directory
SCRIPT_DIR="${PWD}"

# log directory
LOG_DIR="${SCRIPT_DIR}/logs"

BUILD_DIR="${SCRIPT_DIR}/../build_release_letus"
DB_DIR="${SCRIPT_DIR}/../data"
RES_DIR="${SCRIPT_DIR}/results_${DB_NAME}"

# colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color