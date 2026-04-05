if [  -n "${ZSH_VERSION:-}" ]; then
    DIR="$(readlink -f -- "${(%):-%x}")"
    SDK_HOME=$(dirname $DIR)
else
    SDK_HOME="$(dirname "$(readlink -f "${BASH_SOURCE[0]}")")"
fi

if [ -n "${GVSOC_WORKDIR}" ]; then
    SDK_INSTALL=${GVSOC_WORKDIR}
else
    SDK_INSTALL=${SDK_HOME}
fi

make third_party/toolchain

export PATH=$SDK_INSTALL/install/bin:$PATH
export PYTHONPATH=$SDK_INSTALL/install/python:$PYTHONPATH
export PATH=$SDK_HOME/third_party/toolchain/install/bin:$PATH
