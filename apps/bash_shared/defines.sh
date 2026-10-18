unamestr=$(uname)
if [[ "$unamestr" == 'Darwin' ]]; then
   if ! command -v brew &>/dev/null ; then
       ruby -e "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/master/install)"
   fi
   if ! [ "${BASH_VERSINFO}" -ge 4 ]; then
       brew install bash
   fi
   if ! command -v greadlink &>/dev/null ; then
       brew install coreutils
   fi
   YC_PATH_ROOT=$(greadlink -f "$YC_PATH_APPS/../")
else
   YC_PATH_ROOT=$(readlink -f "$YC_PATH_APPS/../")
fi

case $YC_PATH_ROOT in
  /*) YC_PATH_ROOT=$YC_PATH_ROOT;;
  *) YC_PATH_ROOT=$PWD/$YC_PATH_ROOT;;
esac

export YC_PATH_CONF="$YC_PATH_ROOT/conf"

export YC_PATH_MODULES="$YC_PATH_ROOT/modules"

export YC_PATH_DEPS="$YC_PATH_ROOT/deps"

export YC_PATH_VAR="$YC_PATH_ROOT/var"
