//
//  AbstractControllerScriptingInterface.h
//  libraries/script-engine/src
//
//  Created by Brad Hefta-Gaub on 12/17/13.
//  Copyright 2013 High Fidelity, Inc.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//

#pragma once
#ifndef hifi_AbstractControllerScriptingInterface_h
#define hifi_AbstractControllerScriptingInterface_h

#include <unordered_map>
#include <unordered_set>
#include <map>
#include <set>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <QThread>
#include <QtCore/QObject>
#include <QtCore/QVariant>

#include <QtQml/QJSValue>
#include <QtScript/QScriptValue>

#include <DependencyManager.h>

#include "UserInputMapper.h"
#include "StandardControls.h"

namespace controller {
    class InputController : public QObject {
        Q_OBJECT

    public:
        using Key = unsigned int;
        using Pointer = std::shared_ptr<InputController>;

        virtual void update() = 0;
        virtual Key getKey() const = 0;

    public slots:
        virtual bool isActive() const = 0;
        virtual glm::vec3 getAbsTranslation() const = 0;
        virtual glm::quat getAbsRotation() const = 0;
        virtual glm::vec3 getLocTranslation() const = 0;
        virtual glm::quat getLocRotation() const = 0;

    signals:
        //void spatialEvent(const SpatialEvent& event);
    };

    /// handles scripting of input controller commands from JS
    class ScriptingInterface : public QObject, public Dependency {
        Q_OBJECT
        Q_PROPERTY(QVariantMap Hardware READ getHardware CONSTANT FINAL)
        Q_PROPERTY(QVariantMap Actions READ getActions CONSTANT FINAL)
        Q_PROPERTY(QVariantMap Standard READ getStandard CONSTANT FINAL)

    public:
        ScriptingInterface();
        virtual ~ScriptingInterface() {};

        Q_INVOKABLE QVector<Action> getAllActions();
        Q_INVOKABLE QVector<Input::NamedPair> getAvailableInputs(unsigned int device);
        Q_INVOKABLE QString getDeviceName(unsigned int device);
        Q_INVOKABLE float getActionValue(int action);
        Q_INVOKABLE int findDevice(QString name);
        Q_INVOKABLE QVector<QString> getDeviceNames();
        Q_INVOKABLE int findAction(QString actionName);
        Q_INVOKABLE QVector<QString> getActionNames() const;

        Q_INVOKABLE float getValue(const int& source) const;
        Q_INVOKABLE float getButtonValue(StandardButtonChannel source, uint16_t device = 0) const;
        Q_INVOKABLE float getAxisValue(StandardAxisChannel source, uint16_t device = 0) const;
        Q_INVOKABLE Pose getPoseValue(const int& source) const;
        Q_INVOKABLE Pose getPoseValue(StandardPoseChannel source, uint16_t device = 0) const;

        Q_INVOKABLE QObject* newMapping(const QString& mappingName = QUuid::createUuid().toString());
        Q_INVOKABLE void enableMapping(const QString& mappingName, bool enable = true);
        Q_INVOKABLE void disableMapping(const QString& mappingName) { enableMapping(mappingName, false); }
        Q_INVOKABLE QObject* parseMapping(const QString& json);
        Q_INVOKABLE QObject* loadMapping(const QString& jsonUrl);


        //Q_INVOKABLE bool isPrimaryButtonPressed() const;
        //Q_INVOKABLE glm::vec2 getPrimaryJoystickPosition() const;

        //Q_INVOKABLE int getNumberOfButtons() const;
        //Q_INVOKABLE bool isButtonPressed(int buttonIndex) const;

        //Q_INVOKABLE int getNumberOfTriggers() const;
        //Q_INVOKABLE float getTriggerValue(int triggerIndex) const;

        //Q_INVOKABLE int getNumberOfJoysticks() const;
        //Q_INVOKABLE glm::vec2 getJoystickPosition(int joystickIndex) const;

        //Q_INVOKABLE int getNumberOfSpatialControls() const;
        //Q_INVOKABLE glm::vec3 getSpatialControlPosition(int controlIndex) const;
        //Q_INVOKABLE glm::vec3 getSpatialControlVelocity(int controlIndex) const;
        //Q_INVOKABLE glm::vec3 getSpatialControlNormal(int controlIndex) const;
        //Q_INVOKABLE glm::quat getSpatialControlRawRotation(int controlIndex) const;

        Q_INVOKABLE const QVariantMap& getHardware() { return _hardware; }
        Q_INVOKABLE const QVariantMap& getActions() { return _actions; }
        Q_INVOKABLE const QVariantMap& getStandard() { return _standard; }

        bool isMouseCaptured() const { return _mouseCaptured; }
        bool isTouchCaptured() const { return _touchCaptured; }
        bool isWheelCaptured() const { return _wheelCaptured; }
        bool areActionsCaptured() const { return _actionsCaptured; }

    public slots:

        virtual void captureMouseEvents() { _mouseCaptured = true; }
        virtual void releaseMouseEvents() { _mouseCaptured = false; }

        virtual void captureTouchEvents() { _touchCaptured = true; }
        virtual void releaseTouchEvents() { _touchCaptured = false; }

        virtual void captureWheelEvents() { _wheelCaptured = true; }
        virtual void releaseWheelEvents() { _wheelCaptured = false; }

        virtual void captureActionEvents() { _actionsCaptured = true; }
        virtual void releaseActionEvents() { _actionsCaptured = false; }

    signals:
        void actionEvent(int action, float state);
        void inputEvent(int action, float state);
        void hardwareChanged();

    private:
        // Update the exposed variant maps reporting active hardware
        void updateMaps();

        QVariantMap _hardware;
        QVariantMap _actions;
        QVariantMap _standard;

        bool _mouseCaptured{ false };
        bool _touchCaptured{ false };
        bool _wheelCaptured{ false };
        bool _actionsCaptured{ false };
    };


}


#endif // hifi_AbstractControllerScriptingInterface_h
