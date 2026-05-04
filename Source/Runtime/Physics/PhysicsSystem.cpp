#include "PhysicsSystem.hpp"
#include <Jolt/RegisterTypes.h>
#include <Jolt/Core/Factory.h>
#include <Jolt/Physics/PhysicsSettings.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/Collision/Shape/MeshShape.h>
#include <Jolt/Physics/Collision/Shape/ConvexHullShape.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Collision/ContactListener.h>
#include <Jolt/Physics/Collision/Shape/StaticCompoundShape.h>
#include <Jolt/Physics/Constraints/HingeConstraint.h>
#include <Jolt/Physics/Constraints/PointConstraint.h>
#include <Jolt/Physics/Constraints/SwingTwistConstraint.h>
#include "../Event/EventSystem.hpp"
#include "../Event/Event.hpp"
//UI System 射线检测相关
#include <Jolt/Physics/Collision/RayCast.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Collision/Shape/ScaledShape.h>
#include <glm/gtx/matrix_decompose.hpp>
#include "../Debug/PhysicsDebugDraw.hpp"
#include "../Debug/DebugRenderer.hpp"

#include <iostream>
#include <cstdarg>
#include <thread>

#include <Jolt/Physics/Body/BodyFilter.h>
// callback for traces
static void TraceImpl(const char *inFMT, ...)
{
	va_list list;
	va_start(list, inFMT);
	char buffer[1024];
	vsnprintf(buffer, sizeof(buffer), inFMT, list);
	va_end(list);

	std::cout << buffer << std::endl;

}

#ifdef JPH_ENABLE_ASSERTS
// callback for asserts
static bool AssertFailedImpl(const char *inExpression, const char *inMessage, const char *inFile, uint32_t inLine)
{
	std::cout << inFile << ":" << inLine << ": (" << inExpression << ") " << (inMessage != nullptr? inMessage : "") << std::endl;
	// breakpoint
	return true;
}
#endif // JPH_ENABLE_ASSERTS

namespace BroadPhaseLayers
{
	static constexpr JPH::BroadPhaseLayer NON_MOVING(0);
	static constexpr JPH::BroadPhaseLayer MOVING(1);
	static constexpr JPH::uint NUM_LAYERS(2);
};

namespace engine {

class PhysicsSystem::BPLayerInterfaceImpl final : public JPH::BroadPhaseLayerInterface
{
public:
	BPLayerInterfaceImpl()
	{
		mObjectToBroadPhase[Layers::NON_MOVING] = BroadPhaseLayers::NON_MOVING;
		mObjectToBroadPhase[Layers::MOVING] = BroadPhaseLayers::MOVING;
	}

	virtual JPH::uint GetNumBroadPhaseLayers() const override { return BroadPhaseLayers::NUM_LAYERS; }
	virtual JPH::BroadPhaseLayer GetBroadPhaseLayer(JPH::ObjectLayer inLayer) const override
	{
		JPH_ASSERT(inLayer < Layers::NUM_LAYERS);
		return mObjectToBroadPhase[inLayer];
	}
#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLE)
	virtual const char *GetBroadPhaseLayerName(JPH::BroadPhaseLayer inLayer) const override
	{
		switch ((JPH::BroadPhaseLayer::Type)inLayer)
		{
		case (JPH::BroadPhaseLayer::Type)BroadPhaseLayers::NON_MOVING:	return "NON_MOVING";
		case (JPH::BroadPhaseLayer::Type)BroadPhaseLayers::MOVING:		return "MOVING";
		default:														JPH_ASSERT(false); return "INVALID";
		}
	}
#endif // JPH_EXTERNAL_PROFILE || JPH_PROFILE_ENABLE
private:
	JPH::BroadPhaseLayer					mObjectToBroadPhase[Layers::NUM_LAYERS];
};

class PhysicsSystem::ObjectVsBroadPhaseLayerFilterImpl : public JPH::ObjectVsBroadPhaseLayerFilter
{
public:
	virtual bool ShouldCollide(JPH::ObjectLayer inLayer1, JPH::BroadPhaseLayer inLayer2) const override
	{
		switch (inLayer1)
		{
		case Layers::NON_MOVING:
			return inLayer2 == BroadPhaseLayers::MOVING;
		case Layers::MOVING:
			return true;
		default:
			JPH_ASSERT(false);
			return false;
		}
	}
};

class PhysicsSystem::ObjectLayerPairFilterImpl : public JPH::ObjectLayerPairFilter
{
public:
	virtual bool ShouldCollide(JPH::ObjectLayer inObject1, JPH::ObjectLayer inObject2) const override
	{
		switch (inObject1)
		{
		case Layers::NON_MOVING:
			return inObject2 == Layers::MOVING; // non moving only collides with moving
		case Layers::MOVING:
			return true; // moving collides with everything
		default:
			JPH_ASSERT(false);
			return false;
		}
	}
};

class PhysicsSystem::ContactListenerImpl final : public JPH::ContactListener
{
public:
	ContactListenerImpl(PhysicsSystem* sys) : m_sys(sys) {}

	virtual JPH::ValidateResult OnContactValidate(const JPH::Body& inBody1, const JPH::Body& inBody2, JPH::RVec3Arg inBaseOffset, const JPH::CollideShapeResult& inCollisionResult) override
	{
		return JPH::ValidateResult::AcceptAllContactsForThisBodyPair;
	}

	virtual void OnContactAdded(const JPH::Body& inBody1, const JPH::Body& inBody2, const JPH::ContactManifold& inManifold, JPH::ContactSettings& ioSettings) override
	{
		// only broadcast intersection events if the game attached an active EventSystem
		if (m_sys->m_eventSystem) {
			std::string nameA = std::to_string(inBody1.GetID().GetIndexAndSequenceNumber());
			std::string nameB = std::to_string(inBody2.GetID().GetIndexAndSequenceNumber());
			
			auto event = std::make_unique<CollisionEvent>(nameA, nameB);
			m_sys->m_eventSystem->QueueEvent(std::move(event));
		}
	}
private:
	PhysicsSystem* m_sys;
};

PhysicsSystem::PhysicsSystem()
{
}

PhysicsSystem::~PhysicsSystem()
{
	Shutdown();
}

void PhysicsSystem::Init()
{
	JPH::RegisterDefaultAllocator();

	JPH::Trace = TraceImpl;
	JPH_IF_ENABLE_ASSERTS(JPH::AssertFailed = AssertFailedImpl;)

	JPH::Factory::sInstance = new JPH::Factory();
	JPH::RegisterTypes();

	m_tempAllocator = std::make_unique<JPH::TempAllocatorImpl>(10 * 1024 * 1024);
	m_jobSystem = std::make_unique<JPH::JobSystemThreadPool>(JPH::cMaxPhysicsJobs, JPH::cMaxPhysicsBarriers, std::thread::hardware_concurrency() - 1);

	const JPH::uint cMaxBodies = 10240;
	const JPH::uint cNumBodyMutexes = 0;
	const JPH::uint cMaxBodyPairs = 10240;
	const JPH::uint cMaxContactConstraints = 10240;

	m_bpLayerInterface = std::make_unique<BPLayerInterfaceImpl>();
	m_objectVsBroadphaseFilter = std::make_unique<ObjectVsBroadPhaseLayerFilterImpl>();
	m_objectVsObjectFilter = std::make_unique<ObjectLayerPairFilterImpl>();

	m_physicsSystem = std::make_unique<JPH::PhysicsSystem>();
	m_physicsSystem->Init(cMaxBodies, cNumBodyMutexes, cMaxBodyPairs, cMaxContactConstraints, *m_bpLayerInterface, *m_objectVsBroadphaseFilter, *m_objectVsObjectFilter);
	
	// register the contact listener to listen to collisions
	m_contactListener = std::make_unique<ContactListenerImpl>(this);
	m_physicsSystem->SetContactListener(m_contactListener.get());

}

void PhysicsSystem::optimize_broad_phase()
{
	if (m_physicsSystem) {
		m_physicsSystem->OptimizeBroadPhase();
	}
}




void PhysicsSystem::Update(float dt)
{
	if (!m_physicsSystem) {
		return;
	}

	if (mInputSystem && mInputSystem->IsActionPressed("Jump")) {
		JPH::BodyInterface& bodyInterface = m_physicsSystem->GetBodyInterface();

		for (const auto& ragdoll : m_ragdolls) {
			if (ragdoll.bodyIDs.empty()) {
				continue;
			}

			JPH::BodyID pelvisID = ragdoll.bodyIDs.front();
			if (pelvisID.IsInvalid() || !bodyInterface.IsAdded(pelvisID)) {
				continue;
			}

			bool isGrounded = false;
			for (JPH::BodyID bodyID : ragdoll.bodyIDs) {
				if (bodyID.IsInvalid() || !bodyInterface.IsAdded(bodyID)) {
					continue;
				}

				JPH::RVec3 bodyPos = bodyInterface.GetPosition(bodyID);
				JPH::RRayCast ray{ bodyPos, JPH::Vec3(0.0f, -1.35f, 0.0f) };
				JPH::RayCastResult hit;
				JPH::IgnoreSingleBodyFilter bodyFilter(bodyID);
				if (m_physicsSystem->GetNarrowPhaseQuery().CastRay(ray, hit, { }, { }, bodyFilter)) {
					isGrounded = true;
					break;
				}
			}

			if (!isGrounded) {
				continue;
			}

			for (JPH::BodyID bodyID : ragdoll.bodyIDs) {
				if (!bodyID.IsInvalid() && bodyInterface.IsAdded(bodyID)) {
					JPH::Vec3 vel = bodyInterface.GetLinearVelocity(bodyID);
					vel.SetY(vel.GetY() + 10.0f);
					bodyInterface.SetLinearVelocity(bodyID, vel);
				}
			}

			JPH::Vec3 pelvisAngularVelocity = bodyInterface.GetAngularVelocity(pelvisID);
			pelvisAngularVelocity += JPH::Vec3(2.2f, 0.0f, 0.8f);
			bodyInterface.SetAngularVelocity(pelvisID, pelvisAngularVelocity);
		}
	}

	const int cCollisionSteps = 1;
	// Step the system
	m_physicsSystem->Update(dt, cCollisionSteps, m_tempAllocator.get(), m_jobSystem.get());
}
void PhysicsSystem::Shutdown()
{
	ClearRagdolls();

	if (m_physicsSystem) {
		m_physicsSystem = nullptr;
		m_jobSystem = nullptr;
		m_tempAllocator = nullptr;
		
		m_bpLayerInterface = nullptr;
		m_objectVsBroadphaseFilter = nullptr;
		m_objectVsObjectFilter = nullptr;

		JPH::UnregisterTypes();
		delete JPH::Factory::sInstance;
		JPH::Factory::sInstance = nullptr;
	}
}

size_t PhysicsSystem::CreateCapsuleRagdoll(const glm::vec3& pelvisPosition)
{
	if (!m_physicsSystem) {
		return static_cast<size_t>(-1);
	}

	JPH::BodyInterface& bodyInterface = m_physicsSystem->GetBodyInterface();
	SimpleRagdoll ragdoll{};

	constexpr JPH::CollisionGroup::GroupID kRagdollGroupID = 7;
	ragdoll.collisionFilter = new JPH::GroupFilterTable(11);
	for (JPH::CollisionGroup::SubGroupID a = 0; a < 11; ++a) {
		for (JPH::CollisionGroup::SubGroupID b = a + 1; b < 11; ++b) {
			ragdoll.collisionFilter->DisableCollision(a, b);
		}
	}

	auto createBody = [&](const JPH::Shape* shape, const glm::vec3& position, const JPH::Quat& rotation, JPH::CollisionGroup::SubGroupID subGroupID, float linearDamping = 0.05f, float angularDamping = 0.05f) -> JPH::BodyID {
		JPH::BodyCreationSettings settings(
			shape,
			JPH::RVec3(position.x, position.y, position.z),
			rotation,
			JPH::EMotionType::Dynamic,
			Layers::MOVING
		);
		settings.mMotionQuality = JPH::EMotionQuality::LinearCast;
		settings.mLinearDamping = linearDamping;
		settings.mAngularDamping = angularDamping;
		settings.mCollisionGroup = JPH::CollisionGroup(ragdoll.collisionFilter, kRagdollGroupID, subGroupID);

		JPH::Body* body = bodyInterface.CreateBody(settings);
		if (!body) {
			return JPH::BodyID();
		}

		JPH::BodyID bodyID = body->GetID();
		bodyInterface.AddBody(bodyID, JPH::EActivation::Activate);
		bodyInterface.SetGravityFactor(bodyID, 1.2f);
		ragdoll.bodyIDs.push_back(bodyID);
		return bodyID;
	};

	JPH::ShapeRefC headShape = new JPH::SphereShape(0.22f);
	JPH::ShapeRefC chestShape = new JPH::CapsuleShape(0.45f, 0.22f);
	JPH::ShapeRefC pelvisShape = new JPH::BoxShape(JPH::Vec3(0.22f, 0.16f, 0.16f));
	JPH::ShapeRefC upperArmShape = new JPH::CapsuleShape(0.28f, 0.09f);
	JPH::ShapeRefC lowerArmShape = new JPH::CapsuleShape(0.26f, 0.08f);
	JPH::ShapeRefC upperLegShape = new JPH::CapsuleShape(0.40f, 0.11f);
	JPH::ShapeRefC lowerLegShape = new JPH::CapsuleShape(0.38f, 0.10f);

	const glm::vec3 pelvisPos = pelvisPosition;
	const glm::vec3 chestPos = pelvisPos + glm::vec3(0.0f, 0.55f, 0.0f);
	const glm::vec3 headPos = pelvisPos + glm::vec3(0.0f, 1.18f, 0.0f);

	const glm::vec3 leftUpperArmPos = chestPos + glm::vec3(-0.50f, 0.18f, 0.0f);
	const glm::vec3 rightUpperArmPos = chestPos + glm::vec3(0.50f, 0.18f, 0.0f);
	const glm::vec3 leftLowerArmPos = chestPos + glm::vec3(-1.05f, 0.18f, 0.0f);
	const glm::vec3 rightLowerArmPos = chestPos + glm::vec3(1.05f, 0.18f, 0.0f);

	const glm::vec3 leftUpperLegPos = pelvisPos + glm::vec3(-0.16f, -0.58f, 0.0f);
	const glm::vec3 rightUpperLegPos = pelvisPos + glm::vec3(0.16f, -0.58f, 0.0f);
	const glm::vec3 leftLowerLegPos = pelvisPos + glm::vec3(-0.16f, -1.38f, 0.0f);
	const glm::vec3 rightLowerLegPos = pelvisPos + glm::vec3(0.16f, -1.38f, 0.0f);

	const JPH::Quat horizontalLimbRotation = JPH::Quat::sRotation(JPH::Vec3::sAxisZ(), 0.5f * JPH::JPH_PI);

	JPH::BodyID pelvisID = createBody(pelvisShape.GetPtr(), pelvisPos, JPH::Quat::sIdentity(), 0, 0.03f, 0.12f);
	JPH::BodyID chestID = createBody(chestShape.GetPtr(), chestPos, JPH::Quat::sIdentity(), 1, 0.03f, 0.12f);
	JPH::BodyID headID = createBody(headShape.GetPtr(), headPos, JPH::Quat::sIdentity(), 2, 0.03f, 0.08f);
	JPH::BodyID leftUpperArmID = createBody(upperArmShape.GetPtr(), leftUpperArmPos, horizontalLimbRotation, 3);
	JPH::BodyID rightUpperArmID = createBody(upperArmShape.GetPtr(), rightUpperArmPos, horizontalLimbRotation, 4);
	JPH::BodyID leftLowerArmID = createBody(lowerArmShape.GetPtr(), leftLowerArmPos, horizontalLimbRotation, 5);
	JPH::BodyID rightLowerArmID = createBody(lowerArmShape.GetPtr(), rightLowerArmPos, horizontalLimbRotation, 6);
	JPH::BodyID leftUpperLegID = createBody(upperLegShape.GetPtr(), leftUpperLegPos, JPH::Quat::sIdentity(), 7);
	JPH::BodyID rightUpperLegID = createBody(upperLegShape.GetPtr(), rightUpperLegPos, JPH::Quat::sIdentity(), 8);
	JPH::BodyID leftLowerLegID = createBody(lowerLegShape.GetPtr(), leftLowerLegPos, JPH::Quat::sIdentity(), 9);
	JPH::BodyID rightLowerLegID = createBody(lowerLegShape.GetPtr(), rightLowerLegPos, JPH::Quat::sIdentity(), 10);

	auto addSwingTwistConstraint = [&](JPH::BodyID bodyA, JPH::BodyID bodyB, const glm::vec3& anchor,
		const glm::vec3& twistAxis, const glm::vec3& planeAxis,
		float coneAngleDeg, float planeAngleDeg, float twistMinDeg, float twistMaxDeg, float frictionTorque = 25.0f) {
		if (bodyA.IsInvalid() || bodyB.IsInvalid()) {
			return;
		}

		JPH::SwingTwistConstraintSettings settings;
		settings.mSpace = JPH::EConstraintSpace::WorldSpace;
		settings.mPosition1 = JPH::RVec3(anchor.x, anchor.y, anchor.z);
		settings.mPosition2 = JPH::RVec3(anchor.x, anchor.y, anchor.z);
		settings.mTwistAxis1 = JPH::Vec3(twistAxis.x, twistAxis.y, twistAxis.z);
		settings.mTwistAxis2 = JPH::Vec3(twistAxis.x, twistAxis.y, twistAxis.z);
		settings.mPlaneAxis1 = JPH::Vec3(planeAxis.x, planeAxis.y, planeAxis.z);
		settings.mPlaneAxis2 = JPH::Vec3(planeAxis.x, planeAxis.y, planeAxis.z);
		settings.mNormalHalfConeAngle = glm::radians(coneAngleDeg);
		settings.mPlaneHalfConeAngle = glm::radians(planeAngleDeg);
		settings.mTwistMinAngle = glm::radians(twistMinDeg);
		settings.mTwistMaxAngle = glm::radians(twistMaxDeg);
		settings.mMaxFrictionTorque = frictionTorque;

		JPH::Constraint* constraint = bodyInterface.CreateConstraint(&settings, bodyA, bodyB);
		if (constraint) {
			m_physicsSystem->AddConstraint(constraint);
			ragdoll.constraints.push_back(constraint);
		}
	};

	auto addHingeConstraint = [&](JPH::BodyID bodyA, JPH::BodyID bodyB, const glm::vec3& anchor,
		const glm::vec3& hingeAxis, const glm::vec3& normalAxis,
		float minAngleDeg, float maxAngleDeg, float frictionTorque = 20.0f) {
		if (bodyA.IsInvalid() || bodyB.IsInvalid()) {
			return;
		}

		JPH::HingeConstraintSettings settings;
		settings.mSpace = JPH::EConstraintSpace::WorldSpace;
		settings.mPoint1 = JPH::RVec3(anchor.x, anchor.y, anchor.z);
		settings.mPoint2 = JPH::RVec3(anchor.x, anchor.y, anchor.z);
		settings.mHingeAxis1 = JPH::Vec3(hingeAxis.x, hingeAxis.y, hingeAxis.z);
		settings.mHingeAxis2 = JPH::Vec3(hingeAxis.x, hingeAxis.y, hingeAxis.z);
		settings.mNormalAxis1 = JPH::Vec3(normalAxis.x, normalAxis.y, normalAxis.z);
		settings.mNormalAxis2 = JPH::Vec3(normalAxis.x, normalAxis.y, normalAxis.z);
		settings.mLimitsMin = glm::radians(minAngleDeg);
		settings.mLimitsMax = glm::radians(maxAngleDeg);
		settings.mMaxFrictionTorque = frictionTorque;

		JPH::Constraint* constraint = bodyInterface.CreateConstraint(&settings, bodyA, bodyB);
		if (constraint) {
			m_physicsSystem->AddConstraint(constraint);
			ragdoll.constraints.push_back(constraint);
		}
	};

	addSwingTwistConstraint(headID, chestID, pelvisPos + glm::vec3(0.0f, 0.92f, 0.0f),
		glm::vec3(0.0f, 1.0f, 0.0f), glm::vec3(0.0f, 0.0f, 1.0f),
		20.0f, 25.0f, -30.0f, 30.0f, 18.0f);
	addSwingTwistConstraint(chestID, pelvisID, pelvisPos + glm::vec3(0.0f, 0.28f, 0.0f),
		glm::vec3(0.0f, 1.0f, 0.0f), glm::vec3(0.0f, 0.0f, 1.0f),
		18.0f, 15.0f, -20.0f, 20.0f, 30.0f);

	addSwingTwistConstraint(chestID, leftUpperArmID, chestPos + glm::vec3(-0.24f, 0.18f, 0.0f),
		glm::vec3(-1.0f, 0.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f),
		55.0f, 40.0f, -55.0f, 55.0f, 18.0f);
	addSwingTwistConstraint(chestID, rightUpperArmID, chestPos + glm::vec3(0.24f, 0.18f, 0.0f),
		glm::vec3(1.0f, 0.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f),
		55.0f, 40.0f, -55.0f, 55.0f, 18.0f);

	addHingeConstraint(leftUpperArmID, leftLowerArmID, chestPos + glm::vec3(-0.78f, 0.18f, 0.0f),
		glm::vec3(0.0f, 0.0f, 1.0f), glm::vec3(-1.0f, 0.0f, 0.0f),
		0.0f, 145.0f, 10.0f);
	addHingeConstraint(rightUpperArmID, rightLowerArmID, chestPos + glm::vec3(0.78f, 0.18f, 0.0f),
		glm::vec3(0.0f, 0.0f, 1.0f), glm::vec3(1.0f, 0.0f, 0.0f),
		-145.0f, 0.0f, 10.0f);

	addSwingTwistConstraint(pelvisID, leftUpperLegID, pelvisPos + glm::vec3(-0.14f, -0.20f, 0.0f),
		glm::vec3(0.0f, -1.0f, 0.0f), glm::vec3(-1.0f, 0.0f, 0.0f),
		35.0f, 25.0f, -35.0f, 55.0f, 22.0f);
	addSwingTwistConstraint(pelvisID, rightUpperLegID, pelvisPos + glm::vec3(0.14f, -0.20f, 0.0f),
		glm::vec3(0.0f, -1.0f, 0.0f), glm::vec3(1.0f, 0.0f, 0.0f),
		35.0f, 25.0f, -35.0f, 55.0f, 22.0f);

	addHingeConstraint(leftUpperLegID, leftLowerLegID, pelvisPos + glm::vec3(-0.16f, -0.98f, 0.0f),
		glm::vec3(0.0f, 0.0f, 1.0f), glm::vec3(-1.0f, 0.0f, 0.0f),
		0.0f, 125.0f, 14.0f);
	addHingeConstraint(rightUpperLegID, rightLowerLegID, pelvisPos + glm::vec3(0.16f, -0.98f, 0.0f),
		glm::vec3(0.0f, 0.0f, 1.0f), glm::vec3(1.0f, 0.0f, 0.0f),
		-125.0f, 0.0f, 14.0f);

	m_physicsSystem->OptimizeBroadPhase();
	m_ragdolls.push_back(std::move(ragdoll));
	return m_ragdolls.size() - 1;
}

void PhysicsSystem::ClearRagdolls()
{
	if (!m_physicsSystem) {
		m_ragdolls.clear();
		return;
	}

	JPH::BodyInterface& bodyInterface = m_physicsSystem->GetBodyInterface();
	for (auto& ragdoll : m_ragdolls) {
		for (JPH::Constraint* constraint : ragdoll.constraints) {
			if (constraint) {
				m_physicsSystem->RemoveConstraint(constraint);
			}
		}

		for (JPH::BodyID bodyID : ragdoll.bodyIDs) {
			if (!bodyID.IsInvalid() && bodyInterface.IsAdded(bodyID)) {
				bodyInterface.RemoveBody(bodyID);
				bodyInterface.DestroyBody(bodyID);
			}
		}
	}

	m_ragdolls.clear();
}

void PhysicsSystem::DebugDrawRagdolls(DebugRenderer& debugRenderer) const
{
	if (!m_physicsSystem) {
		return;
	}

	const JPH::BodyInterface& bodyInterface = m_physicsSystem->GetBodyInterface();
	for (const auto& ragdoll : m_ragdolls) {
		for (JPH::BodyID bodyID : ragdoll.bodyIDs) {
			if (bodyID.IsInvalid() || !bodyInterface.IsAdded(bodyID)) {
				continue;
			}

			JPH::TransformedShape transformedShape = bodyInterface.GetTransformedShape(bodyID);
			physics_debug::DrawCollisionShapeWireframe(debugRenderer, transformedShape, glm::vec3(1.0f));
		}
	}
}

JPH::BodyID PhysicsSystem::create_sphere_body(const glm::vec3& position, float radius)
{
	JPH::BodyInterface& bodyInterface = m_physicsSystem->GetBodyInterface();

	JPH::SphereShapeSettings sphereSettings(radius);
	sphereSettings.mDensity = 1000.f; // kg/m³

	JPH::ShapeRefC shape = sphereSettings.Create().Get();

	JPH::BodyCreationSettings bodySettings(
		shape,
		JPH::RVec3(position.x, position.y, position.z),
		JPH::Quat::sIdentity(),
		JPH::EMotionType::Dynamic,
		Layers::MOVING
	);
	bodySettings.mGravityFactor = 1.0f;

	JPH::Body* body = bodyInterface.CreateBody(bodySettings);
	JPH::BodyID bodyID = body->GetID();
	bodyInterface.AddBody(bodyID, JPH::EActivation::Activate);

	// ensure broad phase is optimised after adding the body
	m_physicsSystem->OptimizeBroadPhase();

	return bodyID;
}

void PhysicsSystem::create_ground_plane(float y)
{
	JPH::BodyInterface& bodyInterface = m_physicsSystem->GetBodyInterface();

	// 200 x 0.1 x 200 metre flat box centred at (0, y-0.05, 0)
	JPH::BoxShapeSettings groundSettings(JPH::Vec3(100.f, 0.05f, 100.f));
	JPH::ShapeRefC groundShape = groundSettings.Create().Get();

	JPH::BodyCreationSettings groundBodySettings(
		groundShape,
		JPH::RVec3(0.f, y - 0.05f, 0.f),
		JPH::Quat::sIdentity(),
		JPH::EMotionType::Static,
		Layers::NON_MOVING
	);

	JPH::Body* groundBody = bodyInterface.CreateBody(groundBodySettings);
	bodyInterface.AddBody(groundBody->GetID(), JPH::EActivation::DontActivate);

	m_physicsSystem->OptimizeBroadPhase();
}

JPH::BodyID PhysicsSystem::create_static_mesh_body(const EngineMesh& mesh, const glm::mat4& transform)
{
	JPH::BodyInterface& bodyInterface = m_physicsSystem->GetBodyInterface();

	// 1. Build Vertex List
	JPH::VertexList vertices;
	vertices.reserve(mesh.positions.size());
	for (const auto& pos : mesh.positions) {
		vertices.push_back(JPH::Float3(pos.x, pos.y, pos.z));
	}

	// 2. Build Triangle List
	JPH::IndexedTriangleList triangles;
	auto numTriangles = mesh.indices.size() / 3;
	triangles.reserve(numTriangles);
	for (size_t i = 0; i < mesh.indices.size(); i += 3) {
		triangles.emplace_back(
			mesh.indices[i],
			mesh.indices[i + 1],
			mesh.indices[i + 2]
		);
	}

	// 3. Create MeshShapeSettings
	JPH::MeshShapeSettings shapeSettings(vertices, triangles);
	JPH::ShapeSettings::ShapeResult shapeResult = shapeSettings.Create();
	if (shapeResult.HasError()) {
		std::cout << "Error creating MeshShape: " << shapeResult.GetError() << std::endl;
		return JPH::BodyID(); // Invalid BodyID
	}
	JPH::ShapeRefC shape = shapeResult.Get();

	// 4. Extract position and rotation from transform
	glm::vec3 scale;
	glm::quat rotation;
	glm::vec3 translation;
	glm::vec3 skew;
	glm::vec4 perspective;
	glm::decompose(transform, scale, rotation, translation, skew, perspective);
	rotation = glm::normalize(rotation);

	//fix
	if (scale != glm::vec3(1.0f)) {
		JPH::ScaledShapeSettings scaledSettings(shape, JPH::Vec3(scale.x, scale.y, scale.z));
		shape = scaledSettings.Create().Get();
	}


	JPH::BodyCreationSettings bodySettings(
		shape,
		JPH::RVec3(translation.x, translation.y, translation.z),
		toJolt(rotation),
		JPH::EMotionType::Static,
		Layers::NON_MOVING
	);

	JPH::Body* body = bodyInterface.CreateBody(bodySettings);
	if (!body) {
		std::cout << "Error creating static mesh body." << std::endl;
		return JPH::BodyID();
	}

	bodyInterface.AddBody(body->GetID(), JPH::EActivation::DontActivate);
	return body->GetID();
}

JPH::BodyID PhysicsSystem::create_dynamic_convex_body(const EngineMesh& mesh, const glm::mat4& transform, float mass)
{
	JPH::BodyInterface& bodyInterface = m_physicsSystem->GetBodyInterface();

	// Extract position, rotation, scale from transform
	glm::vec3 scale;
	glm::quat rotation;
	glm::vec3 translation;
	glm::vec3 skew;
	glm::vec4 perspective;
	glm::decompose(transform, scale, rotation, translation, skew, perspective);
	rotation = glm::normalize(rotation);
	// 1. Build Vertex List (applying scale directly)
	JPH::Array<JPH::Vec3> points;
	points.resize(mesh.positions.size());
	for (size_t i = 0; i < mesh.positions.size(); ++i) {
		const auto& pos = mesh.positions[i];
		points[i] = JPH::Vec3(pos.x * scale.x, pos.y * scale.y, pos.z * scale.z);
	}

	// 2. Create ConvexHullShapeSettings
	JPH::ConvexHullShapeSettings shapeSettings(points);
	shapeSettings.mDensity = mass > 0.0f ? 1000.0f : 1.0f; // Simplified density proxy
	
	JPH::ShapeSettings::ShapeResult shapeResult = shapeSettings.Create();
	if (shapeResult.HasError()) {
		std::cout << "Error creating ConvexHullShape: " << shapeResult.GetError() << std::endl;
		return JPH::BodyID(); 
	}
	JPH::ShapeRefC shape = shapeResult.Get();

	JPH::BodyCreationSettings bodySettings(
		shape,
		JPH::RVec3(translation.x, translation.y, translation.z),
		toJolt(rotation),
		JPH::EMotionType::Dynamic,
		Layers::MOVING
	);
	
	// Enable Continuous Collision Detection (CCD) to prevent tunneling at high speeds
	bodySettings.mMotionQuality = JPH::EMotionQuality::LinearCast;

	JPH::Body* body = bodyInterface.CreateBody(bodySettings);
	if (!body) {
		std::cout << "Error creating dynamic convex body." << std::endl;
		return JPH::BodyID();
	}

	bodyInterface.AddBody(body->GetID(), JPH::EActivation::Activate);
	return body->GetID();
}

JPH::BodyID PhysicsSystem::create_dynamic_compound_body(
	const std::vector<EngineMesh>& meshes,
	const std::vector<glm::mat4>& meshTransforms,
	const glm::mat4& transform,
	float mass)
{
	JPH::BodyInterface& bodyInterface = m_physicsSystem->GetBodyInterface();



	// Extract position, rotation, scale from world transform
	glm::vec3 scale;
	glm::quat rotation;
	glm::vec3 translation;
	glm::vec3 skew;
	glm::vec4 perspective;
	glm::decompose(transform, scale, rotation, translation, skew, perspective);
	rotation = glm::normalize(rotation);
	// 1. Build one ConvexHull per mesh, add to compound
	JPH::StaticCompoundShapeSettings compoundSettings;
	int partCount = 0;

	for (size_t i = 0; i < meshes.size(); ++i) {
		const auto& mesh = meshes[i];

		glm::vec3 partScale, partTranslation, partSkew;
		glm::quat partRotation;
		glm::vec4 partPersp;
		glm::decompose(meshTransforms[i], partScale, partRotation,
			partTranslation, partSkew, partPersp);
		partRotation = glm::normalize(partRotation);


		glm::vec3 finalScale = scale * partScale;

		JPH::Array<JPH::Vec3> points;
		points.resize(mesh.positions.size());
		for (size_t v = 0; v < mesh.positions.size(); ++v) {
			const auto& pos = mesh.positions[v];
			points[v] = JPH::Vec3(pos.x * finalScale.x,
				pos.y * finalScale.y,
				pos.z * finalScale.z);
		}
		if (points.empty()) continue;

		JPH::ConvexHullShapeSettings hullSettings(points);
		JPH::ShapeSettings::ShapeResult hullResult = hullSettings.Create();
		if (hullResult.HasError()) {
			std::cout << "Error: " << hullResult.GetError() << std::endl;
			continue;
		}


		JPH::Vec3 partOffset(
			(partTranslation.x - translation.x) * scale.x,
			(partTranslation.y - translation.y) * scale.y,
			(partTranslation.z - translation.z) * scale.z
		);

		compoundSettings.AddShape(partOffset, toJolt(partRotation), hullResult.Get());
		partCount++;
	}

	if (partCount == 0) {
		std::cout << "Error: no valid convex hulls for compound body." << std::endl;
		return JPH::BodyID();
	}

	// 2. Create compound shape
	JPH::ShapeSettings::ShapeResult compoundResult = compoundSettings.Create();
	if (compoundResult.HasError()) {
		std::cout << "Error creating CompoundShape: " << compoundResult.GetError() << std::endl;
		return JPH::BodyID();
	}
	JPH::ShapeRefC compoundShape = compoundResult.Get();


	// 3. Create body 
	JPH::BodyCreationSettings bodySettings(
		compoundShape,
		JPH::RVec3(translation.x, translation.y, translation.z),
		toJolt(rotation),
		JPH::EMotionType::Dynamic,
		Layers::MOVING
	);
	bodySettings.mOverrideMassProperties = JPH::EOverrideMassProperties::CalculateInertia;
	bodySettings.mMassPropertiesOverride.mMass = mass;
	bodySettings.mMotionQuality = JPH::EMotionQuality::LinearCast;
	bodySettings.mLinearDamping = 1.0f;    
	bodySettings.mAngularDamping = 10.0f;  

	//TODO: TEST BIKE NOT ROTATION
	//bodySettings.mAllowedDOFs =
	//	JPH::EAllowedDOFs::TranslationX |
	//	JPH::EAllowedDOFs::TranslationY |
	//	JPH::EAllowedDOFs::TranslationZ;

	JPH::Body* body = bodyInterface.CreateBody(bodySettings);
	if (!body) {
		std::cout << "Error creating dynamic compound body." << std::endl;
		return JPH::BodyID();
	}

	bodyInterface.AddBody(body->GetID(), JPH::EActivation::Activate);
	std::cout << "[Compound] Created body with " << partCount << " sub-shapes." << std::endl;


	return body->GetID();
}


// 实现带有忽略逻辑的射线
uint32_t PhysicsSystem::cast_ray_ignore(const glm::vec3& start, const glm::vec3& end, uint32_t ignoreBodyIDRaw)
{
	if (!m_physicsSystem) return JPH::BodyID::cInvalidBodyID;

	JPH::RVec3 joltStart(start.x, start.y, start.z);
	JPH::Vec3 joltDir(end.x - start.x, end.y - start.y, end.z - start.z);
	JPH::RRayCast ray(joltStart, joltDir);

	JPH::RayCastResult hitResult;

	JPH::IgnoreSingleBodyFilter ignoreFilter{ JPH::BodyID(ignoreBodyIDRaw) };

	bool isHit = m_physicsSystem->GetNarrowPhaseQuery().CastRay(
		ray,
		hitResult,
		JPH::BroadPhaseLayerFilter(),
		JPH::ObjectLayerFilter(),
		ignoreFilter 
	);

	if (isHit) {
		return hitResult.mBodyID.GetIndexAndSequenceNumber();
	}

	return JPH::BodyID::cInvalidBodyID;
}
uint32_t PhysicsSystem::cast_ray_ignore_multiple(const glm::vec3& start, const glm::vec3& end, const std::vector<uint32_t>& ignoreIDs)
{
	if (!m_physicsSystem) return JPH::BodyID::cInvalidBodyID;

	JPH::RVec3 joltStart(start.x, start.y, start.z);
	JPH::Vec3 joltDir(end.x - start.x, end.y - start.y, end.z - start.z);
	JPH::RRayCast ray(joltStart, joltDir);
	JPH::RayCastResult hitResult;

	class MultiIgnoreFilter : public JPH::BodyFilter {
		const std::vector<uint32_t>& m_ignores;
	public:
		MultiIgnoreFilter(const std::vector<uint32_t>& ignores) : m_ignores(ignores) {}
		virtual bool ShouldCollide(const JPH::BodyID& inBodyID) const override {
			uint32_t rawID = inBodyID.GetIndexAndSequenceNumber();
			for (uint32_t id : m_ignores) {
				if (rawID == id) return false;
			}
			return true;
		}
	};

	MultiIgnoreFilter filter(ignoreIDs);

	bool isHit = m_physicsSystem->GetNarrowPhaseQuery().CastRay(
		ray, hitResult,
		JPH::BroadPhaseLayerFilter(), JPH::ObjectLayerFilter(), filter
	);

	if (isHit) return hitResult.mBodyID.GetIndexAndSequenceNumber();
	return JPH::BodyID::cInvalidBodyID;
}
//=============================UI System Interactions=============================
uint32_t PhysicsSystem::cast_ray(const glm::vec3& origin, const glm::vec3& direction, float max_distance)
{
	if (!m_physicsSystem) return JPH::BodyID::cInvalidBodyID;

	//Jolt射线 (起点 和 方向向量)
	//jolt statrt point and direction vector
	JPH::RVec3 joltOrigin(origin.x, origin.y, origin.z);
	//射线长度
	//ray length
	JPH::Vec3 joltDir(direction.x * max_distance, direction.y * max_distance, direction.z * max_distance);
	JPH::RRayCast ray(joltOrigin, joltDir);

	JPH::RayCastResult hitResult;//for storing the result of the raycast

	// 执行射线检测
	// starts the raycast and stores the result in hitResult
	// GetNarrowPhaseQuery() 会进行精确的物理形状检测
	bool isHit = m_physicsSystem->GetNarrowPhaseQuery().CastRay(
		ray,
		hitResult,
		JPH::BroadPhaseLayerFilter(),
		JPH::ObjectLayerFilter(),
		JPH::BodyFilter()
	);

	if (isHit) {
		// 返回击中的 BodyID 的原始数值
		//return the raw value of the hit BodyID
		return hitResult.mBodyID.GetIndexAndSequenceNumber();
	}

	// 没击中则返回 Jolt 定义的无效 ID
	//return Jolt's defined invalid ID if no hit
	return JPH::BodyID::cInvalidBodyID;
}









void PhysicsSystem::set_body_transform(uint32_t bodyID, const glm::mat4& transform)
{
	if (!m_physicsSystem || bodyID == JPH::BodyID::cInvalidBodyID) return;

	JPH::BodyInterface& bodyInterface = m_physicsSystem->GetBodyInterface();
	JPH::BodyID joltBodyID(bodyID);

	glm::vec3 scale, translation, skew;
	glm::quat rotation;
	glm::vec4 perspective;
	glm::decompose(transform, scale, rotation, translation, skew, perspective);
	rotation = glm::normalize(rotation);

	// 判断物体状态
	//Determine object state. Static objects must use DontActivate
	JPH::EActivation activation = (bodyInterface.GetMotionType(joltBodyID) == JPH::EMotionType::Static)
		? JPH::EActivation::DontActivate
		: JPH::EActivation::Activate;

	// 将最新的渲染位置同步给刚体
	//Sync the latest render position to the Jolt body
	bodyInterface.SetPositionAndRotation(
		joltBodyID,
		JPH::RVec3(translation.x, translation.y, translation.z),
		toJolt(rotation),
		activation
	);
}
void PhysicsSystem::set_body_scale(uint32_t bodyID, const glm::vec3& newScale, const glm::vec3& pos, const glm::quat& rot) {
	if (!m_physicsSystem || bodyID == JPH::BodyID::cInvalidBodyID) return;

	JPH::BodyInterface& bodyInterface = m_physicsSystem->GetBodyInterface();
	JPH::BodyID joltBodyID(bodyID);

	// 获取当前形状
	// Get the current shape
	JPH::ShapeRefC currentShape = bodyInterface.GetShape(joltBodyID);
	JPH::ShapeRefC baseShape = currentShape;

	if (currentShape->GetSubType() == JPH::EShapeSubType::Scaled) {
		const JPH::ScaledShape* scaledShape = static_cast<const JPH::ScaledShape*>(currentShape.GetPtr());
		baseShape = scaledShape->GetInnerShape();
	}

	// 基于原始 baseShape 创建新的缩放
	// Create a new scaled shape based on the original baseShape
	JPH::ScaledShapeSettings settings(baseShape, JPH::Vec3(newScale.x, newScale.y, newScale.z));
	auto result = settings.Create();

	if (result.IsValid()) {
		JPH::EActivation activation = (bodyInterface.GetMotionType(joltBodyID) == JPH::EMotionType::Static)
			? JPH::EActivation::DontActivate
			: JPH::EActivation::Activate;

		bodyInterface.RemoveBody(joltBodyID);
		bodyInterface.SetShape(joltBodyID, result.Get(), true, JPH::EActivation::DontActivate);

		// 重新加入物理世界时，使用正确的激活状态
		// Re-add to the physics world with the correct activation state
		bodyInterface.AddBody(joltBodyID, activation);

		bodyInterface.SetPositionAndRotation(joltBodyID,
			JPH::RVec3(pos.x, pos.y, pos.z), toJolt(rot), activation);

		m_physicsSystem->OptimizeBroadPhase();
	}
}
//=============================UI System Interactions=============================

//void PhysicsSystem::create_bicycle(uint32_t chassisBodyID) {
//	if (!m_physicsSystem || chassisBodyID == JPH::BodyID::cInvalidBodyID) return;
//
//	m_bicycle = std::make_unique<BicycleState>();
//	m_bicycle->chassisID = JPH::BodyID(chassisBodyID);
//
//
//	JPH::BodyInterface& bi = m_physicsSystem->GetBodyInterface();
//	bi.SetGravityFactor(m_bicycle->chassisID, 1.0f);
//
//	std::cout << "[Bicycle] bicycle created." << std::endl;
//}

} // namespace engine
