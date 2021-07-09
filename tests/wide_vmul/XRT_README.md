## Ubuntu 18.04 install

1. Install Ubuntu 18.04.X LTS, then

```
$ sudo apt update && sudo apt upgrade
$ mkdir src && cd src
$ ssh-keygen -t rsa && cat ~/.ssh/id_rsa.pub
```

and add key to Github account.


1. Install XRT dependencies:

`$ sudo apt install git clinfo`
`$ git clone https://github.com/Xilinx/XRT.git `
`$ sudo XRT/src/runtime_src/tools/scripts/xrtdeps.sh`


### Software Dependencies

1. add the following lines to your `.bashrc` file
```
source /opt/xilinx/xrt/setup.sh
export PATH=$PATH:/tools/Xilinx/Vitis/2021.1/bin:/tools/Xilinx/Vivado/2021.1/bin:/tools/Xilinx/DocNav:/opt/xilinx/xrt/bin/
export LIBRARY_PATH=$LIBRARY_PATH:/usr/lib/x86_64-linux-gnu
```

1. install [Vitis](https://www.xilinx.com/support/download/index.html/content/xilinx/en/downloadNav/vitis.html)
   Download full install tarball, extract, then change into directory:
```
    $ sudo ./xsetup -b ConfigGen
    $ sudo ./xsetup --agree XilinxEULA,3rdPartyEULA,WebTalkTerms --batch Install --config /root/.Xilinx/install_config.txt
    $ sudo rm -rf /tools/Xilinx/Vitis/2021.1/tps/lnx64/cmake-3.3.2/
```

## Alveo Setup

### U250

https://www.xilinx.com/products/boards-and-kits/alveo/u250.html#gettingStarted

Install the latest release of XRT, and then the U250 deployment and dev packages.


To check that card is working correctly, run:

`$ sudo /opt/xilinx/xrt/bin/xbmgmt partition --program --name xilinx_u250_gen3x16_xdma_shell_3_1 --card 4a:00.0`
`$ xbutil validate --device 4a:00.1`
`$ clinfo` should recognize the U250.


### Flash Cards (if shell is upgraded)

`$ sudo /opt/xilinx/xrt/bin/xbmgmt flash --scan`


#### Flash U250
`$ sudo /opt/xilinx/xrt/bin/xbmgmt flash --update --shell  xilinx_u250_gen3x16_base_3 --card 0000:4a:00.0`

After flash is complete, do a cold reboot:

shut down and remove the PC power plug and wait a few minutes
before re-plugging and re-starting.

#### Build test binary

```
$ sudo apt install valgrind
$ sudo snap install cmake --classic
$ sudo apt install libomp-dev
$ cd latke && mkdir build && cd build && cmake .. -DXILINX=ON
$ make -j48

```

Note: Make sure that cmake settings point to correct Vivado and XRT include directories.
Default values are:

```
VIVADO_INCLUDE_DIR       "/tools/Xilinx/Vivado/2021.1/include/"
XRT_INCLUDE_DIR          "/opt/xilinx/xrt/include/"
```

#### Build/Run Kernel

U250

```
$ v++ -t hw --platform xilinx_u250_gen3x16_xdma_3_1_202020_1 -c -k wide_vmul -I'./' -I'/tools/Xilinx/Vivado/2021.1/include/' ./wide_vmul.cpp -o wide_vmul_250HW.xo
$ v++ -t hw --platform xilinx_u250_gen3x16_xdma_3_1_202020_1 --link --config connectivity_u250.cfg  --profile_kernel data:wide_vmul:all:all wide_vmul_250HW.xo -o'wide_vmul_HW.xilinx_u250_gen3x16_xdma_shell_3_1'

```

Now copy `wide_vmul_HW.xilinx_u250_gen3x16_xdma_shell_3_1` binary to latke build directory, and run `wide_vmul` executable.

$ ./wide_vmul wide_vmul_HW
