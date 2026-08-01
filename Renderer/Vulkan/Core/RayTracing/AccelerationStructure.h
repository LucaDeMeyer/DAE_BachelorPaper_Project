#ifndef ACCELERATION_STRUCTURE_H
#define ACCELERATION_STRUCTURE_H
#include <vulkan/vulkan.h>
#include <glm/glm.hpp>
#include <vector>
#include <memory>
#include <unordered_map>
#include "Vulkan/Core/Buffer.h"
#include "Vulkan/Core/ResourceTypes.h"

namespace Core
{

	struct GraphicsContext;
	class ResourceManager;
	struct Mesh;
	class Scene;
}
namespace Core::RT
{

	struct BLAS
	{
		VkAccelerationStructureKHR accelerationStructure = nullptr;
		Core::BufferHandle bufferhndl;
		VkDeviceAddress deviceAddress = 0;

		uint32_t vertexCount = 0;
		uint32_t indexCount = 0;

		BLAS() = default;
		BLAS(BLAS&&) = default;
		BLAS& operator=(BLAS&&) = default;

		// Delete copy
		BLAS(const BLAS&) = delete;
		BLAS& operator=(const BLAS&) = delete;

	};

	struct TLAS {
		VkAccelerationStructureKHR accelerationStructure = nullptr;

		Core::BufferHandle bufferhndl;
		Core::BufferHandle instanceBufferhndl;

		VkDeviceAddress deviceAddress = 0;
		VkDeviceAddress instanceBaseAddress = 0;
		VkDeviceAddress instanceAlignedAddress = 0;
		uint64_t instanceAddressOffset = 0;
		uint32_t instanceCount = 0;
	};

	struct RTInstance {
		glm::mat4 transform;
		uint32_t instanceCustomIndex;
		uint32_t mask;
		uint32_t instanceShaderBindingTableRecordOffset;
		VkGeometryInstanceFlagsKHR flags;
		uint64_t accelerationStructureReference;
	};

	class RTAccelerationStructure
	{
	public:
		RTAccelerationStructure(GraphicsContext& context, ResourceManager& resManager);
		~RTAccelerationStructure() = default;

		void Shutdown();

		BLAS createBLAS(VkCommandBuffer cmd, const Mesh* mesh, VkDeviceAddress scratchAddress, bool allowUpdate = false);
		void buildSceneBLAS(VkCommandBuffer cmd, const Scene* scene);

		void buildTLAS(VkCommandBuffer cmd, const std::vector<RTInstance>& instances, bool allowUpdate = false);
		void updateTLAS(VkCommandBuffer cmd, const std::vector<RTInstance>& instances);

		const TLAS& getTLAS() const { return m_tlas; }
		const std::vector<BLAS>& getBLASList() const { return m_blasList; }
		VkAccelerationStructureKHR getTLASHandle() const { return m_tlas.accelerationStructure; }

		std::vector<RTInstance> createInstancesFromScene(const Scene* scene);

	private:
		GraphicsContext& m_context;
		ResourceManager& m_resManager;

		std::vector<BLAS> m_blasList;
		std::unordered_map<const Mesh*, uint32_t> m_meshToBlasIndex;
		TLAS m_tlas;

		BufferHandle m_blasScratchBuffer;
		BufferHandle m_tlasScratchBuffer;

		VkDeviceAddress getBufferDeviceAddress(VkBuffer buffer) const;

		void createBLASBuffer(BLAS& blas, const VkAccelerationStructureBuildSizesInfoKHR& sizeInfo);
		void createTLASBuffer(TLAS& tlas, const VkAccelerationStructureBuildSizesInfoKHR& sizeInfo);

		void buildBLASGeometry(
			const Mesh* mesh,
			VkAccelerationStructureBuildGeometryInfoKHR& buildInfo,
			VkAccelerationStructureGeometryKHR& geometry,
			VkAccelerationStructureBuildRangeInfoKHR& rangeInfo
		);



		VkDeviceAddress alignUp(VkDeviceAddress address, uint32_t alignment) const {
			return (address + alignment - 1) & ~(static_cast<VkDeviceAddress>(alignment) - 1);
		}
	};

	class RTAccelerationStructureBuilder {
	public:
		RTAccelerationStructureBuilder(GraphicsContext& context, ResourceManager& resManager)
			: m_context(context), m_resManager(resManager), m_as(std::make_unique<RTAccelerationStructure>(context, resManager)) {
		}

		RTAccelerationStructureBuilder& buildFromScene(VkCommandBuffer cmd, const Scene* scene) {
			m_as->buildSceneBLAS(cmd, scene);
			auto instances = m_as->createInstancesFromScene(scene);
			m_as->buildTLAS(cmd, instances, true);
			return *this;
		}

		RTAccelerationStructureBuilder& allowTLASUpdates(bool allow) {
			m_allowTLASUpdates = allow;
			return *this;
		}

		std::unique_ptr<RTAccelerationStructure> build() {
			return std::move(m_as);
		}

	private:
		GraphicsContext& m_context;
		ResourceManager& m_resManager;
		std::unique_ptr<RTAccelerationStructure> m_as;
		bool m_allowTLASUpdates = true;
	};

}

#endif 