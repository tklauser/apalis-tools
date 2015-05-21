# apalis-tools
Tools useful for hacking on Tegra 3 based Toradex Apalis modules.

## nvtegraparts

Print partitioning information for the PT image on the internal eMMC flash. Based on nvtegrapart from https://github.com/Stuw/ac100-tools

### Usage

Directly on the Apalis:

    $ nvtegraparts /dev/mmcblk0boot1 

On an image file:
  
    $ nvtegraparts mmc.img
