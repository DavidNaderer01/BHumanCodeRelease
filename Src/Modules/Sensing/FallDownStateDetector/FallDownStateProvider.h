/**
 * @File Modules/Sensing/FallDownStateProvider.cpp
 * This file declares a module that computes the current body state from sensor data
 * @author Daniel Krause
 */

#pragma once

#include "Debugging/Annotation.h"
#include "Framework/Module.h"
#include "Math/Geometry.h"
#include "Math/UnscentedKalmanFilter.h"
#include "Representations/Configuration/MassCalibration.h"
#include "Representations/Infrastructure/FrameInfo.h"
#include "Representations/Infrastructure/SensorData/FsrSensorData.h"
#include "Representations/Sensing/FallDownState.h"
#include "Representations/Sensing/GroundContactState.h"
#include "Representations/Sensing/InertialData.h"
#include "Representations/Sensing/InertialSensorData.h"
#include "Representations/Sensing/RobotModel.h"
#include "Tools/BehaviorControl/Cabsl.h"

MODULE(FallDownStateProvider,
{,
  REQUIRES(FrameInfo),
  REQUIRES(FsrSensorData),
  REQUIRES(GroundContactState),
  REQUIRES(InertialData),
  REQUIRES(InertialSensorData),
  REQUIRES(MassCalibration),
  REQUIRES(RobotModel),
  PROVIDES(FallDownState),
  DEFINES_PARAMETERS(
  {,
    (Angle)(10_deg) maxGyroToRegainStableState,
    (Angle)(100_deg) maxVelForPrediction,
    (bool)(true) useInertiaData,
    (bool)(false) playSounds,
    (float)(.9f) velocityDiscountFactor,
    (float)(0.1f) forwardingTime,
    (float)(0.5f) minNormalizedFallVectorScalar,
    (float)(0.65f) minFallVectorScalar,
    (float)(0.91f) minPredictNormalizedFallVectorScalar,
    (float)(1.1f) minPredictFallVectorScalar,
    (float)(10.f) minComDistanceToFootCenter,
    (float)(110.f) minComDistanceToDetetctFall,
    (float)(225.f) maxTorsoHeightToKeepSquatting,
    (float)(210.f) minTorsoHeightToKeepUpright,
    (float)(3.f) sigmaArea,
    (int)(150) minTimeBetweenSound,
    (int)(0) minTimeWithoutGroundContactToAssumePickup,
    (Vector2a)(55_deg, 55_deg) minTorsoOrientationToDetermineDirection,
    (int)(5000) maxTimeStaggering, /**< If the robot stays this long staggering, something is wrong and the filter is reset. */

    (Vector3f)(16.f, 16.f, 16.f) positionProcessDeviation, //  dynamic noise density of the com-position
    (Vector2a)(Vector2a::Constant(22_deg)) velocityProcessDeviation, // dynamic noise density of the velocity
    (Vector3f)(2.f, 2.f, 2.f) positionMeasurementDeviation, // measure noise of the com position
    (Vector2a)(Vector2a::Constant(3_deg)) velocityMeasurementDeviation, // measure noise of the gyro
  }),
});

class FallDownStateProvider : public FallDownStateProviderBase, public cabsl::Cabsl<FallDownStateProvider>
{
public:
  FallDownStateProvider();
private:
  bool falling; /**< Is the com outside of the support polygon */
  bool stable; /**< Torso has very limited rotational motion. */
  bool torsoUpright; /**< Is the com within sub support polygon? */
  bool toSquatting; /**< Are all conditions for a return to squatting from a fall met? */
  bool toUpright; /**< Are all conditions for a return to upright from a fall met? */
  bool isPickedUp; /**< Are all conditions for a return to picked up from a fall met? */
  bool useTorsoOrientation; /**< Torso orientation is big enough to be used. */
  bool disablePickUp; /**< For simulated robots, pick up is disabled. */
  const InertialSensorData* theSensorData; /**< Pointer to the sensor data.*/
  FallDownState* theFallDownState; /**< Pointer to the fall down state updated.*/
  FallDownState::Direction direction; /**< The fall direction. Always computed, even if not falling. */
  float torsoAboveGround; /**< The distance of the torso above the ground (in mm). */

  UKF<5> ukf = UKF<5>(Vector5f::Zero()); // The statevector of the ukf is composed of: com, velocity;
  Vector5f dynamicNoise;
  Vector5f measurementNoise;
  bool resetFilter = false;

  Legs::Leg supportFoot;
  Vector3f supportFootCenter; // relative to tilting edge
  Pose3f tiltingEdge,
         lastTiltingEdge;
  unsigned lastTimeSoundPlayed;
  std::vector<Vector3f> supportPolygon;

  void update(FallDownState& fallDownState) override;

  void initUKF(const Vector5f& initMean);
  void updateUKF();
  void dynamicModel(const Pose3f& originToTorso, const Matrix3f& I, Vector5f& state, float dt = Constants::motionCycleTime) const;
  Matrix3f calcInertiaTensor(const Pose3f& originToTorso) const;
  Vector5f measure() const;
  void convertToNewOrigin(const Pose3f& originToTorso, const Pose3f& newOriginToTorso, Vector5f& state) const;

  void getSupportPolygon();

  Pose3f getTiltingEdge();

  /**
   * 2D cross product of OA and OB vectors, i.e. z-component of their 3D cross product.
   * @return a positive value, if OAB makes a counter-clockwise turn,
   *         negative for clockwise turn, and zero if the points are collinear.
   */
  float cross(const Vector3f& O, const Vector3f& A, const Vector3f& B) const;

  /**
   * Creates a convex hull from a list of points.
   * Note: the last point in the returned list is the same as the first one.
   * @return a list of points on the convex hull in counter-clockwise order.
   */
  std::vector<Vector3f> getConvexHull(std::vector<Vector3f>& polygon) const;

  /**
   * Check if center of mass is outside of the support polygon.
   * @return true, if the robot is falling.
   */
  bool isFalling() const;

  /**
   * Check if the point is inside of the polygon.
   * @param point The point to check.
   * @param polygon The polygon of the support foot.
   * @return true, if the point is inside of the polygon.
   */
  bool isPointInsidePolygon(const Vector3f& point, const std::vector<Vector3f>& polygon) const;

  /**
   * Determines the fall direction.
   * @return the direction of fall
   */
  FallDownState::Direction getFallDirection() const;

  /**
   * Draw the debug drawings.
   */
  void draw() const;

  /**
   * Sets the output of this module.
   * @param state The fall down state.
   * @param direction The direction of the fall.
   * @param odometryRotationOffset The odometry offset created by the most recent state transition.
   */
  void setState(FallDownState::State state, FallDownState::Direction direction = FallDownState::none,
                Angle odometryRotationOffset = 0_deg);

  /**
   * Sets the output state and adds an odometry offset if the direction changed.
   * @param state The fall down state.
   */
  void setStateWithPossibleDirectionChange(FallDownState::State state);

  /**
   * Plays a sound of a given name if playback is activated.
   * @param file The text to be pronounced by the robot.
   */
  void say(const char* text);

#include "Options.h"
};
