#include "PhysicsSystem.hpp"
#include <Jolt/RegisterTypes.h>
#include <Jolt/Core/Factory.h>
#include <Jolt/Physics/PhysicsSettings.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/Collision/Shape/MeshShape.h>
#include <Jolt/Physics/Collision/Shape/ConvexHullShape.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>

#include <glm/gtx/matrix_decompose.hpp>

#include <iostream>
#include <cstdarg>
#include <thread>


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


	if (scale != glm::vec3(1.0f)) {
		JPH::VertexList scaledVertices;
		scaledVertices.reserve(mesh.positions.size());
		for (const auto& pos : mesh.positions) {
			scaledVertices.push_back(JPH::Float3(pos.x * scale.x, pos.y * scale.y, pos.z * scale.z));
		}
		JPH::MeshShapeSettings scaledShapeSettings(scaledVertices, triangles);
		shape = scaledShapeSettings.Create().Get();
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




} // namespace engine
