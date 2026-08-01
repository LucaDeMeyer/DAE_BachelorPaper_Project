#include "AccelerationStructure.h"
#include "GraphicsContext.h"
#include "Object.h"
#include "ResourceManager.h"
#include "Scene.h"
#include <iostream>      
#include <stdexcept>

PFN_vkCreateAccelerationStructureKHR pfn_vkCreateAccelerationStructureKHR = nullptr;
PFN_vkGetAccelerationStructureDeviceAddressKHR pfn_vkGetAccelerationStructureDeviceAddressKHR = nullptr;
PFN_vkGetAccelerationStructureBuildSizesKHR pfn_vkGetAccelerationStructureBuildSizesKHR = nullptr;
PFN_vkCmdBuildAccelerationStructuresKHR pfn_vkCmdBuildAccelerationStructuresKHR = nullptr;

Core::RTAccelerationStructure::RTAccelerationStructure(GraphicsContext& context, ResourceManager& resManager)
	: m_context(context), m_resManager(resManager)
{
	VkDevice device = m_context.getDevice();
	pfn_vkCreateAccelerationStructureKHR = (PFN_vkCreateAccelerationStructureKHR)vkGetDeviceProcAddr(device, "vkCreateAccelerationStructureKHR");
	pfn_vkGetAccelerationStructureDeviceAddressKHR = (PFN_vkGetAccelerationStructureDeviceAddressKHR)vkGetDeviceProcAddr(device, "vkGetAccelerationStructureDeviceAddressKHR");
	pfn_vkGetAccelerationStructureBuildSizesKHR = (PFN_vkGetAccelerationStructureBuildSizesKHR)vkGetDeviceProcAddr(device, "vkGetAccelerationStructureBuildSizesKHR");
	pfn_vkCmdBuildAccelerationStructuresKHR = (PFN_vkCmdBuildAccelerationStructuresKHR)vkGetDeviceProcAddr(device, "vkCmdBuildAccelerationStructuresKHR");

	if (!pfn_vkCreateAccelerationStructureKHR || !pfn_vkGetAccelerationStructureDeviceAddressKHR || !pfn_vkGetAccelerationStructureBuildSizesKHR) {
		throw std::runtime_error("Failed to load Acceleration Structure extension functions! Check device features.");
	}
}


void Core::RTAccelerationStructure::Shutdown()
{
	auto device = m_context.getDevice();
	PFN_vkDestroyAccelerationStructureKHR pfn_vkDestroyAccelerationStructureKHR =
		(PFN_vkDestroyAccelerationStructureKHR)vkGetDeviceProcAddr(device, "vkDestroyAccelerationStructureKHR");

	if (m_tlas.accelerationStructure) {
		pfn_vkDestroyAccelerationStructureKHR(device, m_tlas.accelerationStructure, nullptr);
	}

	for (auto& blas : m_blasList) {
		if (blas.accelerationStructure) {
			pfn_vkDestroyAccelerationStructureKHR(device, blas.accelerationStructure, nullptr);
		}
	}

	m_blasList.clear();

}

VkDeviceAddress Core::RTAccelerationStructure::getBufferDeviceAddress(VkBuffer buffer) const
{
	VkBufferDeviceAddressInfo addressInfo{};
	addressInfo.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
	addressInfo.buffer = buffer;

	return vkGetBufferDeviceAddress(m_context.getDevice(), &addressInfo);
}

Core::BLAS Core::RTAccelerationStructure::createBLAS(VkCommandBuffer cmd, const Mesh* mesh, VkDeviceAddress scratchAddress, bool allowUpdate)
{
	BLAS blas;
	blas.vertexCount = mesh->vertexCount;
	blas.indexCount = mesh->indexCount;

	VkAccelerationStructureGeometryKHR geom;
	VkAccelerationStructureBuildGeometryInfoKHR geomInfo;
	VkAccelerationStructureBuildRangeInfoKHR rangeInfo;

	buildBLASGeometry(mesh, geomInfo, geom, rangeInfo);

	VkAccelerationStructureBuildSizesInfoKHR sizeinfo{ VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR };
	uint32_t primitiveCount = blas.indexCount / 3;
	pfn_vkGetAccelerationStructureBuildSizesKHR(m_context.getDevice(), VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &geomInfo, &primitiveCount, &sizeinfo);

	createBLASBuffer(blas, sizeinfo);
	Core::BufferBuilder::Buffer* blasBuf = m_resManager.GetBuffer(blas.bufferhndl);

	VkAccelerationStructureCreateInfoKHR createInfo{};
	createInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR;
	createInfo.buffer = blasBuf->buffer;
	createInfo.size = sizeinfo.accelerationStructureSize;
	createInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;

	if (pfn_vkCreateAccelerationStructureKHR(m_context.getDevice(), &createInfo, nullptr, &blas.accelerationStructure) != VK_SUCCESS) {
		throw std::runtime_error("Failed to create BLAS acceleration structure!");
	}

	VkAccelerationStructureDeviceAddressInfoKHR addressInfo{};
	addressInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR;
	addressInfo.accelerationStructure = blas.accelerationStructure;
	blas.deviceAddress = pfn_vkGetAccelerationStructureDeviceAddressKHR(m_context.getDevice(), &addressInfo);

	geomInfo.dstAccelerationStructure = blas.accelerationStructure;
	geomInfo.scratchData.deviceAddress = scratchAddress;

	const VkAccelerationStructureBuildRangeInfoKHR* pRangeInfo = &rangeInfo;
	pfn_vkCmdBuildAccelerationStructuresKHR(cmd, 1, &geomInfo, &pRangeInfo);

	return blas;
}

void Core::RTAccelerationStructure::createBLASBuffer(BLAS& blas, const VkAccelerationStructureBuildSizesInfoKHR& sizeInfo)
{
	Core::BufferDesc desc{};
	desc.name = "BLAS_Buffer";
	desc.size = sizeInfo.accelerationStructureSize;
	desc.usage = VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR |
		VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
	desc.vmaUsage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;

	blas.bufferhndl = m_resManager.CreateBuffer(desc);
}

void Core::RTAccelerationStructure::buildBLASGeometry(const Mesh* mesh, VkAccelerationStructureBuildGeometryInfoKHR& buildInfo, VkAccelerationStructureGeometryKHR& geometry, VkAccelerationStructureBuildRangeInfoKHR& rangeInfo)
{
	VkBuffer globalVB = m_resManager.GetGlobalVertexBuffer();
	VkBuffer globalIB = m_resManager.GetGlobalIndexBuffer();

	VkDeviceAddress vertexAddress = getBufferDeviceAddress(globalVB);
	VkDeviceAddress indexAddress = getBufferDeviceAddress(globalIB);

	vertexAddress += mesh->vertexOffset * sizeof(RenderTypes::Vertex);
	indexAddress += mesh->firstIndex * sizeof(uint32_t);

	VkAccelerationStructureGeometryTrianglesDataKHR triangles{};
	triangles.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR;
	triangles.vertexFormat = VK_FORMAT_R32G32B32_SFLOAT;
	triangles.vertexData.deviceAddress = vertexAddress;
	triangles.vertexStride = sizeof(RenderTypes::Vertex);
	triangles.maxVertex = mesh->vertexCount - 1;
	triangles.indexType = VK_INDEX_TYPE_UINT32;
	triangles.indexData.deviceAddress = indexAddress;

	geometry = {};
	geometry.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
	geometry.geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
	geometry.geometry.triangles = triangles;
	geometry.flags = VK_GEOMETRY_OPAQUE_BIT_KHR;

	buildInfo = {};
	buildInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
	buildInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
	buildInfo.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
	buildInfo.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
	buildInfo.geometryCount = 1;
	buildInfo.pGeometries = &geometry;

	rangeInfo = {};
	rangeInfo.primitiveCount = mesh->indexCount / 3;
	rangeInfo.primitiveOffset = 0;
	rangeInfo.firstVertex = 0;
	rangeInfo.transformOffset = 0;

}


void Core::RTAccelerationStructure::buildSceneBLAS(VkCommandBuffer cmd,const Scene* scene)
{
	m_blasList.clear();
	m_meshToBlasIndex.clear();

	VkDeviceSize totalScratchSize = 0;

	VkPhysicalDeviceAccelerationStructurePropertiesKHR asProps{};
	asProps.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_PROPERTIES_KHR;
	VkPhysicalDeviceProperties2 props2{};
	props2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
	props2.pNext = &asProps;
	vkGetPhysicalDeviceProperties2(m_context.getPhysicalDevice(), &props2);

	uint32_t scratchAlignment = asProps.minAccelerationStructureScratchOffsetAlignment;

	std::vector<const Mesh*> uniqueMeshes;
	for (const auto& object : scene->GetObjects()) {
		if (m_meshToBlasIndex.find(object.mesh) == m_meshToBlasIndex.end()) {
			uniqueMeshes.push_back(object.mesh);
			m_meshToBlasIndex[object.mesh] = static_cast<uint32_t>(uniqueMeshes.size() - 1);

			VkAccelerationStructureGeometryKHR geom;
			VkAccelerationStructureBuildGeometryInfoKHR geomInfo;
			VkAccelerationStructureBuildRangeInfoKHR rangeInfo;
			buildBLASGeometry(object.mesh, geomInfo, geom, rangeInfo);

			VkAccelerationStructureBuildSizesInfoKHR sizeinfo{ VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR };
			uint32_t primitiveCount = object.mesh->indexCount / 3;
			pfn_vkGetAccelerationStructureBuildSizesKHR(m_context.getDevice(), VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &geomInfo, &primitiveCount, &sizeinfo);

			totalScratchSize += alignUp(sizeinfo.buildScratchSize, scratchAlignment);
		}
	}

	if (!m_blasScratchBuffer.IsValid() || m_resManager.GetBuffer(m_blasScratchBuffer)->size < totalScratchSize) {
		if (m_blasScratchBuffer.IsValid()) m_resManager.DestroyBuffer(m_blasScratchBuffer);

		Core::BufferDesc scratchDesc{};
		scratchDesc.name = "BLAS_Unified_Scratch";
		scratchDesc.size = totalScratchSize;
		scratchDesc.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
		scratchDesc.vmaUsage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
		m_blasScratchBuffer = m_resManager.CreateBuffer(scratchDesc);
	}

	VkDeviceAddress currentScratchAddress = getBufferDeviceAddress(m_resManager.GetBuffer(m_blasScratchBuffer)->buffer);

	for (const Mesh* mesh : uniqueMeshes) {
		currentScratchAddress = alignUp(currentScratchAddress, scratchAlignment);
		BLAS blas = createBLAS(cmd, mesh, currentScratchAddress, false);
		m_blasList.push_back(std::move(blas));

		VkAccelerationStructureGeometryKHR geom;
		VkAccelerationStructureBuildGeometryInfoKHR geomInfo;
		VkAccelerationStructureBuildRangeInfoKHR rangeInfo;
		buildBLASGeometry(mesh, geomInfo, geom, rangeInfo);

		VkAccelerationStructureBuildSizesInfoKHR sizeinfo{ VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR };
		uint32_t primitiveCount = mesh->indexCount / 3;
		pfn_vkGetAccelerationStructureBuildSizesKHR(m_context.getDevice(), VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &geomInfo, &primitiveCount, &sizeinfo);

		currentScratchAddress += sizeinfo.buildScratchSize;
	}

	VkMemoryBarrier barrier{};
	barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
	barrier.srcAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;
	barrier.dstAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR;

	vkCmdPipelineBarrier(cmd,
		VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
		VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
		0, 1, &barrier, 0, nullptr, 0, nullptr);
}

void Core::RTAccelerationStructure::createTLASBuffer(TLAS& tlas, const VkAccelerationStructureBuildSizesInfoKHR& sizeInfo)
{
	Core::BufferDesc desc{};
	desc.name = "TLAS_Buffer";
	desc.size = sizeInfo.accelerationStructureSize;
	desc.usage = VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR |
		VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
	desc.vmaUsage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;

	tlas.bufferhndl = m_resManager.CreateBuffer(desc);
}

void Core::RTAccelerationStructure::buildTLAS(VkCommandBuffer cmd, const std::vector<RTInstance>& instances, bool allowUpdate)
{
	if (instances.empty()) {
		throw std::runtime_error("Cannot build TLAS with no instances");
	}

	m_tlas.instanceCount = static_cast<uint32_t>(instances.size());
	size_t instanceBufferSize = sizeof(VkAccelerationStructureInstanceKHR) * instances.size();
	size_t alignment = 16;
	size_t alignedBufferSize = instanceBufferSize + alignment;

	Core::BufferDesc instanceDesc{};
	instanceDesc.name = "TLAS_Instance_Buffer";
	instanceDesc.size = alignedBufferSize;
	instanceDesc.usage = VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR |
		VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
	instanceDesc.vmaUsage = VMA_MEMORY_USAGE_AUTO_PREFER_HOST;

	instanceDesc.vmaflags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;

	m_tlas.instanceBufferhndl = m_resManager.CreateBuffer(instanceDesc);
	Core::BufferBuilder::Buffer* instBuf = m_resManager.GetBuffer(m_tlas.instanceBufferhndl);

	VkDeviceAddress instanceAddress = getBufferDeviceAddress(instBuf->buffer);
	VkDeviceAddress alignedInstanceAddress = alignUp(instanceAddress, 16);
	uint64_t addressOffset = alignedInstanceAddress - instanceAddress;

	auto* instanceData = reinterpret_cast<VkAccelerationStructureInstanceKHR*>(static_cast<uint8_t*>(instBuf->mapped) + addressOffset);

	for (size_t i = 0; i < instances.size(); ++i) {
		const auto& inst = instances[i];

		VkTransformMatrixKHR transformMatrix;
		auto transposed = glm::transpose(inst.transform);
		memcpy(&transformMatrix, &transposed, sizeof(float) * 12);

		instanceData[i].transform = transformMatrix;
		instanceData[i].instanceCustomIndex = inst.instanceCustomIndex;
		instanceData[i].mask = inst.mask;
		instanceData[i].instanceShaderBindingTableRecordOffset = inst.instanceShaderBindingTableRecordOffset;
		instanceData[i].flags = inst.flags;
		instanceData[i].accelerationStructureReference = inst.accelerationStructureReference;
	}


	VkAccelerationStructureGeometryInstancesDataKHR instancesData{};
	instancesData.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR;
	instancesData.arrayOfPointers = VK_FALSE;
	instancesData.data.deviceAddress = alignedInstanceAddress;

	VkAccelerationStructureGeometryKHR geometry{};
	geometry.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
	geometry.geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR;
	geometry.geometry.instances = instancesData;
	geometry.flags = VK_GEOMETRY_OPAQUE_BIT_KHR;

	VkAccelerationStructureBuildGeometryInfoKHR buildInfo{};
	buildInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
	buildInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
	buildInfo.flags = allowUpdate ?
		(VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_KHR | VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_BUILD_BIT_KHR) :
		VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
	buildInfo.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
	buildInfo.geometryCount = 1;
	buildInfo.pGeometries = &geometry;

	uint32_t instanceCount = m_tlas.instanceCount;
	VkAccelerationStructureBuildSizesInfoKHR sizeInfo{ VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR };

	pfn_vkGetAccelerationStructureBuildSizesKHR(
		m_context.getDevice(), VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &buildInfo, &instanceCount, &sizeInfo);


	createTLASBuffer(m_tlas, sizeInfo);
	Core::BufferBuilder::Buffer* tlasBuf = m_resManager.GetBuffer(m_tlas.bufferhndl);

	VkAccelerationStructureCreateInfoKHR createInfo{};
	createInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR;
	createInfo.buffer = tlasBuf->buffer;
	createInfo.offset = 0;
	createInfo.size = sizeInfo.accelerationStructureSize;
	createInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;

	if (pfn_vkCreateAccelerationStructureKHR(m_context.getDevice(), &createInfo, nullptr, &m_tlas.accelerationStructure) != VK_SUCCESS) {
		throw std::runtime_error("Failed to create TLAS acceleration structure!");
	}

	VkAccelerationStructureDeviceAddressInfoKHR addressInfo{};
	addressInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR;
	addressInfo.accelerationStructure = m_tlas.accelerationStructure;
	m_tlas.deviceAddress = pfn_vkGetAccelerationStructureDeviceAddressKHR(m_context.getDevice(), &addressInfo);


	VkPhysicalDeviceAccelerationStructurePropertiesKHR asProps{};
	asProps.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_PROPERTIES_KHR;
	VkPhysicalDeviceProperties2 props2{};
	props2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
	props2.pNext = &asProps;
	vkGetPhysicalDeviceProperties2(m_context.getPhysicalDevice(), &props2);

	uint32_t scratchAlignment = asProps.minAccelerationStructureScratchOffsetAlignment;
	VkDeviceSize alignedScratchSize = sizeInfo.buildScratchSize + scratchAlignment;

	Core::BufferBuilder::Buffer* scratchBuf = m_resManager.GetBuffer(m_tlasScratchBuffer);
	if (!m_tlasScratchBuffer.IsValid() || scratchBuf->size < alignedScratchSize) {
		if (m_tlasScratchBuffer.IsValid()) {
			m_resManager.DestroyBuffer(m_tlasScratchBuffer);
		}
		Core::BufferDesc scratchDesc{};
		scratchDesc.name = "TLAS_Scratch_Buffer";
		scratchDesc.size = alignedScratchSize;
		scratchDesc.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
		scratchDesc.vmaUsage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;

		m_tlasScratchBuffer = m_resManager.CreateBuffer(scratchDesc);
		scratchBuf = m_resManager.GetBuffer(m_tlasScratchBuffer);
	}

	VkDeviceAddress scratchAddress = getBufferDeviceAddress(scratchBuf->buffer);
	scratchAddress = alignUp(scratchAddress, scratchAlignment);

	buildInfo.dstAccelerationStructure = m_tlas.accelerationStructure;
	buildInfo.scratchData.deviceAddress = scratchAddress;

	VkAccelerationStructureBuildRangeInfoKHR rangeInfo{};
	rangeInfo.primitiveCount = instanceCount;

	m_tlas.instanceBaseAddress = instanceAddress;
	m_tlas.instanceAlignedAddress = alignedInstanceAddress;
	m_tlas.instanceAddressOffset = addressOffset;

	const VkAccelerationStructureBuildRangeInfoKHR* pRangeInfo = &rangeInfo;
	pfn_vkCmdBuildAccelerationStructuresKHR(cmd, 1, &buildInfo, &pRangeInfo);

}
void Core::RTAccelerationStructure::updateTLAS(VkCommandBuffer cmd, const std::vector<RTInstance>& instances)
{
	Core::BufferBuilder::Buffer* instBuf = m_resManager.GetBuffer(m_tlas.instanceBufferhndl);

	auto* instanceData = reinterpret_cast<VkAccelerationStructureInstanceKHR*>(
		static_cast<uint8_t*>(instBuf->mapped) + m_tlas.instanceAddressOffset);

	for (size_t i = 0; i < instances.size(); ++i) {
		VkTransformMatrixKHR transformMatrix;
		auto transposed = glm::transpose(instances[i].transform);
		memcpy(&transformMatrix, &transposed, sizeof(float) * 12);
		instanceData[i].transform = transformMatrix;
	}

	VkAccelerationStructureGeometryInstancesDataKHR instancesData{};
	instancesData.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR;
	instancesData.arrayOfPointers = VK_FALSE;
	instancesData.data.deviceAddress = m_tlas.instanceAlignedAddress;

	VkAccelerationStructureGeometryKHR geometry{};
	geometry.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
	geometry.geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR;
	geometry.geometry.instances = instancesData;
	geometry.flags = VK_GEOMETRY_OPAQUE_BIT_KHR;

	Core::BufferBuilder::Buffer* scratchBuf = m_resManager.GetBuffer(m_tlasScratchBuffer);

	VkAccelerationStructureBuildGeometryInfoKHR buildInfo{};
	buildInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
	buildInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
	buildInfo.flags = VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_KHR;
	buildInfo.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_UPDATE_KHR;
	buildInfo.srcAccelerationStructure = m_tlas.accelerationStructure;
	buildInfo.dstAccelerationStructure = m_tlas.accelerationStructure;
	buildInfo.geometryCount = 1;
	buildInfo.pGeometries = &geometry;
	buildInfo.scratchData.deviceAddress = getBufferDeviceAddress(scratchBuf->buffer);

	VkAccelerationStructureBuildRangeInfoKHR rangeInfo{};
	rangeInfo.primitiveCount = m_tlas.instanceCount;

	const VkAccelerationStructureBuildRangeInfoKHR* pRangeInfo = &rangeInfo;
	pfn_vkCmdBuildAccelerationStructuresKHR(cmd, 1, &buildInfo, &pRangeInfo);
}


std::vector<Core::RTInstance> Core::RTAccelerationStructure::createInstancesFromScene(const Scene* scene)
{
	std::vector<RTInstance> instances;
	const auto& objects = scene->GetObjects();
	instances.reserve(objects.size());

	for (size_t i = 0; i < objects.size(); ++i) {
		const auto& object = objects[i];

		auto it = m_meshToBlasIndex.find(object.mesh);
		if (it == m_meshToBlasIndex.end()) continue;

		uint32_t blasIndex = it->second;
		VkDeviceAddress address = m_blasList[blasIndex].deviceAddress;

		RTInstance instance{};
		instance.transform = object.transform;
		instance.instanceCustomIndex = object.mesh->bindlessID;
		instance.mask = 0xFF;
		instance.instanceShaderBindingTableRecordOffset = 0;
		instance.flags = VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR;
		instance.accelerationStructureReference = address;

		instances.push_back(instance);
	}
	return instances;
}