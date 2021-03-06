//
//  ZoneEntityItem.h
//  libraries/entities/src
//
//  Created by Brad Hefta-Gaub on 12/4/13.
//  Copyright 2013 High Fidelity, Inc.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//

#ifndef hifi_ZoneEntityItem_h
#define hifi_ZoneEntityItem_h

#include <EnvironmentData.h>

#include "KeyLightPropertyGroup.h"
#include "AtmospherePropertyGroup.h"
#include "EntityItem.h"
#include "EntityTree.h"
#include "SkyboxPropertyGroup.h"
#include "StagePropertyGroup.h"

class ZoneEntityItem : public EntityItem {
public:
    static EntityItemPointer factory(const EntityItemID& entityID, const EntityItemProperties& properties);

    ZoneEntityItem(const EntityItemID& entityItemID, const EntityItemProperties& properties);
    
    ALLOW_INSTANTIATION // This class can be instantiated
    
    // methods for getting/setting all properties of an entity
    virtual EntityItemProperties getProperties(EntityPropertyFlags desiredProperties = EntityPropertyFlags()) const;
    virtual bool setProperties(const EntityItemProperties& properties);

    // TODO: eventually only include properties changed since the params.lastViewFrustumSent time
    virtual EntityPropertyFlags getEntityProperties(EncodeBitstreamParams& params) const;

    virtual void appendSubclassData(OctreePacketData* packetData, EncodeBitstreamParams& params, 
                                    EntityTreeElementExtraEncodeData* modelTreeElementExtraEncodeData,
                                    EntityPropertyFlags& requestedProperties,
                                    EntityPropertyFlags& propertyFlags,
                                    EntityPropertyFlags& propertiesDidntFit,
                                    int& propertyCount, 
                                    OctreeElement::AppendState& appendState) const;

    virtual int readEntitySubclassDataFromBuffer(const unsigned char* data, int bytesLeftToRead, 
                                                ReadBitstreamToTreeParams& args,
                                                EntityPropertyFlags& propertyFlags, bool overwriteLocalData,
                                                bool& somethingChanged);



    static bool getZonesArePickable() { return _zonesArePickable; }
    static void setZonesArePickable(bool value) { _zonesArePickable = value; }

    static bool getDrawZoneBoundaries() { return _drawZoneBoundaries; }
    static void setDrawZoneBoundaries(bool value) { _drawZoneBoundaries = value; }
    
    virtual bool isReadyToComputeShape() { return false; }
    void updateShapeType(ShapeType type) { _shapeType = type; }
    virtual ShapeType getShapeType() const;
    
    virtual bool hasCompoundShapeURL() const { return !_compoundShapeURL.isEmpty(); }
    const QString getCompoundShapeURL() const { return _compoundShapeURL; }
    virtual void setCompoundShapeURL(const QString& url);

    const KeyLightPropertyGroup& getKeyLightProperties() const { return _keyLightProperties; }

    void setBackgroundMode(BackgroundMode value) { _backgroundMode = value; }
    BackgroundMode getBackgroundMode() const { return _backgroundMode; }

    EnvironmentData getEnvironmentData() const;
    const AtmospherePropertyGroup& getAtmosphereProperties() const { return _atmosphereProperties; }
    const SkyboxPropertyGroup& getSkyboxProperties() const { return _skyboxProperties; }
    const StagePropertyGroup& getStageProperties() const { return _stageProperties; }

    virtual bool supportsDetailedRayIntersection() const { return true; }
    virtual bool findDetailedRayIntersection(const glm::vec3& origin, const glm::vec3& direction,
                         bool& keepSearching, OctreeElementPointer& element, float& distance,
                         BoxFace& face, glm::vec3& surfaceNormal,
                         void** intersectedObject, bool precisionPicking) const;

    virtual void debugDump() const;

    static const ShapeType DEFAULT_SHAPE_TYPE;
    static const QString DEFAULT_COMPOUND_SHAPE_URL;
    
protected:
    KeyLightPropertyGroup _keyLightProperties;
    
    ShapeType _shapeType = DEFAULT_SHAPE_TYPE;
    QString _compoundShapeURL;
    
    BackgroundMode _backgroundMode = BACKGROUND_MODE_INHERIT;

    StagePropertyGroup _stageProperties;
    AtmospherePropertyGroup _atmosphereProperties;
    SkyboxPropertyGroup _skyboxProperties;

    static bool _drawZoneBoundaries;
    static bool _zonesArePickable;
};

#endif // hifi_ZoneEntityItem_h
