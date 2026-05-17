# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file Copyright.txt or https://cmake.org/licensing for details.

cmake_minimum_required(VERSION 3.5)

file(MAKE_DIRECTORY
  "/home/simonque/Documents/code/esp-idf/components/bootloader/subproject"
  "/home/simonque/Documents/code/esp-idf-projects/learn_freeRTOS/lesson_05/build/bootloader"
  "/home/simonque/Documents/code/esp-idf-projects/learn_freeRTOS/lesson_05/build/bootloader-prefix"
  "/home/simonque/Documents/code/esp-idf-projects/learn_freeRTOS/lesson_05/build/bootloader-prefix/tmp"
  "/home/simonque/Documents/code/esp-idf-projects/learn_freeRTOS/lesson_05/build/bootloader-prefix/src/bootloader-stamp"
  "/home/simonque/Documents/code/esp-idf-projects/learn_freeRTOS/lesson_05/build/bootloader-prefix/src"
  "/home/simonque/Documents/code/esp-idf-projects/learn_freeRTOS/lesson_05/build/bootloader-prefix/src/bootloader-stamp"
)

set(configSubDirs )
foreach(subDir IN LISTS configSubDirs)
    file(MAKE_DIRECTORY "/home/simonque/Documents/code/esp-idf-projects/learn_freeRTOS/lesson_05/build/bootloader-prefix/src/bootloader-stamp/${subDir}")
endforeach()
if(cfgdir)
  file(MAKE_DIRECTORY "/home/simonque/Documents/code/esp-idf-projects/learn_freeRTOS/lesson_05/build/bootloader-prefix/src/bootloader-stamp${cfgdir}") # cfgdir has leading slash
endif()
