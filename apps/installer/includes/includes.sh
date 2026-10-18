[[ ${INSTALLER_GUARDYVAR:-} -eq 1 ]] && return || readonly INSTALLER_GUARDYVAR=1 # include it once

CURRENT_PATH=$( cd "$(dirname "${BASH_SOURCE[0]}")" ; pwd )

source "$CURRENT_PATH/../../bash_shared/includes.sh"

YC_PATH_INSTALLER="$YC_PATH_APPS/installer"

J_PATH="$YC_PATH_DEPS/ycore/joiner"
J_PATH_MODULES="$YC_PATH_MODULES"

source "$J_PATH/joiner.sh"

if [ -f "$YC_PATH_INSTALLER/config.sh"  ]; then
    source "$YC_PATH_INSTALLER/config.sh" # should overwrite previous
fi

source "$YC_PATH_APPS/compiler/includes/includes.sh"

source "$YC_PATH_DEPS/semver_bash/semver.sh"

source "$YC_PATH_INSTALLER/includes/functions.sh"
