[[ ${GUARDYVAR:-} -eq 1 ]] && return || readonly GUARDYVAR=1 # include it once

# force default language for applications
LC_ALL=C

YC_PATH_APPS="$( cd "$( dirname "${BASH_SOURCE[0]}" )/../" && pwd )"

YC_PATH_SHARED="$YC_PATH_APPS/bash_shared"

# shellcheck source=./defines.sh
source "$YC_PATH_SHARED/defines.sh"

# shellcheck source=../../deps/ycore/bash-lib/src/event/hooks.sh
source "$YC_PATH_DEPS/ycore/bash-lib/src/event/hooks.sh"

# shellcheck source=./common.sh
source "$YC_PATH_SHARED/common.sh"

if [[ "$OSTYPE" = "msys" ]]; then
    YC_BINPATH_FULL="$BINPATH"
else
    export YC_BINPATH_FULL="$BINPATH/bin"
fi
