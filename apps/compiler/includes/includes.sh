CURRENT_PATH="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"

source "$CURRENT_PATH/../../bash_shared/includes.sh"

YC_PATH_COMPILER="$YC_PATH_APPS/compiler"

if [ -f "$YC_PATH_COMPILER/config.sh"  ]; then
    source "$YC_PATH_COMPILER/config.sh" # should overwrite previous
fi

function ac_on_after_build() {
    # move the run engine
    cp -rvf "$YC_PATH_APPS/startup-scripts/src/"* "$BINPATH"
}

registerHooks "ON_AFTER_BUILD" ac_on_after_build

source "$YC_PATH_COMPILER/includes/defines.sh"

source "$YC_PATH_COMPILER/includes/functions.sh"

mkdir -p $BUILDPATH
mkdir -p $BINPATH
