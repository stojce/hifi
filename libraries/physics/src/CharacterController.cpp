/*
Bullet Continuous Collision Detection and Physics Library
Copyright (c) 2003-2008 Erwin Coumans  http://bulletphysics.com
2015.03.25 -- modified by Andrew Meadows andrew@highfidelity.io

This software is provided 'as-is', without any express or implied warranty.
In no event will the authors be held liable for any damages arising from the use of this software.
Permission is granted to anyone to use this software for any purpose,
including commercial applications, and to alter it and redistribute it freely,
subject to the following restrictions:

1. The origin of this software must not be misrepresented; you must not claim that you wrote the original software.
   If you use this software in a product, an acknowledgment in the product documentation would be appreciated
   but is not required.
2. Altered source versions must be plainly marked as such, and must not be misrepresented as being the original software.
3. This notice may not be removed or altered from any source distribution.
*/


#include "BulletCollision/CollisionDispatch/btGhostObject.h"

#include "BulletUtil.h"
#include "CharacterController.h"

const uint32_t PENDING_FLAG_ADD_TO_SIMULATION = 1U << 0;
const uint32_t PENDING_FLAG_REMOVE_FROM_SIMULATION = 1U << 1;
const uint32_t PENDING_FLAG_UPDATE_SHAPE = 1U << 2;
const uint32_t PENDING_FLAG_JUMP = 1U << 3;

// static helper method
static btVector3 getNormalizedVector(const btVector3& v) {
    // NOTE: check the length first, then normalize
    // --> avoids assert when trying to normalize zero-length vectors
    btScalar vLength = v.length();
    if (vLength < FLT_EPSILON) {
        return btVector3(0.0f, 0.0f, 0.0f);
    }
    btVector3 n = v;
    n /= vLength;
    return n;
}

class btKinematicClosestNotMeRayResultCallback : public btCollisionWorld::ClosestRayResultCallback {
public:
    btKinematicClosestNotMeRayResultCallback (btCollisionObject* me) : 
        btCollisionWorld::ClosestRayResultCallback(btVector3(0.0f, 0.0f, 0.0f), btVector3(0.0f, 0.0f, 0.0f)) {
        _me = me;
    }

    virtual btScalar addSingleResult(btCollisionWorld::LocalRayResult& rayResult,bool normalInWorldSpace) {
        if (rayResult.m_collisionObject == _me) {
            return 1.0f;
        }
        return ClosestRayResultCallback::addSingleResult (rayResult, normalInWorldSpace);
    }
protected:
    btCollisionObject* _me;
};


class btKinematicClosestNotMeConvexResultCallback : public btCollisionWorld::ClosestConvexResultCallback {
    public:
        btKinematicClosestNotMeConvexResultCallback(btCollisionObject* me, const btVector3& up, btScalar minSlopeDot)
            : btCollisionWorld::ClosestConvexResultCallback(btVector3(0.0, 0.0, 0.0), btVector3(0.0, 0.0, 0.0))
              , _me(me)
              , _up(up)
              , _minSlopeDot(minSlopeDot)
    {
    }

    virtual btScalar addSingleResult(btCollisionWorld::LocalConvexResult& convexResult, bool normalInWorldSpace) {
        if (convexResult.m_hitCollisionObject == _me) {
            return btScalar(1.0);
        }

        if (!convexResult.m_hitCollisionObject->hasContactResponse()) {
            return btScalar(1.0);
        }

        btVector3 hitNormalWorld;
        if (normalInWorldSpace) {
            hitNormalWorld = convexResult.m_hitNormalLocal;
        } else {
            ///need to transform normal into worldspace
            hitNormalWorld = convexResult.m_hitCollisionObject->getWorldTransform().getBasis()*convexResult.m_hitNormalLocal;
        }

        // Note: hitNormalWorld points into character, away from object
        // and _up points opposite to movement

        btScalar dotUp = _up.dot(hitNormalWorld);
        if (dotUp < _minSlopeDot) {
            return btScalar(1.0);
        }

        return ClosestConvexResultCallback::addSingleResult(convexResult, normalInWorldSpace);
    }

protected:
    btCollisionObject* _me;
    const btVector3 _up;
    btScalar _minSlopeDot;
};

class StepDownConvexResultCallback : public btCollisionWorld::ClosestConvexResultCallback {
    // special convex sweep callback for character during the stepDown() phase
    public:
        StepDownConvexResultCallback(btCollisionObject* me,
                const btVector3& up,
                const btVector3& start,
                const btVector3& step,
                const btVector3& pushDirection,
                btScalar minSlopeDot,
                btScalar radius,
                btScalar halfHeight)
            : btCollisionWorld::ClosestConvexResultCallback(btVector3(0.0, 0.0, 0.0), btVector3(0.0, 0.0, 0.0))
              , _me(me)
              , _up(up)
              , _start(start)
              , _step(step)
              , _pushDirection(pushDirection)
              , _minSlopeDot(minSlopeDot)
              , _radius(radius)
              , _halfHeight(halfHeight)
    {
    }

    virtual btScalar addSingleResult(btCollisionWorld::LocalConvexResult& convexResult, bool normalInWorldSpace) {
        if (convexResult.m_hitCollisionObject == _me) {
            return btScalar(1.0);
        }

        if (!convexResult.m_hitCollisionObject->hasContactResponse()) {
            return btScalar(1.0);
        }

        btVector3 hitNormalWorld;
        if (normalInWorldSpace) {
            hitNormalWorld = convexResult.m_hitNormalLocal;
        } else {
            ///need to transform normal into worldspace
            hitNormalWorld = convexResult.m_hitCollisionObject->getWorldTransform().getBasis() * convexResult.m_hitNormalLocal;
        }

        // Note: hitNormalWorld points into character, away from object
        // and _up points opposite to movement

        btScalar dotUp = _up.dot(hitNormalWorld);
        if (dotUp < _minSlopeDot) {
            if (hitNormalWorld.dot(_pushDirection) > 0.0f) {
                // ignore hits that push in same direction as character is moving
                // which helps character NOT snag when stepping off ledges
                return btScalar(1.0f);
            }

            // compute the angle between "down" and the line from character center to "hit" point
            btVector3 fractionalStep = convexResult.m_hitFraction * _step;
            btVector3 localHit = convexResult.m_hitPointLocal - _start + fractionalStep;
            btScalar angle = localHit.angle(-_up);

            // compute a maxAngle based on size of _step
            btVector3 side(_radius, - (_halfHeight - _step.length() + fractionalStep.dot(_up)), 0.0f);
            btScalar maxAngle = side.angle(-_up);

            // Ignore hits that are larger than maxAngle. Effectively what is happening here is:
            // we're ignoring hits at contacts that have non-vertical normals... if they hit higher
            // than the character's "feet".  Ignoring the contact allows the character to slide down
            // for these hits.  In other words, vertical walls against the character's torso will
            // not prevent them from "stepping down" to find the floor.
            if (angle > maxAngle) {
                return btScalar(1.0f);
            }
        }

        btScalar fraction = ClosestConvexResultCallback::addSingleResult(convexResult, normalInWorldSpace);
        return fraction;
    }

protected:
    btCollisionObject* _me;
    const btVector3 _up;
    btVector3 _start;
    btVector3 _step;
    btVector3 _pushDirection;
    btScalar _minSlopeDot;
    btScalar _radius;
    btScalar _halfHeight;
};

/*
 * Returns the reflection direction of a ray going 'direction' hitting a surface with normal 'normal'
 *
 * from: http://www-cs-students.stanford.edu/~adityagp/final/node3.html
 */
btVector3 CharacterController::computeReflectionDirection(const btVector3& direction, const btVector3& normal) {
    return direction - (btScalar(2.0) * direction.dot(normal)) * normal;
}

/*
 * Returns the portion of 'direction' that is parallel to 'normal'
 */
btVector3 CharacterController::parallelComponent(const btVector3& direction, const btVector3& normal) {
    btScalar magnitude = direction.dot(normal);
    return normal * magnitude;
}

/*
 * Returns the portion of 'direction' that is perpindicular to 'normal'
 */
btVector3 CharacterController::perpindicularComponent(const btVector3& direction, const btVector3& normal) {
    return direction - parallelComponent(direction, normal);
}

const btVector3 LOCAL_UP_AXIS(0.0f, 1.0f, 0.0f);
const float DEFAULT_GRAVITY = 5.0f;
const float TERMINAL_VELOCITY = 55.0f;
const float JUMP_SPEED = 5.0f;

CharacterController::CharacterController(AvatarData* avatarData) {
    assert(avatarData);
    _avatarData = avatarData;

    _enabled = false;
    _ghostObject = NULL;
    _convexShape = NULL;

    _addedMargin = 0.02f;
    _walkDirection.setValue(0.0f,0.0f,0.0f);
    _velocityTimeInterval = 0.0f;
    _verticalVelocity = 0.0f;
    _verticalOffset = 0.0f;
    _gravity = DEFAULT_GRAVITY; // slower than Earth's
    _maxFallSpeed = TERMINAL_VELOCITY; // Terminal velocity of a sky diver in m/s.
    _jumpSpeed = JUMP_SPEED;
    _isOnGround = false;
    _isJumping = false;
    _isHovering = true;
    _jumpToHoverStart = 0;
    setMaxSlope(btRadians(45.0f));
    _lastStepUp = 0.0f;

    _pendingFlags = PENDING_FLAG_UPDATE_SHAPE;
    updateShapeIfNecessary();
}

CharacterController::~CharacterController() {
    delete _ghostObject;
    _ghostObject = NULL;
    delete _convexShape;
    _convexShape = NULL;
    // make sure you remove this Character from its DynamicsWorld before reaching this spot
    assert(_dynamicsWorld == NULL);
}

btPairCachingGhostObject* CharacterController::getGhostObject() {
    return _ghostObject;
}

bool CharacterController::recoverFromPenetration(btCollisionWorld* collisionWorld) {
    // Here we must refresh the overlapping paircache as the penetrating movement itself or the
    // previous recovery iteration might have used setWorldTransform and pushed us into an object
    // that is not in the previous cache contents from the last timestep, as will happen if we
    // are pushed into a new AABB overlap. Unhandled this means the next convex sweep gets stuck.
    //
    // Do this by calling the broadphase's setAabb with the moved AABB, this will update the broadphase
    // paircache and the ghostobject's internal paircache at the same time.    /BW

    btVector3 minAabb, maxAabb;
    _convexShape->getAabb(_ghostObject->getWorldTransform(), minAabb, maxAabb);
    collisionWorld->getBroadphase()->setAabb(_ghostObject->getBroadphaseHandle(),
            minAabb,
            maxAabb,
            collisionWorld->getDispatcher());

    bool penetration = false;

    collisionWorld->getDispatcher()->dispatchAllCollisionPairs(_ghostObject->getOverlappingPairCache(), collisionWorld->getDispatchInfo(), collisionWorld->getDispatcher());

    _currentPosition = _ghostObject->getWorldTransform().getOrigin();

    btVector3 currentPosition = _currentPosition;

    btScalar maxPen = btScalar(0.0);
    for (int i = 0; i < _ghostObject->getOverlappingPairCache()->getNumOverlappingPairs(); i++) {
        _manifoldArray.resize(0);

        btBroadphasePair* collisionPair = &_ghostObject->getOverlappingPairCache()->getOverlappingPairArray()[i];

        btCollisionObject* obj0 = static_cast<btCollisionObject*>(collisionPair->m_pProxy0->m_clientObject);
        btCollisionObject* obj1 = static_cast<btCollisionObject*>(collisionPair->m_pProxy1->m_clientObject);

        if ((obj0 && !obj0->hasContactResponse()) || (obj1 && !obj1->hasContactResponse())) {
            continue;
        }

        if (collisionPair->m_algorithm) {
            collisionPair->m_algorithm->getAllContactManifolds(_manifoldArray);
        }

        for (int j = 0;j < _manifoldArray.size(); j++) {
            btPersistentManifold* manifold = _manifoldArray[j];
            btScalar directionSign = (manifold->getBody0() == _ghostObject) ? btScalar(1.0) : btScalar(-1.0);
            for (int p = 0;p < manifold->getNumContacts(); p++) {
                const btManifoldPoint&pt = manifold->getContactPoint(p);

                btScalar dist = pt.getDistance();

                if (dist < 0.0) {
                    bool useContact = true;
                    btVector3 normal = pt.m_normalWorldOnB;
                    normal *= directionSign; // always points from object to character

                    btScalar normalDotUp = normal.dot(_currentUp);
                    if (normalDotUp < _maxSlopeCosine) {
                        // this contact has a non-vertical normal... might need to ignored
                        btVector3 collisionPoint;
                        if (directionSign > 0.0) {
                            collisionPoint = pt.getPositionWorldOnB();
                        } else {
                            collisionPoint = pt.getPositionWorldOnA();
                        }

                        // we do math in frame where character base is origin
                        btVector3 characterBase = currentPosition - (_radius + _halfHeight) * _currentUp;
                        collisionPoint -= characterBase;
                        btScalar collisionHeight = collisionPoint.dot(_currentUp);

                        if (collisionHeight < _lastStepUp) {
                            // This contact is below the lastStepUp, so we ignore it for penetration resolution,
                            // otherwise it may prevent the character from getting close enough to find any available
                            // horizontal foothold that would allow it to climbe the ledge.  In other words, we're
                            // making the character's "feet" soft for collisions against steps, but not floors.
                            useContact = false;
                        }
                    }
                    if (useContact) {

                        if (dist < maxPen) {
                            maxPen = dist;
                            _floorNormal = normal;
                        }
                        const btScalar INCREMENTAL_RESOLUTION_FACTOR = 0.2f;
                        _currentPosition += normal * (fabsf(dist) * INCREMENTAL_RESOLUTION_FACTOR);
                        penetration = true;
                    }
                }
            }
        }
    }
    btTransform newTrans = _ghostObject->getWorldTransform();
    newTrans.setOrigin(_currentPosition);
    _ghostObject->setWorldTransform(newTrans);
    return penetration;
}


void CharacterController::scanDown(btCollisionWorld* world) {
    // we test with downward raycast and if we don't find floor close enough then turn on "hover"
    btKinematicClosestNotMeRayResultCallback callback(_ghostObject);
    callback.m_collisionFilterGroup = getGhostObject()->getBroadphaseHandle()->m_collisionFilterGroup;
    callback.m_collisionFilterMask = getGhostObject()->getBroadphaseHandle()->m_collisionFilterMask;

    btVector3 start = _currentPosition;
    const btScalar MAX_SCAN_HEIGHT = 20.0f + _halfHeight + _radius; // closest possible floor for disabling hover
    const btScalar MIN_HOVER_HEIGHT = 3.0f + _halfHeight + _radius; // distance to floor for enabling hover
    btVector3 end = start - MAX_SCAN_HEIGHT * _currentUp;

    world->rayTest(start, end, callback);
    if (!callback.hasHit()) {
        _isHovering = true;
    } else if (_isHovering && callback.m_closestHitFraction * MAX_SCAN_HEIGHT < MIN_HOVER_HEIGHT) {
        _isHovering = false;
    }
}

void CharacterController::stepUp(btCollisionWorld* world) {
    // phase 1: up

    // compute start and end
    btTransform start, end;
    start.setIdentity();
    start.setOrigin(_currentPosition + _currentUp * (_convexShape->getMargin() + _addedMargin));

    _targetPosition = _currentPosition + _currentUp * _stepUpHeight;
    end.setIdentity();
    end.setOrigin(_targetPosition);

    // sweep up
    btVector3 sweepDirNegative = - _currentUp;
    btKinematicClosestNotMeConvexResultCallback callback(_ghostObject, sweepDirNegative, btScalar(0.7071));
    callback.m_collisionFilterGroup = getGhostObject()->getBroadphaseHandle()->m_collisionFilterGroup;
    callback.m_collisionFilterMask = getGhostObject()->getBroadphaseHandle()->m_collisionFilterMask;
    _ghostObject->convexSweepTest(_convexShape, start, end, callback, world->getDispatchInfo().m_allowedCcdPenetration);

    if (callback.hasHit()) {
        // we hit something, so zero our vertical velocity
        _verticalVelocity = 0.0f;
        _verticalOffset = 0.0f;

        // Only modify the position if the hit was a slope and not a wall or ceiling.
        if (callback.m_hitNormalWorld.dot(_currentUp) > 0.0f) {
            _lastStepUp = _stepUpHeight * callback.m_closestHitFraction;
            _currentPosition.setInterpolate3(_currentPosition, _targetPosition, callback.m_closestHitFraction);
        } else {
            _lastStepUp = _stepUpHeight;
            _currentPosition = _targetPosition;
        }
    } else {
        _currentPosition = _targetPosition;
        _lastStepUp = _stepUpHeight;
    }
}

void CharacterController::updateTargetPositionBasedOnCollision(const btVector3& hitNormal, btScalar tangentMag, btScalar normalMag) {
    btVector3 movementDirection = _targetPosition - _currentPosition;
    btScalar movementLength = movementDirection.length();
    if (movementLength > SIMD_EPSILON) {
        movementDirection.normalize();

        btVector3 reflectDir = computeReflectionDirection(movementDirection, hitNormal);
        reflectDir.normalize();

        btVector3 parallelDir, perpindicularDir;

        parallelDir = parallelComponent(reflectDir, hitNormal);
        perpindicularDir = perpindicularComponent(reflectDir, hitNormal);

        _targetPosition = _currentPosition;
        //if (tangentMag != 0.0) {
        if (0) {
            btVector3 parComponent = parallelDir * btScalar(tangentMag * movementLength);
            _targetPosition += parComponent;
        }

        if (normalMag != 0.0) {
            btVector3 perpComponent = perpindicularDir * btScalar(normalMag * movementLength);
            _targetPosition += perpComponent;
        }
    }
}

void CharacterController::stepForward(btCollisionWorld* collisionWorld, const btVector3& movement) {
    // phase 2: forward
    _targetPosition = _currentPosition + movement;

    btTransform start, end;
    start.setIdentity();
    end.setIdentity();

    /* TODO: experiment with this to see if we can use this to help direct motion when a floor is available
    if (_touchingContact) {
        if (_normalizedDirection.dot(_floorNormal) < btScalar(0.0)) {
            updateTargetPositionBasedOnCollision(_floorNormal, 1.0f, 1.0f);
        }
    }*/

    // modify shape's margin for the sweeps
    btScalar margin = _convexShape->getMargin();
    _convexShape->setMargin(margin + _addedMargin);

    const btScalar MIN_STEP_DISTANCE_SQUARED = 1.0e-6f;
    btVector3 step = _targetPosition - _currentPosition;
    btScalar stepLength2 = step.length2();
    int maxIter = 10;

    while (stepLength2 > MIN_STEP_DISTANCE_SQUARED && maxIter-- > 0) {
        start.setOrigin(_currentPosition);
        end.setOrigin(_targetPosition);

        // sweep forward
        btVector3 sweepDirNegative(_currentPosition - _targetPosition);
        btKinematicClosestNotMeConvexResultCallback callback(_ghostObject, sweepDirNegative, btScalar(0.0));
        callback.m_collisionFilterGroup = _ghostObject->getBroadphaseHandle()->m_collisionFilterGroup;
        callback.m_collisionFilterMask = _ghostObject->getBroadphaseHandle()->m_collisionFilterMask;
        _ghostObject->convexSweepTest(_convexShape, start, end, callback, collisionWorld->getDispatchInfo().m_allowedCcdPenetration);

        if (callback.hasHit()) {
            // we hit soemthing!
            // Compute new target position by removing portion cut-off by collision, which will produce a new target
            // that is the closest approach of the the obstacle plane to the original target.
            step = _targetPosition - _currentPosition;
            btScalar stepDotNormal = step.dot(callback.m_hitNormalWorld); // we expect this dot to be negative
            step += (stepDotNormal * (1.0f - callback.m_closestHitFraction)) * callback.m_hitNormalWorld;
            _targetPosition = _currentPosition + step;

            stepLength2 = step.length2();
        } else {
            // we swept to the end without hitting anything
            _currentPosition = _targetPosition;
            break;
        }
    }

    // restore shape's margin
    _convexShape->setMargin(margin);
}

void CharacterController::stepDown(btCollisionWorld* collisionWorld, btScalar dt) {
    // phase 3: down
    //
    // The "stepDown" phase first makes a normal sweep down that cancels the lift from the "stepUp" phase.
    // If it hits a ledge then it stops otherwise it makes another sweep down in search of a floor within
    // reach of the character's feet.

    // first sweep for ledge
    btVector3 step = (_verticalVelocity * dt - _lastStepUp) * _currentUp;

    StepDownConvexResultCallback callback(_ghostObject,
            _currentUp,
            _currentPosition, step,
            _walkDirection,
            _maxSlopeCosine,
            _radius, _halfHeight);
    callback.m_collisionFilterGroup = _ghostObject->getBroadphaseHandle()->m_collisionFilterGroup;
    callback.m_collisionFilterMask = _ghostObject->getBroadphaseHandle()->m_collisionFilterMask;

    btTransform start, end;
    start.setIdentity();
    end.setIdentity();

    start.setOrigin(_currentPosition);
    _targetPosition = _currentPosition + step;
    end.setOrigin(_targetPosition);
    _ghostObject->convexSweepTest(_convexShape, start, end, callback, collisionWorld->getDispatchInfo().m_allowedCcdPenetration);

    _isOnGround = false;
    if (callback.hasHit()) {
        _currentPosition += callback.m_closestHitFraction * step;
        _verticalVelocity = 0.0f;
        _verticalOffset = 0.0f;
        _isJumping = false;
        _isOnGround = true;
    } else if (!_isJumping) {
        // sweep again for floor within downStep threshold
        step = -_stepDownHeight * _currentUp;
        StepDownConvexResultCallback callback2 (_ghostObject,
                _currentUp,
                _currentPosition, step,
                _walkDirection,
                _maxSlopeCosine,
                _radius, _halfHeight);

        callback2.m_collisionFilterGroup = _ghostObject->getBroadphaseHandle()->m_collisionFilterGroup;
        callback2.m_collisionFilterMask = _ghostObject->getBroadphaseHandle()->m_collisionFilterMask;

        _currentPosition = _targetPosition;
        _targetPosition = _currentPosition + step;

        start.setOrigin(_currentPosition);
        end.setOrigin(_targetPosition);
        _ghostObject->convexSweepTest(_convexShape, start, end, callback2, collisionWorld->getDispatchInfo().m_allowedCcdPenetration);

        if (callback2.hasHit()) {
            _currentPosition += callback2.m_closestHitFraction * step;
            _verticalVelocity = 0.0f;
            _verticalOffset = 0.0f;
            _isJumping = false;
            _isOnGround = true;
        } else {
            // nothing to step down on
            _lastStepUp = 0.0f;
        }
    } else {
        // we're jumping, and didn't hit anything, so our target position is where we would have fallen to
        _currentPosition = _targetPosition;
    }
}

void CharacterController::setWalkDirection(const btVector3& walkDirection) {
    // This must be implemented to satisfy base-class interface but does nothing.
    // Use setVelocityForTimeInterval() instead.
    assert(false);
}

void CharacterController::setVelocityForTimeInterval(const btVector3& velocity, btScalar timeInterval) {
    _walkDirection = velocity;
    _normalizedDirection = getNormalizedVector(_walkDirection);
    _velocityTimeInterval += timeInterval;
}

void CharacterController::reset(btCollisionWorld* collisionWorld) {
    _verticalVelocity = 0.0;
    _verticalOffset = 0.0;
    _isOnGround = false;
    _isJumping = false;
    _walkDirection.setValue(0,0,0);
    _velocityTimeInterval = 0.0;

    //clear pair cache
    btHashedOverlappingPairCache *cache = _ghostObject->getOverlappingPairCache();
    while (cache->getOverlappingPairArray().size() > 0) {
        cache->removeOverlappingPair(cache->getOverlappingPairArray()[0].m_pProxy0,
                cache->getOverlappingPairArray()[0].m_pProxy1,
                collisionWorld->getDispatcher());
    }
}

void CharacterController::warp(const btVector3& origin) {
    btTransform xform;
    xform.setIdentity();
    xform.setOrigin(origin);
    _ghostObject->setWorldTransform(xform);
}


void CharacterController::preStep(btCollisionWorld* collisionWorld) {
    if (!_enabled) {
        return;
    }
    int numPenetrationLoops = 0;
    _touchingContact = false;
    while (recoverFromPenetration(collisionWorld)) {
        numPenetrationLoops++;
        _touchingContact = true;
        if (numPenetrationLoops > 4) {
            break;
        }
    }

    // the CharacterController algorithm can only change the position, 
    // so we don't bother to pull the rotation out of the transform
    const btTransform& transform = _ghostObject->getWorldTransform();
    _currentPosition = transform.getOrigin();
}

void CharacterController::playerStep(btCollisionWorld* collisionWorld, btScalar dt) {
    if (!_enabled) {
        return; // no motion
    }

    // Update fall velocity.
    if (_isHovering) {
        const btScalar HOVER_RELAXATION_TIMESCALE = 1.0f;
        _verticalVelocity *= (1.0f - dt / HOVER_RELAXATION_TIMESCALE);
    } else {
        _verticalVelocity -= _gravity * dt;
        if (_verticalVelocity > _jumpSpeed) {
            _verticalVelocity = _jumpSpeed;
        } else if (_verticalVelocity < -_maxFallSpeed) {
            _verticalVelocity = -_maxFallSpeed;
        }
    }
    _verticalOffset = _verticalVelocity * dt;

    btTransform xform;
    xform = _ghostObject->getWorldTransform();

    // the algorithm is as follows:
    // (1) step the character up a little bit so that its forward step doesn't hit the floor
    // (2) step the character forward
    // (3) step the character down looking for new ledges, the original floor, or a floor one step below where we started

    scanDown(collisionWorld);

    stepUp(collisionWorld);

    // compute substep and decrement total interval
    btScalar dtMoving = (dt < _velocityTimeInterval) ? dt : _velocityTimeInterval;
    _velocityTimeInterval -= dt;
    _stepDt += dt;

    // stepForward substep
    btVector3 move = _walkDirection * dtMoving;
    stepForward(collisionWorld, move);

    stepDown(collisionWorld, dt);

    xform.setOrigin(_currentPosition);
    _ghostObject->setWorldTransform(xform);
}

void CharacterController::setMaxFallSpeed(btScalar speed) {
    _maxFallSpeed = speed;
}

void CharacterController::setJumpSpeed(btScalar jumpSpeed) {
    _jumpSpeed = jumpSpeed;
}

void CharacterController::setMaxJumpHeight(btScalar maxJumpHeight) {
    _maxJumpHeight = maxJumpHeight;
}

bool CharacterController::canJump() const {
    return _isOnGround;
}

void CharacterController::jump() {
    _pendingFlags |= PENDING_FLAG_JUMP;

    // check for case where user is holding down "jump" key...
    // we'll eventually tansition to "hover"
    if (!_isHovering) {
        if (!_isJumping) {
            _jumpToHoverStart = usecTimestampNow();
        } else {
            quint64 now = usecTimestampNow();
            const quint64 JUMP_TO_HOVER_PERIOD = USECS_PER_SECOND;
            if (now - _jumpToHoverStart < JUMP_TO_HOVER_PERIOD) {
                _isHovering = true;
            }
        }
    }
}

void CharacterController::setGravity(btScalar gravity) {
    _gravity = gravity;
}

btScalar CharacterController::getGravity() const {
    return _gravity;
}

void CharacterController::setMaxSlope(btScalar slopeRadians) {
    _maxSlopeRadians = slopeRadians;
    _maxSlopeCosine = btCos(slopeRadians);
}

btScalar CharacterController::getMaxSlope() const {
    return _maxSlopeRadians;
}

bool CharacterController::onGround() const {
    return _isOnGround;
}

void CharacterController::debugDraw(btIDebugDraw* debugDrawer) {
}

void CharacterController::setUpInterpolate(bool value) {
    // This method is required by btCharacterControllerInterface, but it does nothing.
    // What it used to do was determine whether stepUp() would: stop where it hit the ceiling
    // (interpolate = true, and now default behavior) or happily penetrate objects above the avatar.
}

void CharacterController::setLocalBoundingBox(const glm::vec3& corner, const glm::vec3& scale) {
    _boxScale = scale;

    float x = _boxScale.x;
    float z = _boxScale.z;
    float radius = 0.5f * sqrtf(0.5f * (x * x + z * z));
    float halfHeight = 0.5f * _boxScale.y - radius;
    float MIN_HALF_HEIGHT = 0.1f;
    if (halfHeight < MIN_HALF_HEIGHT) {
        halfHeight = MIN_HALF_HEIGHT;
    }

    // compare dimensions
    float radiusDelta = glm::abs(radius - _radius);
    float heightDelta = glm::abs(halfHeight - _halfHeight);
    if (radiusDelta < FLT_EPSILON && heightDelta < FLT_EPSILON) {
        // shape hasn't changed --> nothing to do
    } else {
        if (_dynamicsWorld) {
            // must REMOVE from world prior to shape update
            _pendingFlags |= PENDING_FLAG_REMOVE_FROM_SIMULATION;
        }
        _pendingFlags |= PENDING_FLAG_UPDATE_SHAPE;
        // only need to ADD back when we happen to be enabled
        if (_enabled) {
            _pendingFlags |= PENDING_FLAG_ADD_TO_SIMULATION;
        }
    }

    // it's ok to change offset immediately -- there are no thread safety issues here
    _shapeLocalOffset = corner + 0.5f * _boxScale;
}

bool CharacterController::needsAddition() const {
    return (bool)(_pendingFlags & PENDING_FLAG_ADD_TO_SIMULATION);
}

bool CharacterController::needsRemoval() const {
    return (bool)(_pendingFlags & PENDING_FLAG_REMOVE_FROM_SIMULATION);
}

void CharacterController::setEnabled(bool enabled) {
    if (enabled != _enabled) {
        if (enabled) {
            // Don't bother clearing REMOVE bit since it might be paired with an UPDATE_SHAPE bit.
            // Setting the ADD bit here works for all cases so we don't even bother checking other bits.
            _pendingFlags |= PENDING_FLAG_ADD_TO_SIMULATION;
            _isHovering = true;
        } else {
            if (_dynamicsWorld) {
                _pendingFlags |= PENDING_FLAG_REMOVE_FROM_SIMULATION;
            }
            _pendingFlags &= ~ PENDING_FLAG_ADD_TO_SIMULATION;
            _isOnGround = false;
        }
        _enabled = enabled;
    }
}

void CharacterController::setDynamicsWorld(btDynamicsWorld* world) {
    if (_dynamicsWorld != world) {
        if (_dynamicsWorld) { 
            if (_ghostObject) {
                _dynamicsWorld->removeCollisionObject(_ghostObject);
                _dynamicsWorld->removeAction(this);
            }
            _dynamicsWorld = NULL;
        }
        if (world && _ghostObject) {
            _dynamicsWorld = world;
            _pendingFlags &= ~ PENDING_FLAG_JUMP;
            _dynamicsWorld->addCollisionObject(_ghostObject,
                    btBroadphaseProxy::CharacterFilter,
                    btBroadphaseProxy::StaticFilter | btBroadphaseProxy::DefaultFilter);
            _dynamicsWorld->addAction(this);
            reset(_dynamicsWorld);
        }
    }
    if (_dynamicsWorld) {
        if (_pendingFlags & PENDING_FLAG_UPDATE_SHAPE) {
            // shouldn't fall in here, but if we do make sure both ADD and REMOVE bits are still set
            _pendingFlags |= PENDING_FLAG_ADD_TO_SIMULATION | PENDING_FLAG_REMOVE_FROM_SIMULATION;
        } else {
            _pendingFlags &= ~PENDING_FLAG_ADD_TO_SIMULATION;
        }
    } else {
        _pendingFlags &= ~ PENDING_FLAG_REMOVE_FROM_SIMULATION;
    }
}

void CharacterController::updateShapeIfNecessary() {
    if (_pendingFlags & PENDING_FLAG_UPDATE_SHAPE) {
        assert(!(_pendingFlags & PENDING_FLAG_REMOVE_FROM_SIMULATION));
        _pendingFlags &= ~ PENDING_FLAG_UPDATE_SHAPE;
        // make sure there is NO pending removal from simulation at this point
        // (don't want to delete _ghostObject out from under the simulation)
        // delete shape and GhostObject
        delete _ghostObject;
        _ghostObject = NULL;
        delete _convexShape;
        _convexShape = NULL;

        // compute new dimensions from avatar's bounding box
        float x = _boxScale.x;
        float z = _boxScale.z;
        _radius = 0.5f * sqrtf(0.5f * (x * x + z * z));
        _halfHeight = 0.5f * _boxScale.y - _radius;
        float MIN_HALF_HEIGHT = 0.1f;
        if (_halfHeight < MIN_HALF_HEIGHT) {
            _halfHeight = MIN_HALF_HEIGHT;
        }
        // NOTE: _shapeLocalOffset is already computed

        if (_radius > 0.0f) {
            // create new ghost
            _ghostObject = new btPairCachingGhostObject();
            _ghostObject->setWorldTransform(btTransform(glmToBullet(_avatarData->getOrientation()),
                                                            glmToBullet(_avatarData->getPosition())));
            // stepHeight affects the heights of ledges that the character can ascend
            _stepUpHeight = _radius + 0.25f * _halfHeight + 0.1f;
            _stepDownHeight = _radius;

            // create new shape
            _convexShape = new btCapsuleShape(_radius, 2.0f * _halfHeight);
            _ghostObject->setCollisionShape(_convexShape);
            _ghostObject->setCollisionFlags(btCollisionObject::CF_CHARACTER_OBJECT);
        } else {
            // TODO: handle this failure case
        }
    }
}

void CharacterController::preSimulation(btScalar timeStep) {
    if (_enabled && _dynamicsWorld) {
        glm::quat rotation = _avatarData->getOrientation();
        _currentUp = quatRotate(glmToBullet(rotation), LOCAL_UP_AXIS);
        glm::vec3 position = _avatarData->getPosition() + rotation * _shapeLocalOffset;
        btVector3 walkVelocity = glmToBullet(_avatarData->getVelocity());

        _ghostObject->setWorldTransform(btTransform(glmToBullet(rotation), glmToBullet(position)));
        setVelocityForTimeInterval(walkVelocity, timeStep);
        if (_pendingFlags & PENDING_FLAG_JUMP) {
            _pendingFlags &= ~ PENDING_FLAG_JUMP;
            if (canJump()) {
                _verticalVelocity = _jumpSpeed;
                _isJumping = true;
            }
        }
        // remember last position so we can throttle the total motion from the next step
        _lastPosition = position;
        _stepDt = 0.0f;
    }
}

void CharacterController::postSimulation() {
    if (_enabled && _ghostObject) {
        const btTransform& avatarTransform = _ghostObject->getWorldTransform();
        glm::quat rotation = bulletToGLM(avatarTransform.getRotation());
        glm::vec3 position = bulletToGLM(avatarTransform.getOrigin());

        // cap the velocity of the step so that the character doesn't POP! so hard on steps
        glm::vec3 finalStep = position - _lastPosition;
        btVector3 finalVelocity = _walkDirection;
        finalVelocity += _verticalVelocity * _currentUp;
        const btScalar MAX_RESOLUTION_SPEED = 5.0f; // m/sec
        btScalar maxStepLength = glm::max(MAX_RESOLUTION_SPEED, 2.0f * finalVelocity.length()) * _stepDt;
        btScalar stepLength = glm::length(finalStep);
        if (stepLength > maxStepLength) {
            position = _lastPosition + (maxStepLength / stepLength) * finalStep;
            // NOTE: we don't need to move ghostObject to throttled position unless 
            // we want to support do async ray-traces/collision-queries against character
        }

        _avatarData->setOrientation(rotation);
        _avatarData->setPosition(position - rotation * _shapeLocalOffset);
    }
}

