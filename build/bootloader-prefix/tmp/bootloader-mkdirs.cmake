# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file Copyright.txt or https://cmake.org/licensing for details.

cmake_minimum_required(VERSION 3.5)

file(MAKE_DIRECTORY
  "D:/Espressif/frameworks/esp-idf-v5.1.4/components/bootloader/subproject"
  "D:/Espressif/frameworks/keypad/build/bootloader"
  "D:/Espressif/frameworks/keypad/build/bootloader-prefix"
  "D:/Espressif/frameworks/keypad/build/bootloader-prefix/tmp"
  "D:/Espressif/frameworks/keypad/build/bootloader-prefix/src/bootloader-stamp"
  "D:/Espressif/frameworks/keypad/build/bootloader-prefix/src"
  "D:/Espressif/frameworks/keypad/build/bootloader-prefix/src/bootloader-stamp"
)

set(configSubDirs )
foreach(subDir IN LISTS configSubDirs)
    file(MAKE_DIRECTORY "D:/Espressif/frameworks/keypad/build/bootloader-prefix/src/bootloader-stamp/${subDir}")
endforeach()
if(cfgdir)
  file(MAKE_DIRECTORY "D:/Espressif/frameworks/keypad/build/bootloader-prefix/src/bootloader-stamp${cfgdir}") # cfgdir has leading slash
endif()
