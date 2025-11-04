Sketch uses 1281223 bytes (97%) of program storage space. Maximum is 1310720 bytes.
Sketch uses 1281223 bytes (97%) of program storage space. Maximum is 1310720 bytes.

## upload bootlogo
python3 tools/upload_system_image.py /dev/ttyACM0 assets/bootlogo.jpg

## upload font
python3 tools/upload_system_image.py /dev/ttyACM0 assets/fonts/font.vlw

arduino-cli compile  -b esp32:esp32:esp32  --build-property build.flash_size=16MB  --build-property build.partitions=partitions.csv  --build-property compiler.cpp.extra_flags="-DSMOOTH_FONT -DLOAD_GFXFF"  --build-property compiler.c.extra_flags="-DSMOOTH_FONT -DLOAD_GFXFF"  --build-path build-16m . &&   arduino-cli upload  -b esp32:esp32:esp32   --input-dir build-16m  -p /dev/ttyACM


#define SPI_FREQUENCY  40000000
