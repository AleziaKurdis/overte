#
#  Created by Anthony Thibault on 2019/8/24
#  Copyright 2019 High Fidelity, Inc.
#  Copyright 2023 Overte e.V.
#
#  Distributed under the Apache License, Version 2.0.
#  See the accompanying file LICENSE or http:#www.apache.org/licenses/LICENSE-2.0.html
#  SPDX-License-Identifier: Apache-2.0
#

set(TARGET_NAME hifiOsc)
setup_hifi_plugin(Qml)
link_hifi_libraries(shared controllers ui plugins input-plugins display-plugins)
include_hifi_library_headers(script-engine)
target_liblo()


