#include "PhysicsSystem.hpp"
#include <Jolt/RegisterTypes.h>
#include <Jolt/Core/Factory.h>
#include <Jolt/Physics/PhysicsSettings.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/Collision/Shape/MeshShape.h>
#include <Jolt/Physics/Collision/Shape/ConvexHullShape.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Collision/ContactListener.h>
#include <Jolt/Physics/Collision/Shape/StaticCompoundShape.h>
#include "../Event/EventSystem.hpp"
#include "../Event/Event.hpp"
//UI System 射线检测相关
#include <Jolt/Physics/Collision/RayCast.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Collision/Shape/ScaledShape.h>
#include <glm/gtx/matrix_decompose.hpp>

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

	create_bicycle(8388679);
}

void PhysicsSystem::optimize_broad_phase()
{
	if (m_physicsSystem) {
		m_physicsSystem->OptimizeBroadPhase();
	}
}




void PhysicsSystem::AddForceDirection(const JPH::BodyID& bodyID, float force)
{
	if (!mInputSystem || !mState) return;
	JPH::BodyInterface& bi = m_physicsSystem->GetBodyInterface();
	if (!bi.IsAdded(bodyID)) return;

	float yaw = mState->Yaw;
	glm::vec3 forward(-std::sin(yaw), 0.0f, -std::cos(yaw));
	glm::vec3 right = glm::normalize(glm::cross(forward, glm::vec3(0, 1, 0)));

	float inputZ = 0.0f, inputX = 0.0f;
	if (mInputSystem->IsActionHeld("MoveForward"))  inputZ += 1.0f;
	if (mInputSystem->IsActionHeld("MoveBackward")) inputZ -= 1.0f;
	if (mInputSystem->IsActionHeld("StrafeLeft"))   inputX -= 1.0f;
	if (mInputSystem->IsActionHeld("StrafeRight"))  inputX += 1.0f;


        glm::vec3 worldDir = forward * std::max(inputZ, 0.0f) + right * inputX;
	if (glm::length(worldDir) > 0.001f)
	{
		worldDir = glm::normalize(worldDir);


		glm::vec3 forceDir(worldDir.x, 0.0f, worldDir.z);
		JPH::RVec3 bodyPos = bi.GetPosition(bodyID);
		JPH::RRayCast ray(bodyPos, JPH::Vec3(0.0f, -5.0f, 0.0f));
		JPH::RayCastResult hit;

		if (m_physicsSystem->GetNarrowPhaseQuery().CastRay(
			ray, hit,
			JPH::BroadPhaseLayerFilter(),
			JPH::ObjectLayerFilter(),
			JPH::BodyFilter()))
		{
			JPH::BodyLockRead lock(m_physicsSystem->GetBodyLockInterface(), hit.mBodyID);
			if (lock.Succeeded()) {
				JPH::Vec3 normal = lock.GetBody().GetWorldSpaceSurfaceNormal(
					hit.mSubShapeID2, ray.GetPointOnRay(hit.mFraction));
				glm::vec3 n(normal.GetX(), normal.GetY(), normal.GetZ());
				glm::vec3 projected = forceDir - n * glm::dot(forceDir, n);
				if (glm::length(projected) > 0.001f)
					forceDir = glm::normalize(projected);
			}
		}

		bi.AddForce(bodyID, JPH::Vec3(forceDir.x * force, forceDir.y * force, forceDir.z * force ));
	}
	else
	{
		JPH::Vec3 vel = bi.GetLinearVelocity(bodyID);
		bi.AddForce(bodyID, JPH::Vec3(-vel.GetX() * 10.0f, 0.0f, -vel.GetZ() * 10.0f));
	}

	bi.SetAngularVelocity(bodyID, JPH::Vec3::sZero());

	if (glm::length(worldDir) > 0.001f)
	{
		JPH::Quat targetRot = JPH::Quat::sRotation(JPH::Vec3::sAxisY(), yaw + JPH::JPH_PI);
		JPH::Quat currentRot = bi.GetRotation(bodyID);
		JPH::Quat smoothRot = currentRot.SLERP(targetRot, 0.15f);
		bi.SetRotation(bodyID, smoothRot, JPH::EActivation::Activate);
	}

	JPH::Vec3 vel = bi.GetLinearVelocity(bodyID);
	float maxSpeed = 10.0f;
	float hSpeed = std::sqrt(vel.GetX() * vel.GetX() + vel.GetZ() * vel.GetZ());
	if (hSpeed > maxSpeed)
	{
		float scale = maxSpeed / hSpeed;
		bi.SetLinearVelocity(bodyID,
			JPH::Vec3(vel.GetX() * scale, vel.GetY(), vel.GetZ() * scale));
	}
}


void PhysicsSystem::Update(float dt)
{
	if (!m_physicsSystem) {
		return;
	}

	if (mState && mState->thirdPersonMode && m_bicycle) {
		update_bicycle(dt);
	}

	if (mState && mState->thirdPersonMode) {
		//burstlink's old bike
		//AddForceDirection(JPH::BodyID(8388674));

		//AddForceDirection(JPH::BodyID(8388679));
	}

	const int cCollisionSteps = 1;
	// Step the system
	m_physicsSystem->Update(dt, cCollisionSteps, m_tempAllocator.get(), m_jobSystem.get());
}
void PhysicsSystem::Shutdown()
{
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

void PhysicsSystem::create_bicycle(uint32_t chassisBodyID) {
	if (!m_physicsSystem || chassisBodyID == JPH::BodyID::cInvalidBodyID) return;

	m_bicycle = std::make_unique<BicycleState>();
	m_bicycle->chassisID = JPH::BodyID(chassisBodyID);


	JPH::BodyInterface& bi = m_physicsSystem->GetBodyInterface();
	bi.SetGravityFactor(m_bicycle->chassisID, 1.0f);

	std::cout << "[Bicycle] bicycle created." << std::endl;
}

void PhysicsSystem::update_bicycle(float dt) {
	if (!m_bicycle || !mInputSystem || !m_physicsSystem) return;

	JPH::BodyInterface& bi = m_physicsSystem->GetBodyInterface();
	JPH::BodyID id = m_bicycle->chassisID;
	if (!bi.IsAdded(id)) return;

	float inputThrottle = 0.0f;  
	float inputSteer = 0.0f;  

	if (mInputSystem->IsActionHeld("MoveForward"))  inputThrottle += 1.0f;
	if (mInputSystem->IsActionHeld("MoveBackward")) inputThrottle -= 1.0f;
	if (mInputSystem->IsActionHeld("StrafeLeft"))   inputSteer += 1.0f;
	if (mInputSystem->IsActionHeld("StrafeRight"))  inputSteer -= 1.0f;

	//车把
	const float maxSteerAngle = glm::radians(25.0f);
	const float steerSpeed = glm::radians(90.0f); 
	float targetSteer = inputSteer * maxSteerAngle;
	float steerDiff = targetSteer - m_bicycle->steerAngle;
	float maxDelta = steerSpeed * dt;
	m_bicycle->steerAngle += glm::clamp(steerDiff, -maxDelta, maxDelta);


	JPH::Vec3 vel = bi.GetLinearVelocity(id);
	float speed = std::sqrt(vel.GetX() * vel.GetX() + vel.GetZ() * vel.GetZ());
	m_bicycle->currentSpeed = speed;




	//车身倾斜
	const float maxLeanAngle = glm::radians(30.0f);
	const float leanSpeed = glm::radians(90.0f); 
	float maxLeanDelta = leanSpeed * dt;

	float targetLean = 0.0f;
	if (speed > 0.5f) {
		targetLean = -inputSteer * maxLeanAngle;
	}
	else {
		targetLean = 0.0f; 
	}


	float leanDiff = targetLean - m_bicycle->leanAngle;


	m_bicycle->leanAngle += glm::clamp(leanDiff, -maxLeanDelta, maxLeanDelta);



	JPH::Quat currentRot = bi.GetRotation(id);
	JPH::Vec3 fwd = currentRot.RotateAxisZ();
	float currentYaw = std::atan2(-fwd.GetX(), -fwd.GetZ());

	const float wheelBase = 1.6f; 
	float yawRate = 0.0f;
	if (speed > 0.1f) {
		yawRate = (speed * std::tan(m_bicycle->steerAngle)) / wheelBase;
	}
	float newYaw = currentYaw + yawRate * dt;


	JPH::Quat yawQuat = JPH::Quat::sRotation(JPH::Vec3::sAxisY(), newYaw + JPH::JPH_PI);
	JPH::Quat leanQuat = JPH::Quat::sRotation(JPH::Vec3::sAxisZ(), m_bicycle->leanAngle);
	JPH::Quat finalRot = yawQuat * leanQuat;


	bi.SetRotation(id, finalRot, JPH::EActivation::Activate);


	//施加的力的大小
	const float driveForce = 1000.0f;

	//停车力的大小
	const float brakeForce = 20.0f;

	//最大速度限制
	const float maxSpeed = 60.0f;  


	glm::vec3 forwardDir(-std::sin(newYaw), 0.0f, -std::cos(newYaw));

	if (std::abs(inputThrottle) > 0.01f) {

		if (speed < maxSpeed || inputThrottle < 0.0f) {
			bi.AddForce(id, JPH::Vec3(
				forwardDir.x * driveForce * inputThrottle,
				0.0f,
				forwardDir.z * driveForce * inputThrottle
			));
		}
	}
	else {

		bi.AddForce(id, JPH::Vec3(
			-vel.GetX() * brakeForce,
			0.0f,
			-vel.GetZ() * brakeForce
		));
	}


	bi.SetAngularVelocity(id, JPH::Vec3::sZero());


	if (speed > maxSpeed) {
		float scale = maxSpeed / speed;
		bi.SetLinearVelocity(id, JPH::Vec3(
			vel.GetX() * scale, vel.GetY(), vel.GetZ() * scale
		));
	}
}


} // namespace engine
