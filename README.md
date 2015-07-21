# apalis-tools
Tools useful for hacking on Tegra 3 based Toradex Apalis modules.

## nvtegraparts

Print partitioning information for the PT image on the internal eMMC flash. Based on nvtegrapart from https://github.com/Stuw/ac100-tools

### Usage

Directly on the Apalis:

    $ nvtegraparts /dev/mmcblk0boot1

On an image file (e.g. created with `dd if=/dev/mmcblk0boot1 of=mmcblk0boot1.img bs=4096 count=1`):

    $ nvtegraparts mmcblk0boot1.img

## trdx-configblock

Read/write Toradex configuration block from eMMC flash. Based on u-boot code from http://git.toradex.com/cgit/u-boot-toradex.git

### Usage

Directly on the Apalis:

    $ trdx-configblock /dev/mmcblk0
