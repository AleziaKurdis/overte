//
//  SciptEngineBenchmarks.h
//  tests/script-engine/src
//
//  Created by Dale Glass
//  Copyright 2023 Overte e.V.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//  SPDX-License-Identifier: Apache-2.0
//

#pragma once

#include <QtTest/QtTest>
#include "ScriptManager.h"
#include "ScriptEngine.h"


using ScriptManagerPointer = std::shared_ptr<ScriptManager>;


class ScriptEngineBenchmarkTests : public QObject {
    Q_OBJECT
private slots:
    void initTestCase();
    void benchmarkSetProperty();
    void benchmarkSetProperty1K();
    void benchmarkSetProperty16K();
    void benchmarkQueryProperty();
    void benchmarkSimpleScript();

private:
    ScriptManagerPointer makeManager(const QString &source, const QString &filename);
};

