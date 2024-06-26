//
//  LocationScriptingInterface.cpp
//  libraries/script-engine/src
//
//  Created by Ryan Huffman on 4/29/14.
//  Copyright 2014 High Fidelity, Inc.
//  Copyright 2023 Overte e.V.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//  SPDX-License-Identifier: Apache-2.0
//

#include "LocationScriptingInterface.h"

#include <AddressManager.h>
#include "ScriptContext.h"
#include "ScriptEngine.h"
#include "ScriptValue.h"

LocationScriptingInterface* LocationScriptingInterface::getInstance() {
    static LocationScriptingInterface sharedInstance;
    return &sharedInstance;
}

ScriptValue LocationScriptingInterface::locationGetter(ScriptContext* context, ScriptEngine* engine) {
    return engine->newQObject(DependencyManager::get<AddressManager>().data());
}

ScriptValue LocationScriptingInterface::locationSetter(ScriptContext* context, ScriptEngine* engine) {
    const QVariant& argumentVariant = context->argument(0).toVariant();
    
    // just try and convert the argument to a string, should be a hifi:// address
    QMetaObject::invokeMethod(DependencyManager::get<AddressManager>().data(), "handleLookupString",
                              Q_ARG(const QString&, argumentVariant.toString()));
    
    return engine->undefinedValue();
}
