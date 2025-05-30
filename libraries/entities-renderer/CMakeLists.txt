# Copyright 2013-2019, High Fidelity, Inc.
# Copyright 2021-2023 Overte e.V.
# SPDX-License-Identifier: Apache-2.0

set(TARGET_NAME entities-renderer)
setup_hifi_library(Network)
link_hifi_libraries(shared workload gpu shaders procedural graphics material-networking model-networking script-engine render render-utils image qml ui pointers entities)
include_hifi_library_headers(networking)
include_hifi_library_headers(gl)
include_hifi_library_headers(ktx)
include_hifi_library_headers(octree)
include_hifi_library_headers(audio)
include_hifi_library_headers(physics)
include_hifi_library_headers(animation)
include_hifi_library_headers(hfm)
include_hifi_library_headers(model-serializers)
include_hifi_library_headers(avatars)
include_hifi_library_headers(controllers)
include_hifi_library_headers(task)
include_hifi_library_headers(graphics-scripting) # for Forward.h
include_hifi_library_headers(entities)

target_bullet()
target_polyvox()

if (WIN32)
  add_compile_definitions(_USE_MATH_DEFINES)
endif()
