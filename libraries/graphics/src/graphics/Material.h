//
//  Material.h
//  libraries/graphics/src/graphics
//
//  Created by Sam Gateau on 12/10/2014.
//  Copyright 2014 High Fidelity, Inc.
//  Copyright 2024 Overte e.V.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//
#ifndef hifi_model_Material_h
#define hifi_model_Material_h

#include <mutex>
#include <bitset>
#include <unordered_map>
#include <queue>

#include <ColorUtils.h>

#include <gpu/Resource.h>
#include <gpu/Texture.h>
#include <gpu/TextureTable.h>

#include "MaterialMappingMode.h"

class Transform;

namespace graphics {

class TextureMap;
typedef std::shared_ptr< TextureMap > TextureMapPointer;

// Material Key is a coarse trait description of a material used to classify the materials
class MaterialKey {
public:
    // Be careful changing these, they need to match up with the bits in graphics/Material.slh
   enum FlagBit {
        EMISSIVE_VAL_BIT = 0,
        UNLIT_VAL_BIT,
        ALBEDO_VAL_BIT,
        METALLIC_VAL_BIT,
        GLOSSY_VAL_BIT,
        OPACITY_VAL_BIT,
        OPACITY_MASK_MAP_BIT,           // Opacity Map and Opacity MASK map are mutually exclusive
        OPACITY_TRANSLUCENT_MAP_BIT,
        OPACITY_MAP_MODE_BIT,           // Opacity map mode bit is set if the value has set explicitely and not deduced from the textures assigned 
        OPACITY_CUTOFF_VAL_BIT,
        SCATTERING_VAL_BIT,

        // THe map bits must be in the same sequence as the enum names for the map channels
        EMISSIVE_MAP_BIT,
        ALBEDO_MAP_BIT,
        METALLIC_MAP_BIT,
        ROUGHNESS_MAP_BIT,
        NORMAL_MAP_BIT,
        OCCLUSION_MAP_BIT,
        LIGHT_MAP_BIT,
        SCATTERING_MAP_BIT,

        EXTRA_1_BIT,
        EXTRA_2_BIT,
        EXTRA_3_BIT,
        EXTRA_4_BIT,
        EXTRA_5_BIT,

        SPLAT_MAP_BIT, // Splat maps work differently than the other maps, so we put it at the end to not interfere with the extra bits

        NUM_FLAGS,
    };
    typedef std::bitset<NUM_FLAGS> Flags;

    enum MapChannel {
        EMISSIVE_MAP = 0,
        ALBEDO_MAP,
        METALLIC_MAP,
        ROUGHNESS_MAP,
        NORMAL_MAP,
        OCCLUSION_MAP,
        LIGHT_MAP,
        SCATTERING_MAP,
        SPLAT_MAP,

        NUM_MAP_CHANNELS,
    };

    enum OpacityMapMode {
        OPACITY_MAP_OPAQUE = 0,
        OPACITY_MAP_MASK,
        OPACITY_MAP_BLEND,
    };
    static std::string getOpacityMapModeName(OpacityMapMode mode);
    // find the enum value from a string, return true if match found
    static bool getOpacityMapModeFromName(const std::string& modeName, OpacityMapMode& mode);

    enum CullFaceMode {
        CULL_NONE = 0,
        CULL_FRONT,
        CULL_BACK,

        NUM_CULL_FACE_MODES
    };
    static std::string getCullFaceModeName(CullFaceMode mode);
    static bool getCullFaceModeFromName(const std::string& modeName, CullFaceMode& mode);

    // The signature is the Flags
    Flags _flags;

    MaterialKey() : _flags(0) {}
    MaterialKey(const Flags& flags) : _flags(flags) {}

    class Builder {
        Flags _flags{ 0 };
    public:
        Builder() {}

        MaterialKey build() const { return MaterialKey(_flags); }

        Builder& withEmissive() { _flags.set(EMISSIVE_VAL_BIT); return (*this); }
        Builder& withUnlit() { _flags.set(UNLIT_VAL_BIT); return (*this); }

        Builder& withAlbedo() { _flags.set(ALBEDO_VAL_BIT); return (*this); }
        Builder& withMetallic() { _flags.set(METALLIC_VAL_BIT); return (*this); }
        Builder& withGlossy() { _flags.set(GLOSSY_VAL_BIT); return (*this); }

        Builder& withTranslucentFactor() { _flags.set(OPACITY_VAL_BIT); return (*this); }
        Builder& withTranslucentMap() { _flags.set(OPACITY_TRANSLUCENT_MAP_BIT); return (*this); }
        Builder& withMaskMap() { _flags.set(OPACITY_MASK_MAP_BIT); return (*this); }
        Builder& withOpacityMapMode(OpacityMapMode mode) {
            switch (mode) {
            case OPACITY_MAP_OPAQUE:
                _flags.reset(OPACITY_TRANSLUCENT_MAP_BIT);
                _flags.reset(OPACITY_MASK_MAP_BIT);
                break;
            case OPACITY_MAP_MASK:
                _flags.reset(OPACITY_TRANSLUCENT_MAP_BIT);
                _flags.set(OPACITY_MASK_MAP_BIT);
                break;
            case OPACITY_MAP_BLEND:
                _flags.set(OPACITY_TRANSLUCENT_MAP_BIT);
                _flags.reset(OPACITY_MASK_MAP_BIT);
                break;
            };
            _flags.set(OPACITY_MAP_MODE_BIT); // Intentionally set the mode!
            return (*this);
        }
        Builder& withOpacityCutoff() { _flags.set(OPACITY_CUTOFF_VAL_BIT); return (*this); }

        Builder& withScattering() { _flags.set(SCATTERING_VAL_BIT); return (*this); }

        Builder& withEmissiveMap() { _flags.set(EMISSIVE_MAP_BIT); return (*this); }
        Builder& withAlbedoMap() { _flags.set(ALBEDO_MAP_BIT); return (*this); }
        Builder& withMetallicMap() { _flags.set(METALLIC_MAP_BIT); return (*this); }
        Builder& withRoughnessMap() { _flags.set(ROUGHNESS_MAP_BIT); return (*this); }

        Builder& withNormalMap() { _flags.set(NORMAL_MAP_BIT); return (*this); }
        Builder& withOcclusionMap() { _flags.set(OCCLUSION_MAP_BIT); return (*this); }
        Builder& withLightMap() { _flags.set(LIGHT_MAP_BIT); return (*this); }
        Builder& withScatteringMap() { _flags.set(SCATTERING_MAP_BIT); return (*this); }

        // Convenient standard keys that we will keep on using all over the place
        static MaterialKey opaqueAlbedo() { return Builder().withAlbedo().build(); }
    };

    void setEmissive(bool value) { _flags.set(EMISSIVE_VAL_BIT, value); }
    bool isEmissive() const { return _flags[EMISSIVE_VAL_BIT]; }

    void setUnlit(bool value) { _flags.set(UNLIT_VAL_BIT, value); }
    bool isUnlit() const { return _flags[UNLIT_VAL_BIT]; }

    void setEmissiveMap(bool value) { _flags.set(EMISSIVE_MAP_BIT, value); }
    bool isEmissiveMap() const { return _flags[EMISSIVE_MAP_BIT]; }
 
    void setAlbedo(bool value) { _flags.set(ALBEDO_VAL_BIT, value); }
    bool isAlbedo() const { return _flags[ALBEDO_VAL_BIT]; }

    void setAlbedoMap(bool value) { _flags.set(ALBEDO_MAP_BIT, value); }
    bool isAlbedoMap() const { return _flags[ALBEDO_MAP_BIT]; }

    void setMetallic(bool value) { _flags.set(METALLIC_VAL_BIT, value); }
    bool isMetallic() const { return _flags[METALLIC_VAL_BIT]; }

    void setMetallicMap(bool value) { _flags.set(METALLIC_MAP_BIT, value); }
    bool isMetallicMap() const { return _flags[METALLIC_MAP_BIT]; }

    void setGlossy(bool value) { _flags.set(GLOSSY_VAL_BIT, value); }
    bool isGlossy() const { return _flags[GLOSSY_VAL_BIT]; }
    bool isRough() const { return !_flags[GLOSSY_VAL_BIT]; }

    void setRoughnessMap(bool value) { _flags.set(ROUGHNESS_MAP_BIT, value); }
    bool isRoughnessMap() const { return _flags[ROUGHNESS_MAP_BIT]; }

    void setTranslucentFactor(bool value) { _flags.set(OPACITY_VAL_BIT, value); }
    bool isTranslucentFactor() const { return _flags[OPACITY_VAL_BIT]; }

    void setTranslucentMap(bool value) { _flags.set(OPACITY_TRANSLUCENT_MAP_BIT, value); }
    bool isTranslucentMap() const { return _flags[OPACITY_TRANSLUCENT_MAP_BIT]; }

    void setOpacityMaskMap(bool value) { _flags.set(OPACITY_MASK_MAP_BIT, value); }
    bool isOpacityMaskMap() const { return _flags[OPACITY_MASK_MAP_BIT]; }

    void setOpacityCutoff(bool value) { _flags.set(OPACITY_CUTOFF_VAL_BIT, value); }
    bool isOpacityCutoff() const { return _flags[OPACITY_CUTOFF_VAL_BIT]; }

    void setNormalMap(bool value) { _flags.set(NORMAL_MAP_BIT, value); }
    bool isNormalMap() const { return _flags[NORMAL_MAP_BIT]; }

    void setOcclusionMap(bool value) { _flags.set(OCCLUSION_MAP_BIT, value); }
    bool isOcclusionMap() const { return _flags[OCCLUSION_MAP_BIT]; }

    void setLightMap(bool value) { _flags.set(LIGHT_MAP_BIT, value); }
    bool isLightMap() const { return _flags[LIGHT_MAP_BIT]; }

    void setScattering(bool value) { _flags.set(SCATTERING_VAL_BIT, value); }
    bool isScattering() const { return _flags[SCATTERING_VAL_BIT]; }

    void setScatteringMap(bool value) { _flags.set(SCATTERING_MAP_BIT, value); }
    bool isScatteringMap() const { return _flags[SCATTERING_MAP_BIT]; }

    void setMapChannel(MapChannel channel, bool value) { _flags.set(EMISSIVE_MAP_BIT + channel, value); }
    bool isMapChannel(MapChannel channel) const { return _flags[EMISSIVE_MAP_BIT + channel]; }


    // Translucency and Opacity Heuristics are combining several flags:
    void setOpacityMapMode(OpacityMapMode mode) {
        switch (mode) {
        case OPACITY_MAP_OPAQUE:
            _flags.reset(OPACITY_TRANSLUCENT_MAP_BIT);
            _flags.reset(OPACITY_MASK_MAP_BIT);
            break;
        case OPACITY_MAP_MASK:
            _flags.reset(OPACITY_TRANSLUCENT_MAP_BIT);
            _flags.set(OPACITY_MASK_MAP_BIT);
            break;
        case OPACITY_MAP_BLEND:
            _flags.set(OPACITY_TRANSLUCENT_MAP_BIT);
            _flags.reset(OPACITY_MASK_MAP_BIT);
            break;
        };
        _flags.set(OPACITY_MAP_MODE_BIT); // Intentionally set the mode!
    }
    bool isOpacityMapMode() const { return _flags[OPACITY_MAP_MODE_BIT]; }
    OpacityMapMode getOpacityMapMode() const { return (isOpacityMaskMap() ? OPACITY_MAP_MASK : (isTranslucentMap() ? OPACITY_MAP_BLEND : OPACITY_MAP_OPAQUE)); }

    bool isTranslucent() const { return isTranslucentFactor() || isTranslucentMap(); }
    bool isOpaque() const { return !isTranslucent(); }
    bool isSurfaceOpaque() const { return isOpaque() && !isOpacityMaskMap(); }
    bool isTexelOpaque() const { return isOpaque() && isOpacityMaskMap(); }
};

class MaterialFilter {
public:
    MaterialKey::Flags _value{ 0 };
    MaterialKey::Flags _mask{ 0 };


    MaterialFilter(const MaterialKey::Flags& value = MaterialKey::Flags(0), const MaterialKey::Flags& mask = MaterialKey::Flags(0)) : _value(value), _mask(mask) {}

    class Builder {
        MaterialKey::Flags _value{ 0 };
        MaterialKey::Flags _mask{ 0 };
    public:
        Builder() {}

        MaterialFilter build() const { return MaterialFilter(_value, _mask); }

        Builder& withoutEmissive()       { _value.reset(MaterialKey::EMISSIVE_VAL_BIT); _mask.set(MaterialKey::EMISSIVE_VAL_BIT); return (*this); }
        Builder& withEmissive()        { _value.set(MaterialKey::EMISSIVE_VAL_BIT);  _mask.set(MaterialKey::EMISSIVE_VAL_BIT); return (*this); }

        Builder& withoutEmissiveMap()       { _value.reset(MaterialKey::EMISSIVE_MAP_BIT); _mask.set(MaterialKey::EMISSIVE_MAP_BIT); return (*this); }
        Builder& withEmissiveMap()        { _value.set(MaterialKey::EMISSIVE_MAP_BIT);  _mask.set(MaterialKey::EMISSIVE_MAP_BIT); return (*this); }

        Builder& withoutUnlit()       { _value.reset(MaterialKey::UNLIT_VAL_BIT); _mask.set(MaterialKey::UNLIT_VAL_BIT); return (*this); }
        Builder& withUnlit()        { _value.set(MaterialKey::UNLIT_VAL_BIT);  _mask.set(MaterialKey::UNLIT_VAL_BIT); return (*this); }

        Builder& withoutAlbedo()       { _value.reset(MaterialKey::ALBEDO_VAL_BIT); _mask.set(MaterialKey::ALBEDO_VAL_BIT); return (*this); }
        Builder& withAlbedo()        { _value.set(MaterialKey::ALBEDO_VAL_BIT);  _mask.set(MaterialKey::ALBEDO_VAL_BIT); return (*this); }

        Builder& withoutAlbedoMap()       { _value.reset(MaterialKey::ALBEDO_MAP_BIT); _mask.set(MaterialKey::ALBEDO_MAP_BIT); return (*this); }
        Builder& withAlbedoMap()        { _value.set(MaterialKey::ALBEDO_MAP_BIT);  _mask.set(MaterialKey::ALBEDO_MAP_BIT); return (*this); }

        Builder& withoutMetallic()       { _value.reset(MaterialKey::METALLIC_VAL_BIT); _mask.set(MaterialKey::METALLIC_VAL_BIT); return (*this); }
        Builder& withMetallic()        { _value.set(MaterialKey::METALLIC_VAL_BIT);  _mask.set(MaterialKey::METALLIC_VAL_BIT); return (*this); }

        Builder& withoutMetallicMap()       { _value.reset(MaterialKey::METALLIC_MAP_BIT); _mask.set(MaterialKey::METALLIC_MAP_BIT); return (*this); }
        Builder& withMetallicMap()        { _value.set(MaterialKey::METALLIC_MAP_BIT);  _mask.set(MaterialKey::METALLIC_MAP_BIT); return (*this); }

        Builder& withoutGlossy()       { _value.reset(MaterialKey::GLOSSY_VAL_BIT); _mask.set(MaterialKey::GLOSSY_VAL_BIT); return (*this); }
        Builder& withGlossy()        { _value.set(MaterialKey::GLOSSY_VAL_BIT);  _mask.set(MaterialKey::GLOSSY_VAL_BIT); return (*this); }

        Builder& withoutRoughnessMap()       { _value.reset(MaterialKey::ROUGHNESS_MAP_BIT); _mask.set(MaterialKey::ROUGHNESS_MAP_BIT); return (*this); }
        Builder& withRoughnessMap()        { _value.set(MaterialKey::ROUGHNESS_MAP_BIT);  _mask.set(MaterialKey::ROUGHNESS_MAP_BIT); return (*this); }

        Builder& withoutTranslucentFactor()       { _value.reset(MaterialKey::OPACITY_VAL_BIT); _mask.set(MaterialKey::OPACITY_VAL_BIT); return (*this); }
        Builder& withTranslucentFactor()        { _value.set(MaterialKey::OPACITY_VAL_BIT);  _mask.set(MaterialKey::OPACITY_VAL_BIT); return (*this); }

        Builder& withoutTranslucentMap()       { _value.reset(MaterialKey::OPACITY_TRANSLUCENT_MAP_BIT); _mask.set(MaterialKey::OPACITY_TRANSLUCENT_MAP_BIT); return (*this); }
        Builder& withTranslucentMap()        { _value.set(MaterialKey::OPACITY_TRANSLUCENT_MAP_BIT);  _mask.set(MaterialKey::OPACITY_TRANSLUCENT_MAP_BIT); return (*this); }

        Builder& withoutMaskMap()       { _value.reset(MaterialKey::OPACITY_MASK_MAP_BIT); _mask.set(MaterialKey::OPACITY_MASK_MAP_BIT); return (*this); }
        Builder& withMaskMap()        { _value.set(MaterialKey::OPACITY_MASK_MAP_BIT);  _mask.set(MaterialKey::OPACITY_MASK_MAP_BIT); return (*this); }

        Builder& withoutOpacityMapMode() { _value.reset(MaterialKey::OPACITY_MAP_MODE_BIT); _mask.set(MaterialKey::OPACITY_MAP_MODE_BIT); return (*this); }
        Builder& withOpacityMapMode() { _value.set(MaterialKey::OPACITY_MAP_MODE_BIT);  _mask.set(MaterialKey::OPACITY_MAP_MODE_BIT); return (*this); }

        Builder& withoutOpacityCutoff() { _value.reset(MaterialKey::OPACITY_CUTOFF_VAL_BIT); _mask.set(MaterialKey::OPACITY_CUTOFF_VAL_BIT); return (*this); }
        Builder& withOpacityCutoff() { _value.set(MaterialKey::OPACITY_CUTOFF_VAL_BIT);  _mask.set(MaterialKey::OPACITY_CUTOFF_VAL_BIT); return (*this); }

        Builder& withoutNormalMap()       { _value.reset(MaterialKey::NORMAL_MAP_BIT); _mask.set(MaterialKey::NORMAL_MAP_BIT); return (*this); }
        Builder& withNormalMap()        { _value.set(MaterialKey::NORMAL_MAP_BIT);  _mask.set(MaterialKey::NORMAL_MAP_BIT); return (*this); }

        Builder& withoutOcclusionMap()       { _value.reset(MaterialKey::OCCLUSION_MAP_BIT); _mask.set(MaterialKey::OCCLUSION_MAP_BIT); return (*this); }
        Builder& withOcclusionMap()        { _value.set(MaterialKey::OCCLUSION_MAP_BIT);  _mask.set(MaterialKey::OCCLUSION_MAP_BIT); return (*this); }

        Builder& withoutLightMap()       { _value.reset(MaterialKey::LIGHT_MAP_BIT); _mask.set(MaterialKey::LIGHT_MAP_BIT); return (*this); }
        Builder& withLightMap()        { _value.set(MaterialKey::LIGHT_MAP_BIT);  _mask.set(MaterialKey::LIGHT_MAP_BIT); return (*this); }

        Builder& withoutScattering()       { _value.reset(MaterialKey::SCATTERING_VAL_BIT); _mask.set(MaterialKey::SCATTERING_VAL_BIT); return (*this); }
        Builder& withScattering()        { _value.set(MaterialKey::SCATTERING_VAL_BIT);  _mask.set(MaterialKey::SCATTERING_VAL_BIT); return (*this); }

        Builder& withoutScatteringMap()       { _value.reset(MaterialKey::SCATTERING_MAP_BIT); _mask.set(MaterialKey::SCATTERING_MAP_BIT); return (*this); }
        Builder& withScatteringMap()        { _value.set(MaterialKey::SCATTERING_MAP_BIT);  _mask.set(MaterialKey::SCATTERING_MAP_BIT); return (*this); }


        // Convenient standard keys that we will keep on using all over the place
        static MaterialFilter opaqueAlbedo() { return Builder().withAlbedo().withoutTranslucentFactor().build(); }
    };

    // Item Filter operator testing if a key pass the filter
    bool test(const MaterialKey& key) const { return (key._flags & _mask) == (_value & _mask); }

    class Less {
    public:
        bool operator() (const MaterialFilter& left, const MaterialFilter& right) const {
            if (left._value.to_ulong() == right._value.to_ulong()) {
                return left._mask.to_ulong() < right._mask.to_ulong();
            } else {
                return left._value.to_ulong() < right._value.to_ulong();
            }
        }
    };
};

class Material {
public:
    typedef MaterialKey::MapChannel MapChannel;
    typedef std::unordered_map<MapChannel, TextureMapPointer> TextureMaps;
    typedef std::unordered_map<MapChannel, gpu::Sampler> SamplerMap;
    typedef std::unordered_map<MapChannel, int> TexCoordSetMap;

    Material();
    Material(const Material& material);
    virtual ~Material() = default;
    Material& operator= (const Material& material);

    virtual MaterialKey getKey() const { return _key; }

    static const float DEFAULT_EMISSIVE;
    void setEmissive(const glm::vec3& emissive, bool isSRGB = true);
    virtual glm::vec3 getEmissive(bool SRGB = true) const { return (SRGB ? ColorUtils::tosRGBVec3(_emissive) : _emissive); }

    static const float DEFAULT_OPACITY;
    void setOpacity(float opacity);
    virtual float getOpacity() const { return _opacity; }

    static const MaterialKey::OpacityMapMode DEFAULT_OPACITY_MAP_MODE;
    void setOpacityMapMode(MaterialKey::OpacityMapMode opacityMapMode);
    virtual MaterialKey::OpacityMapMode getOpacityMapMode() const;

    static const float DEFAULT_OPACITY_CUTOFF;
    void setOpacityCutoff(float opacityCutoff);
    virtual float getOpacityCutoff() const { return _opacityCutoff; }

    static const MaterialKey::CullFaceMode DEFAULT_CULL_FACE_MODE;
    void setCullFaceMode(MaterialKey::CullFaceMode cullFaceMode) { _cullFaceMode = cullFaceMode; }
    virtual MaterialKey::CullFaceMode getCullFaceMode() const { return _cullFaceMode; }

    void setUnlit(bool value);
    virtual bool isUnlit() const { return _key.isUnlit(); }

    static const float DEFAULT_ALBEDO;
    void setAlbedo(const glm::vec3& albedo, bool isSRGB = true);
    virtual glm::vec3 getAlbedo(bool SRGB = true) const { return (SRGB ? ColorUtils::tosRGBVec3(_albedo) : _albedo); }

    static const float DEFAULT_METALLIC;
    void setMetallic(float metallic);
    virtual float getMetallic() const { return _metallic; }

    static const float DEFAULT_ROUGHNESS;
    void setRoughness(float roughness);
    virtual float getRoughness() const { return _roughness; }

    static const float DEFAULT_SCATTERING;
    void setScattering(float scattering);
    virtual float getScattering() const { return _scattering; }

    // The texture map to channel association
    static const int NUM_TEXCOORD_TRANSFORMS { 2 };
    void setTextureMap(MapChannel channel, const TextureMapPointer& textureMap);
    virtual TextureMaps getTextureMaps() const { return _textureMaps; } // FIXME - not thread safe...
    const TextureMapPointer getTextureMap(MapChannel channel) const;

    void setSampler(MapChannel channel, const gpu::Sampler& sampler);
    void applySampler(MapChannel channel);

    void setTexCoordSet(MapChannel channel, int texCoordSet);
    int getTexCoordSet(MapChannel channel);

    // Albedo maps cannot have opacity detected until they are loaded
    // This method allows const changing of the key/schemaBuffer without touching the map
    // return true if the opacity changed, flase otherwise
    virtual bool resetOpacityMap() const;

    // conversion from legacy material properties to PBR equivalent
    static float shininessToRoughness(float shininess) { return 1.0f - shininess / 100.0f; }

    void setTextureTransforms(const Transform& transform, MaterialMappingMode mode, bool repeat);

    const std::string& getName() const { return _name; }
    void setName(const std::string& name) { _name = name; }

    const std::string& getModel() const { return _model; }
    void setModel(const std::string& model) { _model = model; }

    virtual glm::mat4 getTexCoordTransform(uint i) const { return _texcoordTransforms[i]; }
    void setTexCoordTransform(uint i, const glm::mat4& mat4) { _texcoordTransforms[i] = mat4; }
    virtual glm::vec2 getLightmapParams() const { return _lightmapParams; }
    virtual glm::vec2 getMaterialParams() const { return _materialParams; }

    virtual uint8_t getLayers() const { return _layers; }
    void setLayers(uint8_t layers) { _layers = std::min(3, std::max(1, (int)layers)); }

    virtual bool getDefaultFallthrough() const { return _defaultFallthrough; }
    void setDefaultFallthrough(bool defaultFallthrough) { _defaultFallthrough = defaultFallthrough; }

    enum ExtraFlagBit {
        TEXCOORDTRANSFORM0 = MaterialKey::NUM_FLAGS,
        TEXCOORDTRANSFORM1,
        LIGHTMAP_PARAMS,
        MATERIAL_PARAMS,
        CULL_FACE_MODE,

        EXTRA_1_BIT,
        EXTRA_2_BIT,
        EXTRA_3_BIT,

        NUM_TOTAL_FLAGS
    };
    std::unordered_map<uint, bool> getPropertyFallthroughs() { return _propertyFallthroughs; }
    bool getPropertyFallthrough(uint property) { return _propertyFallthroughs[property]; }
    void setPropertyDoesFallthrough(uint property) { _propertyFallthroughs[property] = true; }

    virtual bool isProcedural() const { return false; }
    virtual bool isEnabled() const { return true; }
    virtual bool isReady() const { return true; }
    virtual QString getProceduralString() const { return QString(); }

    virtual bool isReference() const { return false; }

    virtual bool isMToon() const { return false; }
    static const glm::vec3 DEFAULT_SHADE;
    virtual glm::vec3 getShade(bool SRGB = true) const { return glm::vec3(0.0f); }
    static const float DEFAULT_SHADING_SHIFT;
    virtual float getShadingShift() const { return 0.0f; }
    static const float DEFAULT_SHADING_TOONY;
    virtual float getShadingToony() const { return 0.0f; }
    static const glm::vec3 DEFAULT_MATCAP;
    virtual glm::vec3 getMatcap(bool SRGB = true) const { return glm::vec3(0.0f); }
    static const glm::vec3 DEFAULT_PARAMETRIC_RIM;
    virtual glm::vec3 getParametricRim(bool SRGB = true) const { return glm::vec3(0.0f); }
    static const float DEFAULT_PARAMETRIC_RIM_FRESNEL_POWER;
    virtual float getParametricRimFresnelPower() const { return 0.0f; }
    static const float DEFAULT_PARAMETRIC_RIM_LIFT;
    virtual float getParametricRimLift() const { return 0.0f; }
    static const float DEFAULT_RIM_LIGHTING_MIX;
    virtual float getRimLightingMix() const { return 0.0f; }
    static const float DEFAULT_UV_ANIMATION_SCROLL_SPEED;
    virtual float getUVAnimationScrollXSpeed() const { return 0.0f; }
    virtual float getUVAnimationScrollYSpeed() const { return 0.0f; }
    virtual float getUVAnimationRotationSpeed() const { return 0.0f; }

    static const glm::vec3 DEFAULT_OUTLINE;
    virtual uint8_t getOutlineWidthMode() { return 0; }
    virtual float getOutlineWidth() { return 0.0f; }
    virtual glm::vec3 getOutline(bool SRGB = true) const { return glm::vec3(0.0f); }

    static const std::string HIFI_PBR;
    static const std::string HIFI_SHADER_SIMPLE;
    static const std::string VRM_MTOON;

protected:
    std::string _name { "" };
    mutable MaterialKey _key { 0 };
    std::string _model { HIFI_PBR };
    uint8_t _layers { 1 };

private:
    // Material properties
    glm::vec3 _emissive { DEFAULT_EMISSIVE };
    float _opacity { DEFAULT_OPACITY };
    glm::vec3 _albedo { DEFAULT_ALBEDO };
    float _roughness { DEFAULT_ROUGHNESS };
    float _metallic { DEFAULT_METALLIC };
    float _scattering { DEFAULT_SCATTERING };
    float _opacityCutoff { DEFAULT_OPACITY_CUTOFF };
    std::array<glm::mat4, NUM_TEXCOORD_TRANSFORMS> _texcoordTransforms;
    glm::vec2 _lightmapParams { 0.0, 1.0 };
    glm::vec2 _materialParams { 0.0, 1.0 };
    MaterialKey::CullFaceMode _cullFaceMode { DEFAULT_CULL_FACE_MODE };
    TextureMaps _textureMaps;
    SamplerMap _samplers;
    TexCoordSetMap _texCoordSets;

    bool _defaultFallthrough { false };
    std::unordered_map<uint, bool> _propertyFallthroughs { NUM_TOTAL_FLAGS };

    mutable std::recursive_mutex _textureMapsMutex;
};
typedef std::shared_ptr<Material> MaterialPointer;

class MaterialLayer {
public:
    MaterialLayer(MaterialPointer material, quint16 priority) : material(material), priority(priority) {}

    MaterialPointer material { nullptr };
    quint16 priority { 0 };
};

class MaterialLayerCompare {
public:
    bool operator() (MaterialLayer left, MaterialLayer right) {
        return left.priority < right.priority;
    }
};
typedef std::priority_queue<MaterialLayer, std::vector<MaterialLayer>, MaterialLayerCompare> MaterialLayerQueue;

class MultiMaterial : public MaterialLayerQueue {
public:
    MultiMaterial();

    void push(const MaterialLayer& value) {
        MaterialLayerQueue::push(value);
        _hasCalculatedTextureInfo = false;
        _needsUpdate = true;
    }

    bool remove(const MaterialPointer& value) {
        auto it = c.begin();
        while (it != c.end()) {
            if (it->material == value) {
                break;
            }
            it++;
        }
        if (it != c.end()) {
            c.erase(it);
            std::make_heap(c.begin(), c.end(), comp);
            _hasCalculatedTextureInfo = false;
            _needsUpdate = true;
            return true;
        } else {
            return false;
        }
    }

    // Schema to access the attribute values of the material
    class Schema {
    public:
        glm::vec3 _emissive { Material::DEFAULT_EMISSIVE }; // No Emissive
        float _opacity { Material::DEFAULT_OPACITY }; // Opacity = 1 => Not Transparent

        glm::vec3 _albedo { Material::DEFAULT_ALBEDO }; // Grey albedo => isAlbedo
        float _roughness { Material::DEFAULT_ROUGHNESS }; // Roughness = 1 => Not Glossy

        float _metallic { Material::DEFAULT_METALLIC }; // Not Metallic
        float _scattering { Material::DEFAULT_SCATTERING }; // Scattering info
        float _opacityCutoff { Material::DEFAULT_OPACITY_CUTOFF }; // Opacity cutoff applyed when using opacityMap as Mask 
        uint32_t _key { 0 }; // a copy of the materialKey

        // Texture Coord Transform Array
        glm::mat4 _texcoordTransforms[Material::NUM_TEXCOORD_TRANSFORMS];

        // x: material mode (0 for UV, 1 for PROJECTED)
        // y: 1 for texture repeat, 0 for discard outside of 0 - 1
        glm::vec2 _materialParams { 0.0, 1.0 };

        glm::vec2 _lightmapParams { 0.0, 1.0 };

        glm::vec3 spare { 0.0f };
        uint32_t _texCoordSets { 0 };

        Schema() {
            for (auto& transform : _texcoordTransforms) {
                transform = glm::mat4();
            }
        }

        void setTexCoordSet(int channel, int texCoordSet) {
            // We currently only support two texCoord sets (0 and 1), but eventually we want to support 4, so we use 2 bits
            const int MAX_TEX_COORD_SET = 1;
            _texCoordSets |= std::min(texCoordSet, MAX_TEX_COORD_SET) << (2 * channel);
        }
    };

    class MToonSchema {
    public:
        glm::vec3 _emissive { Material::DEFAULT_EMISSIVE }; // No Emissive
        float _opacity { Material::DEFAULT_OPACITY }; // Opacity = 1 => Not Transparent

        glm::vec3 _albedo { Material::DEFAULT_ALBEDO }; // Grey albedo => isAlbedo
        float _opacityCutoff { Material::DEFAULT_OPACITY_CUTOFF }; // Opacity cutoff applyed when using opacityMap as Mask

        glm::vec3 _shade { Material::DEFAULT_SHADE };
        float _shadingShift { Material::DEFAULT_SHADING_SHIFT };

        glm::vec3 _matcap { Material::DEFAULT_MATCAP };
        float _shadingToony { Material::DEFAULT_SHADING_TOONY };

        glm::vec3 _parametricRim { Material::DEFAULT_PARAMETRIC_RIM };
        float _parametricRimFresnelPower { Material::DEFAULT_PARAMETRIC_RIM_FRESNEL_POWER };

        float _parametricRimLift { Material::DEFAULT_PARAMETRIC_RIM_LIFT };
        float _rimLightingMix { Material::DEFAULT_RIM_LIGHTING_MIX };
        glm::vec2 _uvAnimationScrollSpeed { Material::DEFAULT_UV_ANIMATION_SCROLL_SPEED };

        float _uvAnimationScrollRotationSpeed { Material::DEFAULT_UV_ANIMATION_SCROLL_SPEED };
        float _time { 0.0f };
        uint32_t _key { 0 }; // a copy of the materialKey
        float _spare { 0.0f };

        // Texture Coord Transform Array
        glm::mat4 _texcoordTransforms[Material::NUM_TEXCOORD_TRANSFORMS];

        // x: material mode (0 for UV, 1 for PROJECTED)
        // y: 1 for texture repeat, 0 for discard outside of 0 - 1
        glm::vec2 _materialParams { 0.0, 1.0 };

        uint32_t _texCoordSets { 0 };

        float spare { 0.0f };

        MToonSchema() {
            for (auto& transform : _texcoordTransforms) {
                transform = glm::mat4();
            }
        }

        void setTexCoordSet(int channel, int texCoordSet) {
            // We currently only support two texCoord sets (0 and 1), but eventually we want to support 4, so we use 2 bits
            const int MAX_TEX_COORD_SET = 1;
            _texCoordSets |= std::min(texCoordSet, MAX_TEX_COORD_SET) << (2 * channel);
        }
    };

    void setMaterialKey(const graphics::MaterialKey& materialKey) { _materialKey = materialKey; }
    graphics::MaterialKey getMaterialKey() const { return _materialKey; }
    gpu::BufferView& getSchemaBuffer() { return _schemaBuffer; }
    glm::vec4 getColor() const {
        glm::vec3 albedo;
        float opacity;
        if (_isMToon) {
            const auto& schema = _schemaBuffer.get<graphics::MultiMaterial::MToonSchema>();
            albedo = schema._albedo;
            opacity = schema._opacity;
        } else {
            const auto& schema = _schemaBuffer.get<graphics::MultiMaterial::Schema>();
            albedo = schema._albedo;
            opacity = schema._opacity;
        }
        return glm::vec4(ColorUtils::tosRGBVec3(albedo), opacity);
    }
    const std::array<gpu::TextureTablePointer, 3>& getTextureTables() const { return _textureTables; }

    void setCullFaceMode(graphics::MaterialKey::CullFaceMode cullFaceMode) { _cullFaceMode = cullFaceMode; }
    graphics::MaterialKey::CullFaceMode getCullFaceMode() const { return _cullFaceMode; }

    uint8_t getLayers() const { return _layers; }

    void setNeedsUpdate(bool needsUpdate) { _needsUpdate = needsUpdate; }
    void setTexturesLoading(bool value) { _texturesLoading = value; }
    void setInitialized() { _initialized = true; }

    bool shouldUpdate() const { return !_initialized || _needsUpdate || _texturesLoading || anyReferenceMaterialsOrTexturesChanged(); }

    int getTextureCount() const { calculateMaterialInfo(); return _textureCount; }
    size_t getTextureSize()  const { calculateMaterialInfo(); return _textureSize; }
    bool hasTextureInfo() const { return _hasCalculatedTextureInfo; }

    void resetReferenceTexturesAndMaterials();
    void addReferenceTexture(const std::function<gpu::TexturePointer()>& textureOperator);
    void addReferenceMaterial(const std::function<graphics::MaterialPointer()>& materialOperator);

    void setisMToonAndLayers(bool isMToon, uint8_t layers);
    bool isMToon() const { return _isMToon; }
    void setMToonTime();
    bool hasOutline() const { return _outlineWidthMode != 0 && _outlineWidth > 0.0f; }
    uint8_t getOutlineWidthMode() const { return _outlineWidthMode; }
    float getOutlineWidth() const { return _outlineWidth; }
    glm::vec3 getOutline() const { return _outline; }
    void resetOutline() { _outlineWidthMode = 0; _outlineWidth = 0.0f; _outline = glm::vec3(0.0f); }
    void setOutlineWidthMode(uint8_t mode) { _outlineWidthMode = mode; }
    void setOutlineWidth(float width) { _outlineWidth = width; }
    void setOutline(const glm::vec3& outline) { _outline = outline; }

    void setSplatMap(const gpu::TexturePointer& splatMap) { _splatMap = splatMap; }
    const gpu::TexturePointer& getSplatMap() const { return _splatMap; }
    bool isSplatMap() const { return (bool)_splatMap; }

    void addSamplerFunc(std::function<void(void)> samplerFunc) { _samplerFuncs.push_back(samplerFunc); }
    void resetSamplers() { _samplerFuncs.clear(); }
    void applySamplers() const;

private:
    graphics::MaterialKey _materialKey;
    gpu::BufferView _schemaBuffer;
    std::array<gpu::TextureTablePointer, 3> _textureTables;
    graphics::MaterialKey::CullFaceMode _cullFaceMode { graphics::Material::DEFAULT_CULL_FACE_MODE };
    gpu::TexturePointer _splatMap { nullptr };
    bool _needsUpdate { false };
    bool _texturesLoading { false };
    bool _initialized { false };
    uint8_t _layers { 1 };

    mutable size_t _textureSize { 0 };
    mutable int _textureCount { 0 };
    mutable bool _hasCalculatedTextureInfo { false };
    void calculateMaterialInfo() const;

    bool anyReferenceMaterialsOrTexturesChanged() const;

    std::vector<std::pair<std::function<gpu::TexturePointer()>, gpu::TexturePointer>> _referenceTextures;
    std::vector<std::pair<std::function<graphics::MaterialPointer()>, graphics::MaterialPointer>> _referenceMaterials;

    bool _isMToon { false };
    uint8_t _outlineWidthMode { 0 };
    float _outlineWidth { 0.0f };
    glm::vec3 _outline { graphics::Material::DEFAULT_OUTLINE };

    std::vector<std::function<void(void)>> _samplerFuncs;
};

};

#endif
