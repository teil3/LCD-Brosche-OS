Sketch uses 1281223 bytes (97%) of program storage space. Maximum is 1310720 bytes.
Sketch uses 1281223 bytes (97%) of program storage space. Maximum is 1310720 bytes.

## upload bootlogo
python3 tools/upload_system_image.py /dev/ttyACM0 assets/bootlogo.jpg

## upload font
python3 tools/upload_system_image.py /dev/ttyACM0 assets/fonts/font.vlw

python3 tools/upload_system_image.py /dev/ttyACM0 assets/fonts/FreeSans12pt.vlw /system/fonts && python3 tools/upload_system_image.py /dev/ttyACM0 assets/fonts/FreeSans18pt.vlw /system/fonts && python3 tools/upload_system_image.py /dev/ttyACM0 assets/fonts/FreeSans24pt.vlw /system/fonts && python3 tools/upload_system_image.py /dev/ttyACM0 assets/fonts/FreeSansBold12pt.vlw /system/fonts && python3 tools/upload_system_image.py /dev/ttyACM0 assets/fonts/FreeSansBold18pt.vlw /system/fonts && python3 tools/upload_system_image.py /dev/ttyACM0 assets/fonts/FreeSansBold24pt.vlw /system/fonts && python3 tools/upload_system_image.py /dev/ttyACM0 assets/fonts/FreeSerif12pt.vlw /system/fonts && python3 tools/upload_system_image.py /dev/ttyACM0 assets/fonts/FreeSerif18pt.vlw /system/fonts && python3 tools/upload_system_image.py /dev/ttyACM0 assets/fonts/FreeSerif24pt.vlw /system/fonts 



arduino-cli compile  -b esp32:esp32:esp32  --build-property build.flash_size=16MB  --build-property build.partitions=partitions.csv  --build-property compiler.cpp.extra_flags="-DSMOOTH_FONT -DLOAD_GFXFF"  --build-property compiler.c.extra_flags="-DSMOOTH_FONT -DLOAD_GFXFF"  --build-path build-16m . &&   arduino-cli upload  -b esp32:esp32:esp32   --input-dir build-16m  -p /dev/ttyACM


#define SPI_FREQUENCY  40000000


abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZäöüÄÖÜàâéèêëîïùçòì
0123456789
!?"'`´^¨~/*-_=+%&@€$£#§|¬¦,.;:<>()[]{}\


python3 tools/list_littlefs.py --port /dev/ttyACM0 --root /system/fonts
