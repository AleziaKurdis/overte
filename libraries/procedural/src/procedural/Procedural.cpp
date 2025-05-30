//
//  Created by Bradley Austin Davis on 2015/09/05
//  Copyright 2013-2015 High Fidelity, Inc.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//

#include "Procedural.h"

#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QDateTime>

#include <gpu/Batch.h>
#include <SharedUtil.h>
#include <NumericalConstants.h>
#include <GLMHelpers.h>
#include <NetworkingConstants.h>
#include <MetaverseAPI.h>
#include <shaders/Shaders.h>

#include "ShaderConstants.h"
#include "Logging.h"

Q_LOGGING_CATEGORY(proceduralLog, "hifi.gpu.procedural")

bool Procedural::enableProceduralShaders = false;

// User-data parsing constants
static const QString PROCEDURAL_USER_DATA_KEY = "ProceduralEntity";
static const QString VERTEX_URL_KEY = "vertexShaderURL";
static const QString FRAGMENT_URL_KEY = "fragmentShaderURL";
static const QString URL_KEY = "shaderUrl";
static const QString VERSION_KEY = "version";
static const QString UNIFORMS_KEY = "uniforms";
static const QString CHANNELS_KEY = "channels";

// Shader replace strings
static const std::string PROCEDURAL_BLOCK = "//PROCEDURAL_BLOCK";
static const std::string PROCEDURAL_VERSION = "//PROCEDURAL_VERSION";

bool operator==(const ProceduralData& a, const ProceduralData& b) {
    return ((a.version == b.version) &&
            (a.fragmentShaderUrl == b.fragmentShaderUrl) &&
            (a.vertexShaderUrl == b.vertexShaderUrl) &&
            (a.uniforms == b.uniforms) &&
            (a.channels == b.channels));
}

QJsonValue ProceduralData::getProceduralData(const QString& proceduralJson) {
    if (proceduralJson.isEmpty()) {
        return QJsonValue();
    }

    QJsonParseError parseError;
    auto doc = QJsonDocument::fromJson(proceduralJson.toUtf8(), &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        return QJsonValue();
    }

    auto object = doc.object();
    auto userDataIt = object.find(PROCEDURAL_USER_DATA_KEY);
    if (userDataIt != object.end()) {
        return userDataIt.value();
    }

    return object;
}

ProceduralData ProceduralData::parse(const QString& proceduralData) {
    ProceduralData result;
    result.parse(getProceduralData(proceduralData).toObject());
    return result;
}

void ProceduralData::parse(const QJsonObject& proceduralData) {
    if (proceduralData.isEmpty()) {
        return;
    }

    {
        auto versionJson = proceduralData[VERSION_KEY];
        if (versionJson.isDouble()) {
            version = (uint8_t)(floor(versionJson.toDouble()));
            // invalid version
            if (!(version == 1 || version == 2 || version == 3 || version == 4)) {
                return;
            }
        } else {
            // All unversioned shaders default to V1
            version = 1;
        }
    }

    { // Fragment shader URL (either fragmentShaderURL or shaderUrl)
        auto rawShaderUrl = proceduralData[FRAGMENT_URL_KEY].toString();
        fragmentShaderUrl = DependencyManager::get<ResourceManager>()->normalizeURL(rawShaderUrl);

        if (fragmentShaderUrl.isEmpty()) {
            rawShaderUrl = proceduralData[URL_KEY].toString();
            fragmentShaderUrl = DependencyManager::get<ResourceManager>()->normalizeURL(rawShaderUrl);
        }
    }

    {  // Vertex shader URL
        auto rawShaderUrl = proceduralData[VERTEX_URL_KEY].toString();
        vertexShaderUrl = DependencyManager::get<ResourceManager>()->normalizeURL(rawShaderUrl);
    }

    uniforms = proceduralData[UNIFORMS_KEY].toObject();
    channels = proceduralData[CHANNELS_KEY].toArray();
}

std::function<void(gpu::StatePointer, bool)> Procedural::opaqueStencil = [](gpu::StatePointer state, bool useAA) {};
std::function<void(gpu::StatePointer)> Procedural::transparentStencil = [](gpu::StatePointer state) {};

Procedural::Procedural(bool useAA) {
    _opaqueState->setCullMode(gpu::State::CULL_NONE);
    _opaqueState->setDepthTest(true, true, gpu::LESS_EQUAL);
    _opaqueState->setBlendFunction(false,
        gpu::State::SRC_ALPHA, gpu::State::BLEND_OP_ADD, gpu::State::INV_SRC_ALPHA,
        gpu::State::FACTOR_ALPHA, gpu::State::BLEND_OP_ADD, gpu::State::ONE);
    opaqueStencil(_opaqueState, useAA);

    _transparentState->setCullMode(gpu::State::CULL_NONE);
    _transparentState->setDepthTest(true, false, gpu::LESS_EQUAL);
    _transparentState->setBlendFunction(true,
        gpu::State::SRC_ALPHA, gpu::State::BLEND_OP_ADD, gpu::State::INV_SRC_ALPHA,
        gpu::State::FACTOR_ALPHA, gpu::State::BLEND_OP_ADD, gpu::State::ONE);
    transparentStencil(_transparentState);

    _standardInputsBuffer = std::make_shared<gpu::Buffer>(sizeof(StandardInputs), nullptr);
}

void Procedural::setProceduralData(const ProceduralData& proceduralData) {
    std::lock_guard<std::mutex> lock(_mutex);
    if (proceduralData == _data) {
        return;
    }

    _enabled = false;

    if (proceduralData.version != _data.version) {
        _data.version = proceduralData.version;
        _shaderDirty = true;
    }

    if (proceduralData.uniforms != _data.uniforms) {
        // If the uniform keys changed, we need to recreate the whole shader to handle the reflection
        if (proceduralData.uniforms.keys() != _data.uniforms.keys()) {
            _shaderDirty = true;
        }
        _data.uniforms = proceduralData.uniforms;
        _uniformsDirty = true;
    }

    if (proceduralData.channels != _data.channels) {
        _data.channels = proceduralData.channels;

        static gpu::Sampler defaultSampler;
        static std::once_flag once;
        std::call_once(once, [&] {
            defaultSampler = gpu::Sampler(gpu::Sampler::FILTER_MIN_MAG_MIP_LINEAR);
        });

        // Must happen on the main thread
        auto textureCache = DependencyManager::get<TextureCache>();
        size_t channelCount = std::min(MAX_PROCEDURAL_TEXTURE_CHANNELS, (size_t)proceduralData.channels.size());
        for (size_t channel = 0; channel < MAX_PROCEDURAL_TEXTURE_CHANNELS; ++channel) {
            if (channel < channelCount) {
                QJsonValue channelValue = proceduralData.channels.at((int)channel);
                if (channelValue.isString()) {
                    QString url = channelValue.toString();
                    _channels[channel] = textureCache->getTexture(QUrl(url));
                    _samplers[channel] = defaultSampler;
                    continue;
                } else if (channelValue.isObject()) {
                    QJsonObject channelMap = channelValue.toObject();

                    auto urlItr = channelMap.constFind("url");
                    if (urlItr != channelMap.constEnd() && urlItr->isString()) {
                        _channels[channel] = textureCache->getTexture(QUrl(urlItr->toString()));

                        auto samplerItr = channelMap.constFind("sampler");
                        if (samplerItr != channelMap.constEnd() && samplerItr->isObject()) {
                            _samplers[channel] = gpu::Sampler::parseSampler(samplerItr->toObject());
                        } else {
                            _samplers[channel] = defaultSampler;
                        }
                        continue;
                    }
                }
            }

            // Fallthrough case:
            // Release those textures no longer in use
            _channels[channel] = textureCache->getTexture(QUrl());
            _samplers[channel] = defaultSampler;
        }
    }

    if (proceduralData.fragmentShaderUrl != _data.fragmentShaderUrl) {
        _data.fragmentShaderUrl = proceduralData.fragmentShaderUrl;

        _shaderDirty = true;
        _networkFragmentShader.reset();
        _fragmentShaderPath.clear();
        _fragmentShaderSource.clear();

        if (!_data.fragmentShaderUrl.isValid()) {
            qCWarning(proceduralLog) << "Invalid fragment shader URL: " << _data.fragmentShaderUrl;
            return;
        }

        if (_data.fragmentShaderUrl.isLocalFile()) {
            if (!QFileInfo(_data.fragmentShaderUrl.toLocalFile()).exists()) {
                qCWarning(proceduralLog) << "Invalid fragment shader URL, missing local file: " << _data.fragmentShaderUrl;
                return;
            }
            _fragmentShaderPath = _data.fragmentShaderUrl.toLocalFile();
        } else if (_data.fragmentShaderUrl.scheme() == URL_SCHEME_QRC) {
            _fragmentShaderPath = ":" + _data.fragmentShaderUrl.path();
        } else {
            _networkFragmentShader = ShaderCache::instance().getShader(_data.fragmentShaderUrl);
        }
    }

    if (proceduralData.vertexShaderUrl != _data.vertexShaderUrl) {
        _data.vertexShaderUrl = proceduralData.vertexShaderUrl;

        _shaderDirty = true;
        _networkVertexShader.reset();
        _vertexShaderPath.clear();
        _vertexShaderSource.clear();

        if (!_data.vertexShaderUrl.isValid()) {
            qCWarning(proceduralLog) << "Invalid vertex shader URL: " << _data.vertexShaderUrl;
            return;
        }

        if (_data.vertexShaderUrl.isLocalFile()) {
            if (!QFileInfo(_data.vertexShaderUrl.toLocalFile()).exists()) {
                qCWarning(proceduralLog) << "Invalid vertex shader URL, missing local file: " << _data.vertexShaderUrl;
                return;
            }
            _vertexShaderPath = _data.vertexShaderUrl.toLocalFile();
        } else if (_data.vertexShaderUrl.scheme() == URL_SCHEME_QRC) {
            _vertexShaderPath = ":" + _data.vertexShaderUrl.path();
        } else {
            _networkVertexShader = ShaderCache::instance().getShader(_data.vertexShaderUrl);
        }

    }

    _enabled = true;
}

bool Procedural::isReady() const {
#if defined(USE_GLES)
    return false;
#endif

    std::lock_guard<std::mutex> lock(_mutex);

    if (!_enabled) {
        return false;
    }

    if (!_hasStartedFade) {
        _fadeStartTime = usecTimestampNow();
    }

    // We need to have at least one shader, and whichever ones we have need to be loaded
    bool hasFragmentShader = !_fragmentShaderPath.isEmpty() || _networkFragmentShader;
    bool fragmentShaderLoaded = !_fragmentShaderPath.isEmpty() || (_networkFragmentShader && _networkFragmentShader->isLoaded());
    bool hasVertexShader = !_vertexShaderPath.isEmpty() || _networkVertexShader;
    bool vertexShaderLoaded = !_vertexShaderPath.isEmpty() || (_networkVertexShader && _networkVertexShader->isLoaded());
    if ((!hasFragmentShader && !hasVertexShader) || (hasFragmentShader && !fragmentShaderLoaded) || (hasVertexShader && !vertexShaderLoaded)) {
        return false;
    }

    // Do we have textures, and if so, are they loaded?
    for (size_t i = 0; i < MAX_PROCEDURAL_TEXTURE_CHANNELS; ++i) {
        if (_channels[i] && !_channels[i]->isLoaded()) {
            return false;
        }
    }

    if (!_hasStartedFade) {
        _hasStartedFade = true;
        _isFading = true;
    }

    return true;
}

void Procedural::prepare(gpu::Batch& batch,
                         const glm::vec3& position,
                         const glm::vec3& size,
                         const glm::quat& orientation,
                         const uint64_t& created,
                         const ProceduralProgramKey key) {
    std::lock_guard<std::mutex> lock(_mutex);
    _entityDimensions = size;
    _entityPosition = position;
    _entityOrientation = glm::mat3_cast(orientation);
    _entityCreated = created;
    if (!_fragmentShaderPath.isEmpty()) {
        auto lastModified = (uint64_t)QFileInfo(_fragmentShaderPath).lastModified().toMSecsSinceEpoch();
        if (lastModified > _fragmentShaderModified) {
            QFile file(_fragmentShaderPath);
            file.open(QIODevice::ReadOnly);
            _fragmentShaderSource = QTextStream(&file).readAll();
            _shaderDirty = true;
            _fragmentShaderModified = lastModified;
        }
    } else if (_fragmentShaderSource.isEmpty() && _networkFragmentShader && _networkFragmentShader->isLoaded()) {
        _fragmentShaderSource = _networkFragmentShader->_source;
        _shaderDirty = true;
    }

    if (!_vertexShaderPath.isEmpty()) {
        auto lastModified = (uint64_t)QFileInfo(_vertexShaderPath).lastModified().toMSecsSinceEpoch();
        if (lastModified > _vertexShaderModified) {
            QFile file(_vertexShaderPath);
            file.open(QIODevice::ReadOnly);
            _vertexShaderSource = QTextStream(&file).readAll();
            _shaderDirty = true;
            _vertexShaderModified = lastModified;
        }
    } else if (_vertexShaderSource.isEmpty() && _networkVertexShader && _networkVertexShader->isLoaded()) {
        _vertexShaderSource = _networkVertexShader->_source;
        _shaderDirty = true;
    }

    if (_shaderDirty) {
        _proceduralPipelines.clear();
    }

    auto pipeline = _proceduralPipelines.find(key);
    bool recompiledShader = false;
    if (pipeline == _proceduralPipelines.end()) {
        gpu::Shader::Source vertexSource;
        if (key.isSkinnedDQ()) {
            vertexSource = _vertexSourceSkinnedDQ;
        } else if (key.isSkinned()) {
            vertexSource = _vertexSourceSkinned;
        } else {
            vertexSource = _vertexSource;
        }

        gpu::Shader::Source& fragmentSource = (key.isTransparent() && _transparentFragmentSource.valid()) ? _transparentFragmentSource : _opaqueFragmentSource;

        // Build the fragment and vertex shaders
        auto versionDefine = "#define PROCEDURAL_V" + std::to_string(_data.version);
        fragmentSource.replacements = _fragmentReplacements;
        fragmentSource.replacements[PROCEDURAL_VERSION] = versionDefine;
        if (!_fragmentShaderSource.isEmpty()) {
            fragmentSource.replacements[PROCEDURAL_BLOCK] = _fragmentShaderSource.toStdString();
        }
        vertexSource.replacements = _vertexReplacements;
        vertexSource.replacements[PROCEDURAL_VERSION] = versionDefine;
        if (!_vertexShaderSource.isEmpty()) {
            vertexSource.replacements[PROCEDURAL_BLOCK] = _vertexShaderSource.toStdString();
        }

        // Set any userdata specified uniforms (if any)
        if (!_data.uniforms.empty()) {
            // First grab all the possible dialect/variant/reflections
            std::vector<shader::Reflection*> allFragmentReflections;
            for (auto dialectIt = fragmentSource.dialectSources.begin(); dialectIt != fragmentSource.dialectSources.end(); ++dialectIt) {
                for (auto variantIt = (*dialectIt).second.variantSources.begin(); variantIt != (*dialectIt).second.variantSources.end(); ++variantIt) {
                    allFragmentReflections.push_back(&(*variantIt).second.reflection);
                }
            }
            std::vector<shader::Reflection*> allVertexReflections;
            for (auto dialectIt = vertexSource.dialectSources.begin(); dialectIt != vertexSource.dialectSources.end(); ++dialectIt) {
                for (auto variantIt = (*dialectIt).second.variantSources.begin(); variantIt != (*dialectIt).second.variantSources.end(); ++variantIt) {
                    allVertexReflections.push_back(&(*variantIt).second.reflection);
                }
            }
            // Then fill in every reflections the new custom bindings
            size_t customSlot = procedural::slot::uniform::Custom;
            _slotMap.clear();
            for (const auto& key : _data.uniforms.keys()) {
                bool isArrayUniform = false;
                size_t numSlots = 0;
                const QJsonValue& value = _data.uniforms[key];
                if (value.isDouble()) {
                    numSlots = 1;
                } else if (value.isArray()) {
                    const QJsonArray valueArray = value.toArray();
                    if (valueArray.size() > 0) {
                        if (valueArray[0].isArray()) {
                            const size_t valueLength = valueArray[0].toArray().size();
                            size_t count = 0;
                            for (const QJsonValue& value : valueArray) {
                                if (value.isArray()) {
                                    const QJsonArray innerValueArray = value.toArray();
                                    if (innerValueArray.size() == valueLength) {
                                        if (valueLength == 3 || valueLength == 4 || valueLength == 9 || valueLength == 16) {
                                            count++;
                                            isArrayUniform = true;
                                        }
                                    }
                                }
                            }
                            numSlots = count;
                        } else if (valueArray[0].isDouble()) {
                            numSlots = 1;
                        }
                    }
                }

                if (numSlots > 0) {
                    std::string uniformName = key.toLocal8Bit().data();
                    std::string trueUniformName = uniformName;
                    if (isArrayUniform) {
                        trueUniformName += "[0]";
                    }
                    for (auto reflection : allFragmentReflections) {
                        reflection->uniforms[trueUniformName] = customSlot;
                    }
                    for (auto reflection : allVertexReflections) {
                        reflection->uniforms[trueUniformName] = customSlot;
                    }
                    _slotMap[uniformName] = customSlot;
                    customSlot += numSlots;
                }
            }
        }

        // Leave this here for debugging
        //qCDebug(proceduralLog) << "FragmentShader:\n" << fragmentSource.getSource(shader::Dialect::glsl450, shader::Variant::Mono).c_str();
        //qCDebug(proceduralLog) << "VertexShader:\n" << vertexSource.getSource(shader::Dialect::glsl450, shader::Variant::Mono).c_str();

        gpu::ShaderPointer vertexShader = gpu::Shader::createVertex(vertexSource);
        gpu::ShaderPointer fragmentShader = gpu::Shader::createPixel(fragmentSource);
        gpu::ShaderPointer program = gpu::Shader::createProgram(vertexShader, fragmentShader);

        _proceduralPipelines[key] = gpu::Pipeline::create(program, key.isTransparent() ? _transparentState : _opaqueState);

        // Error fallback: pink checkerboard
        if (_errorFallbackFragmentSource.isEmpty()) {
            QFile file(_errorFallbackFragmentPath);
            file.open(QIODevice::ReadOnly);
            _errorFallbackFragmentSource = QTextStream(&file).readAll();
        }
        vertexSource.replacements.erase(PROCEDURAL_BLOCK);
        fragmentSource.replacements[PROCEDURAL_BLOCK] = _errorFallbackFragmentSource.toStdString();
        gpu::ShaderPointer errorVertexShader = gpu::Shader::createVertex(vertexSource);
        gpu::ShaderPointer errorFragmentShader = gpu::Shader::createPixel(fragmentSource);
        gpu::ShaderPointer errorProgram = gpu::Shader::createProgram(errorVertexShader, errorFragmentShader);
        _errorPipelines[key] = gpu::Pipeline::create(errorProgram, _opaqueState);

        // Disabled fallback: nothing
        vertexSource.replacements.erase(PROCEDURAL_BLOCK);
        fragmentSource.replacements.erase(PROCEDURAL_BLOCK);
        gpu::ShaderPointer disabledVertexShader = gpu::Shader::createVertex(vertexSource);
        gpu::ShaderPointer disabledFragmentShader = gpu::Shader::createPixel(fragmentSource);
        gpu::ShaderPointer disabledProgram = gpu::Shader::createProgram(disabledVertexShader, disabledFragmentShader);
        _disabledPipelines[key] = gpu::Pipeline::create(disabledProgram, _opaqueState);

        _lastCompile = usecTimestampNow();
        if (_firstCompile == 0) {
            _firstCompile = _lastCompile;
        }
        _frameCount = 0;
        recompiledShader = true;
    }

    gpu::PipelinePointer finalPipeline = recompiledShader ? _proceduralPipelines[key] : pipeline->second;
    if (!enableProceduralShaders) {
        finalPipeline = _disabledPipelines[key];
    } else if (!finalPipeline || finalPipeline->getProgram()->compilationHasFailed()) {
        finalPipeline = _errorPipelines[key];
    }

    // FIXME: need to handle forward rendering
    batch.setPipeline(finalPipeline);

    bool recreateUniforms = _shaderDirty || _uniformsDirty || recompiledShader || _prevKey != key;
    if (recreateUniforms) {
        setupUniforms();
    }

    _prevKey = key;
    _shaderDirty = _uniformsDirty = false;

    for (auto lambda : _uniforms) {
        lambda(batch);
    }

    for (size_t i = 0; i < MAX_PROCEDURAL_TEXTURE_CHANNELS; ++i) {
        if (_channels[i] && _channels[i]->isLoaded()) {
            auto gpuTexture = _channels[i]->getGPUTexture();
            if (gpuTexture) {
                gpuTexture->setSampler(_samplers[i]);
                gpuTexture->setAutoGenerateMips(true);
            }
            batch.setResourceTexture((gpu::uint32)(procedural::slot::texture::Channel0 + i), gpuTexture);
        }
    }
}

void Procedural::setupUniforms() {
    _uniforms.clear();
    // Set any userdata specified uniforms
    for (const auto& key : _data.uniforms.keys()) {
        const std::string uniformName = key.toLocal8Bit().data();
        auto slotItr = _slotMap.find(uniformName);
        if (slotItr == _slotMap.end()) {
            continue;
        }

        const size_t slot = slotItr->second;
        const QJsonValue& value = _data.uniforms[key];
        if (value.isDouble()) {
            const float v = value.toDouble();
            _uniforms.push_back([slot, v](gpu::Batch& batch) { batch._glUniform1f(slot, v); });
        } else if (value.isArray()) {
            const QJsonArray valueArray = value.toArray();
            if (valueArray.size() > 0) {
                if (valueArray[0].isArray()) {
                    const size_t valueLength = valueArray[0].toArray().size();
                    std::vector<float> vs;
                    vs.reserve(valueLength * valueArray.size());
                    size_t count = 0;
                    for (const QJsonValue& value : valueArray) {
                        if (value.isArray()) {
                            const QJsonArray innerValueArray = value.toArray();
                            if (innerValueArray.size() == valueLength) {
                                if (valueLength == 3 || valueLength == 4 || valueLength == 9 || valueLength == 16) {
                                    for (size_t i = 0; i < valueLength; i++) {
                                        vs.push_back(innerValueArray[i].toDouble());
                                    }
                                    count++;
                                }
                            }
                        }
                    }
                    if (count > 0) {
                        switch (valueLength) {
                            case 3: {
                                _uniforms.push_back([slot, vs, count](gpu::Batch& batch) { batch._glUniform3fv(slot, count, vs.data()); });
                                break;
                            }
                            case 4: {
                                _uniforms.push_back([slot, vs, count](gpu::Batch& batch) { batch._glUniform4fv(slot, count, vs.data()); });
                                break;
                            }
                            case 9: {
                                _uniforms.push_back([slot, vs, count](gpu::Batch& batch) { batch._glUniformMatrix3fv(slot, count, false, vs.data()); });
                                break;
                            }
                            case 16: {
                                _uniforms.push_back([slot, vs, count](gpu::Batch& batch) { batch._glUniformMatrix4fv(slot, count, false, vs.data()); });
                                break;
                            }
                            default:
                                break;
                        }
                    }
                } else if (valueArray[0].isDouble()) {
                    switch (valueArray.size()) {
                        case 1: {
                            const float v = valueArray[0].toDouble();
                            _uniforms.push_back([slot, v](gpu::Batch& batch) { batch._glUniform(slot, v); });
                            break;
                        }
                        case 2: {
                            const glm::vec2 v{ valueArray[0].toDouble(), valueArray[1].toDouble() };
                            _uniforms.push_back([slot, v](gpu::Batch& batch) { batch._glUniform(slot, v); });
                            break;
                        }
                        case 3: {
                            const glm::vec3 v{
                                valueArray[0].toDouble(),
                                valueArray[1].toDouble(),
                                valueArray[2].toDouble(),
                            };
                            _uniforms.push_back([slot, v](gpu::Batch& batch) { batch._glUniform(slot, v); });
                            break;
                        }
                        case 4: {
                            const glm::vec4 v{
                                valueArray[0].toDouble(),
                                valueArray[1].toDouble(),
                                valueArray[2].toDouble(),
                                valueArray[3].toDouble(),
                            };
                            _uniforms.push_back([slot, v](gpu::Batch& batch) { batch._glUniform(slot, v); });
                            break;
                        }
                        case 9: {
                            const glm::mat3 m{
                                valueArray[0].toDouble(),
                                valueArray[1].toDouble(),
                                valueArray[2].toDouble(),
                                valueArray[3].toDouble(),
                                valueArray[4].toDouble(),
                                valueArray[5].toDouble(),
                                valueArray[6].toDouble(),
                                valueArray[7].toDouble(),
                                valueArray[8].toDouble(),
                            };
                            _uniforms.push_back([slot, m](gpu::Batch& batch) { batch._glUniform(slot, m); });
                            break;
                        }
                        case 16: {
                            const glm::mat4 m{
                                valueArray[0].toDouble(),
                                valueArray[1].toDouble(),
                                valueArray[2].toDouble(),
                                valueArray[3].toDouble(),
                                valueArray[4].toDouble(),
                                valueArray[5].toDouble(),
                                valueArray[6].toDouble(),
                                valueArray[7].toDouble(),
                                valueArray[8].toDouble(),
                                valueArray[9].toDouble(),
                                valueArray[10].toDouble(),
                                valueArray[11].toDouble(),
                                valueArray[12].toDouble(),
                                valueArray[13].toDouble(),
                                valueArray[14].toDouble(),
                                valueArray[15].toDouble(),
                            };
                            _uniforms.push_back([slot, m](gpu::Batch& batch) { batch._glUniform(slot, m); });
                            break;
                        }
                        default:
                            break;
                    }
                }
            }
        }
    }

    _uniforms.push_back([this](gpu::Batch& batch) {
        _standardInputs.position = vec4(_entityPosition, 1.0f);
        // Minimize floating point error by doing an integer division to milliseconds, before the floating point division to seconds
        auto now = usecTimestampNow();
        _standardInputs.timeSinceLastCompile = (float)((now - _lastCompile) / USECS_PER_MSEC) / MSECS_PER_SECOND;
        _standardInputs.timeSinceFirstCompile = (float)((now - _firstCompile) / USECS_PER_MSEC) / MSECS_PER_SECOND;
        _standardInputs.timeSinceEntityCreation = (float)((now - _entityCreated) / USECS_PER_MSEC) / MSECS_PER_SECOND;


        // Date
        {
            QDateTime now = QDateTime::currentDateTimeUtc();
            QDate date = now.date();
            QTime time = now.time();
            _standardInputs.date.x = date.year();
            // Shadertoy month is 0 based
            _standardInputs.date.y = date.month() - 1;
            // But not the day... go figure
            _standardInputs.date.z = date.day();
            float fractSeconds = (time.msec() / 1000.0f);
            _standardInputs.date.w = (time.hour() * 3600) + (time.minute() * 60) + time.second() + fractSeconds;
        }

        _standardInputs.scale = vec4(_entityDimensions, 1.0f);
        _standardInputs.frameCount = ++_frameCount;
        _standardInputs.orientation = mat4(_entityOrientation);

        for (size_t i = 0; i < MAX_PROCEDURAL_TEXTURE_CHANNELS; ++i) {
            if (_channels[i]) {
                _standardInputs.resolution[i] = vec4(_channels[i]->getWidth(), _channels[i]->getHeight(), 1.0f, 1.0f);
            } else {
                _standardInputs.resolution[i] = vec4(1.0f);
            }
        }

        _standardInputsBuffer->setSubData(0, _standardInputs);
        batch.setUniformBuffer(procedural::slot::buffer::Inputs, _standardInputsBuffer, 0, sizeof(StandardInputs));
    });
}

glm::vec4 Procedural::getColor(const glm::vec4& entityColor) const {
    if (_data.version == 1) {
        return glm::vec4(1);
    }
    return entityColor;
}

bool Procedural::hasVertexShader() const {
    std::lock_guard<std::mutex> lock(_mutex);
    return !_data.vertexShaderUrl.isEmpty();
}


void Procedural::setVertexReplacements(const std::unordered_map<std::string, std::string>& replacements) {
    std::lock_guard<std::mutex> lock(_mutex);
    _vertexReplacements = replacements;
    _shaderDirty = true;
}

void Procedural::setFragmentReplacements(const std::unordered_map<std::string, std::string>& replacements) {
    std::lock_guard<std::mutex> lock(_mutex);
    _fragmentReplacements = replacements;
    _shaderDirty = true;
}

void graphics::ProceduralMaterial::initializeProcedural() {
    _procedural._vertexSource = gpu::Shader::getVertexShaderSource(shader::render_utils::vertex::simple_procedural);
    _procedural._vertexSourceSkinned = gpu::Shader::getVertexShaderSource(shader::render_utils::vertex::simple_procedural_deformed);
    _procedural._vertexSourceSkinnedDQ = gpu::Shader::getVertexShaderSource(shader::render_utils::vertex::simple_procedural_deformeddq);

    // FIXME: Setup proper uniform slots and use correct pipelines for forward rendering
    _procedural._opaqueFragmentSource = gpu::Shader::getFragmentShaderSource(shader::render_utils::fragment::simple_procedural);
    _procedural._transparentFragmentSource = gpu::Shader::getFragmentShaderSource(shader::render_utils::fragment::simple_procedural_translucent);

    _procedural._errorFallbackFragmentPath = ":" + QUrl("qrc:///shaders/errorShader.frag").path();
}
