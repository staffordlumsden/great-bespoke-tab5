Local override of the installed ESP-IDF v5.5.5 fatfs component.
SDK Git HEAD: b774170ff46c393eeb5e495ea37936038d3f4f4f (dirty).
Upstream: https://github.com/espressif/esp-idf/tree/v5.5.5/components/fatfs

Changes only in src/ffconf.h:
- FF_FS_EXFAT=1, to mount the existing 64 GB exFAT card without formatting.
- Define FF_USE_LABEL as literal 0/1 based on CONFIG_FATFS_USE_LABEL. The
  original macro references an undefined boolean config symbol in a C expression
  on the exFAT code path when volume-label support is disabled.

sdkconfig.defaults enables FATFS_LFN_HEAP and UTF-8 for exFAT. The remaining
sources and IDF VFS/SDMMC integration are copied unchanged from the local SDK.
The override is project-local and does not change the shared CYD SDK.
Rebase/review this copy when changing ESP-IDF versions. Upstream source
copyright and license notices are retained in their original files.
