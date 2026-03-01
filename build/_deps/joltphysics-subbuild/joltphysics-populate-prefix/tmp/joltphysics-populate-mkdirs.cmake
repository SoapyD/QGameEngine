# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file LICENSE.rst or https://cmake.org/licensing for details.

cmake_minimum_required(VERSION ${CMAKE_VERSION}) # this file comes with cmake

# If CMAKE_DISABLE_SOURCE_CHANGES is set to true and the source directory is an
# existing directory in our source tree, calling file(MAKE_DIRECTORY) on it
# would cause a fatal error, even though it would be a no-op.
if(NOT EXISTS "D:/Documents/Programming/C_Projects/QEngine/build/_deps/joltphysics-src")
  file(MAKE_DIRECTORY "D:/Documents/Programming/C_Projects/QEngine/build/_deps/joltphysics-src")
endif()
file(MAKE_DIRECTORY
  "D:/Documents/Programming/C_Projects/QEngine/build/_deps/joltphysics-build"
  "D:/Documents/Programming/C_Projects/QEngine/build/_deps/joltphysics-subbuild/joltphysics-populate-prefix"
  "D:/Documents/Programming/C_Projects/QEngine/build/_deps/joltphysics-subbuild/joltphysics-populate-prefix/tmp"
  "D:/Documents/Programming/C_Projects/QEngine/build/_deps/joltphysics-subbuild/joltphysics-populate-prefix/src/joltphysics-populate-stamp"
  "D:/Documents/Programming/C_Projects/QEngine/build/_deps/joltphysics-subbuild/joltphysics-populate-prefix/src"
  "D:/Documents/Programming/C_Projects/QEngine/build/_deps/joltphysics-subbuild/joltphysics-populate-prefix/src/joltphysics-populate-stamp"
)

set(configSubDirs )
foreach(subDir IN LISTS configSubDirs)
    file(MAKE_DIRECTORY "D:/Documents/Programming/C_Projects/QEngine/build/_deps/joltphysics-subbuild/joltphysics-populate-prefix/src/joltphysics-populate-stamp/${subDir}")
endforeach()
if(cfgdir)
  file(MAKE_DIRECTORY "D:/Documents/Programming/C_Projects/QEngine/build/_deps/joltphysics-subbuild/joltphysics-populate-prefix/src/joltphysics-populate-stamp${cfgdir}") # cfgdir has leading slash
endif()
