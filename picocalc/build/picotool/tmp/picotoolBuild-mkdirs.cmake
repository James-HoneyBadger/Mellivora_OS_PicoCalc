# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file Copyright.txt or https://cmake.org/licensing for details.

cmake_minimum_required(VERSION 3.5)

file(MAKE_DIRECTORY
  "/home/james/Mellivora_OS_PicoCalc/picocalc/build/_deps/picotool-src"
  "/home/james/Mellivora_OS_PicoCalc/picocalc/build/_deps/picotool-build"
  "/home/james/Mellivora_OS_PicoCalc/picocalc/build/_deps"
  "/home/james/Mellivora_OS_PicoCalc/picocalc/build/picotool/tmp"
  "/home/james/Mellivora_OS_PicoCalc/picocalc/build/picotool/src/picotoolBuild-stamp"
  "/home/james/Mellivora_OS_PicoCalc/picocalc/build/picotool/src"
  "/home/james/Mellivora_OS_PicoCalc/picocalc/build/picotool/src/picotoolBuild-stamp"
)

set(configSubDirs )
foreach(subDir IN LISTS configSubDirs)
    file(MAKE_DIRECTORY "/home/james/Mellivora_OS_PicoCalc/picocalc/build/picotool/src/picotoolBuild-stamp/${subDir}")
endforeach()
if(cfgdir)
  file(MAKE_DIRECTORY "/home/james/Mellivora_OS_PicoCalc/picocalc/build/picotool/src/picotoolBuild-stamp${cfgdir}") # cfgdir has leading slash
endif()
