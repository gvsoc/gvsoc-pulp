# GVSoC models for Pulp-platform

## OS Requirements installation

### Siracusa

Xtensor is needed to compile the Neureka model.

It can be installed on Ubuntu with:

~~~~~shell
sudo apt install libxtensor-dev libxsimd-dev
~~~~~

It can be installed on Fedora with:

~~~~~shell
sudo dnf install xtensor-devel.x86_64
~~~~~

It can also be downloaded and installed to a custom location. In this case, the path must be
given with this envvar:

~~~~~shell
export XTENSOR_INCLUDE_DIR=<path>
~~~~~
