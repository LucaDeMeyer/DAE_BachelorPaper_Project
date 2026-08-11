#include "RenderGraph.h"
#include "Pass.h"
#include "Vulkan/Core/ResourceManager.h" 
#include <algorithm>
#include <cassert>
#include <queue>

#include "Vulkan/Core/GraphicsContext.h"


namespace Render::Graph
{

    RGHandle RenderGraphBuilder::CreateTexture(const Core::TextureDesc& desc, bool isTransient) const {
        RGHandle handle{ static_cast<uint32_t>(m_parentGraph->m_physicalResources.size()) };

        RGPhysicalResource res{};
        res.textureDesc = desc;
        res.isBuffer = false;
        res.isTransient = isTransient; 
        res.isImported = false;

        m_parentGraph->m_physicalResources.push_back(res);
        return handle;
    }

    RGHandle RenderGraphBuilder::CreateBuffer(const Core::BufferDesc& desc) const {
        RGHandle handle{ static_cast<uint32_t>(m_parentGraph->m_physicalResources.size()) };

        RGPhysicalResource res{};
        res.bufferDesc = desc;
        res.isBuffer = true;
        res.isTransient = true;
        res.isImported = false;

        m_parentGraph->m_physicalResources.push_back(res);
        return handle;
    }

    RGHandle RenderGraphBuilder::RegisterImportedBuffer(VkBuffer buffer, const Core::BufferDesc& desc) const
    {
    	return m_parentGraph->RegisterImportedBuffer(buffer, desc);
        
    }

    void RenderGraphBuilder::AddDependency(RGHandle handle, AccessType access) {
        _dependencies.push_back({ handle, access });
    }

    void RenderGraph::Execute(const RenderTypes::RenderContext& context) {


        uint32_t frameQueryOffset = context.currentFrameIndex * m_maxQueriesPerFrame;
        uint32_t queriesNeededThisFrame = static_cast<uint32_t>(m_compiledPasses.size()) * 2;


        if (queriesNeededThisFrame > 0 && m_frameQueriesValid[context.currentFrameIndex]) {
            std::vector<uint64_t> timeStamps(queriesNeededThisFrame);
            VkResult res = vkGetQueryPoolResults(
                context.resourceManager->GetContext().getDevice(),
                m_queryPool,
                frameQueryOffset,
                queriesNeededThisFrame,
                timeStamps.size() * sizeof(uint64_t),
                timeStamps.data(),
                sizeof(uint64_t),
                VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT
            );

            if (res == VK_SUCCESS) {
                for (size_t i = 0; i < m_compiledPasses.size(); ++i) {
                    uint64_t start = timeStamps[i * 2];
                    uint64_t end = timeStamps[i * 2 + 1];

                    uint32_t originalPassIdx = m_passOrder[i]; // Get the un-sorted ID
                    if (m_gpuTimes.size() <= originalPassIdx) m_gpuTimes.resize(originalPassIdx + 1, 0.0f);

                    m_gpuTimes[originalPassIdx] = float(end - start) * m_timestampPeriod / 1000000.0f;
                }
            }
        }

        vkCmdResetQueryPool(context.cmd, m_queryPool, frameQueryOffset, queriesNeededThisFrame);

        for (uint32_t i = 0; i < m_compiledPasses.size(); ++i) {

            const CompiledPass& cp = m_compiledPasses[i];
            uint32_t queryStartIdx = frameQueryOffset + (i * 2);
            uint32_t queryEndIdx = queryStartIdx + 1;
            uint32_t originalPassIdx = m_passOrder[i];

            if (ext_vkCmdBeginDebugUtilsLabelEXT) {
                VkDebugUtilsLabelEXT labelInfo{ VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT };
                
                labelInfo.pLabelName = cp.pass->GetName().c_str();
                labelInfo.color[0] = 0.2f; // R
                labelInfo.color[1] = 0.6f; // G
                labelInfo.color[2] = 1.0f; // B
                labelInfo.color[3] = 1.0f; // A

                ext_vkCmdBeginDebugUtilsLabelEXT(context.cmd, &labelInfo);
            }

            if (!cp.imageBarriers.empty() || !cp.bufferBarriers.empty()) {
                VkDependencyInfo depInfo{ VK_STRUCTURE_TYPE_DEPENDENCY_INFO };
                depInfo.imageMemoryBarrierCount = (uint32_t)cp.imageBarriers.size();
                depInfo.pImageMemoryBarriers = cp.imageBarriers.data();
                depInfo.bufferMemoryBarrierCount = (uint32_t)cp.bufferBarriers.size();
                depInfo.pBufferMemoryBarriers = cp.bufferBarriers.data();

                vkCmdPipelineBarrier2(context.cmd, &depInfo);
            }

            vkCmdWriteTimestamp(context.cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, m_queryPool, queryStartIdx);
            auto cpuStart = std::chrono::high_resolution_clock::now();

            cp.pass->Execute(context,*this);

            auto cpuEnd = std::chrono::high_resolution_clock::now();
            vkCmdWriteTimestamp(context.cmd, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, m_queryPool, queryEndIdx);

            float cpuTimeMs = std::chrono::duration<float, std::milli>(cpuEnd - cpuStart).count();
            if (m_cpuTimes.size() <= originalPassIdx) m_cpuTimes.resize(originalPassIdx + 1, 0.0f);
            m_cpuTimes[originalPassIdx] = cpuTimeMs;

            if (ext_vkCmdEndDebugUtilsLabelEXT) {
                ext_vkCmdEndDebugUtilsLabelEXT(context.cmd);
            }
        }

        if (queriesNeededThisFrame > 0) {
            m_frameQueriesValid[context.currentFrameIndex] = true;
        }
    }
    RGHandle RenderGraphBuilder::FindResource(const std::string& name) const {
        for (uint32_t i = 0; i < m_parentGraph->m_physicalResources.size(); ++i) {
            if (m_parentGraph->m_physicalResources[i].textureDesc.name == name) {
                return RGHandle{ i };
            }
        }
        return RGHandle{}; 
    }

    void Render::Graph::RenderGraph::Compile(Core::ResourceManager& resourceManager)
    {
        const uint32_t passCount = static_cast<uint32_t>(m_passes.size());

        // Data structures to track how resources are accessed to build the Directed Acyclic Graph (DAG)
        std::vector<std::vector<std::pair<uint32_t, AccessType>>> resourceTouchList(passCount);
        std::vector<std::unordered_set<uint32_t>> adj(passCount); // Adjacency list for pass dependencies
        std::vector<uint32_t> indeg(passCount, 0);

        // 1. Ask every pass what resources they need (Setup phase)
        setupPassesAndRecordDependencies(resourceTouchList);

        // Ensure all physical resources were actually created during Setup
        for (const auto& res : m_physicalResources) {
            if (!res.isImported && res.textureDesc.format == VK_FORMAT_UNDEFINED && !res.isBuffer) {
                assert(false && "RenderGraph: Resource declared but never created!");
            }
        }
        resourceTouchList.resize(m_physicalResources.size());

        // 2. Track when resources are first created and last used (for aliasing/memory reuse)
        computeResourceLifetimes(resourceTouchList);

        // 3. Build the Dependency Graph based on Write/Read access
        buildAdjacencyGraph(resourceTouchList, adj, indeg);

        for (uint32_t src = 0; src < adj.size(); ++src) {
            for (uint32_t dst : adj[src]) {
                std::cerr << m_passes[src]->GetName() << "(" << src << ") -> "
                    << m_passes[dst]->GetName() << "(" << dst << ")\n";
            }
        }

        // 4. Cull Passes: If a pass writes to a texture that is never read by the Swapchain or a side-effect pass, cull it!
        std::vector<bool> needed = CullPasses(resourceTouchList);

        // 5. Topological Sort: Order the passes logically so dependencies are resolved before execution
        buildTopologicalOrder(needed, adj, indeg);

        // 6. Memory Allocation: Now that we know the order, allocate actual Vulkan memory for transient resources.
        //    Memory is reused if a resource's lifetime has expired!
        AllocatePhysicalResources(resourceManager, resourceTouchList, needed);

        for (uint32_t r = 0; r < m_physicalResources.size(); ++r) {
            auto& res = m_physicalResources[r];
            if (!res.isTransient && !res.isImported) {
                // If it doesn't have a physical texture yet, create it permanently!
                if (!res.physicalTexture.IsValid() && !res.isBuffer) {
                    res.physicalTexture = resourceManager.CreateTexture(res.textureDesc);
                }
                else if (!res.physicalBuffer.IsValid() && res.isBuffer) {
                    res.physicalBuffer = resourceManager.CreateBuffer(res.bufferDesc);
                }
            }
        }
        // 7. Barrier Generation: Automatically insert VkImageMemoryBarrier2 transitions between passes 
        //    based on their declared AccessTypes.
        buildBarriers(resourceManager, resourceTouchList, needed);

    
    }


    void RenderGraph::setupPassesAndRecordDependencies(std::vector<std::vector<std::pair<uint32_t, AccessType>>>& resourceTouchList)
    {
        const auto passCount = static_cast<uint32_t>(m_passes.size());
        m_passDependencies.assign(passCount, {});
        resourceTouchList.clear();

        // We iterate through every registered pass and call its virtual Setup() function.
        // During Setup(), the pass uses the Builder to declare textures/buffers it intends to create or use.
        // We record every "touch" (e.g., Pass 0 wants to Write to Resource A, Pass 1 wants to Read Resource A)
        // so we can build our dependency graph later.
        for (uint32_t p = 0; p < passCount; ++p) {
            auto& pass = m_passes[p];

            RenderGraphBuilder builder(this, p);
            pass->Setup(builder);

            for (auto& [handle, accessType] : builder._dependencies) {
                if (!handle.IsValid()) continue;

                uint32_t rid = handle.id;
                if (rid >= resourceTouchList.size()) {
                    resourceTouchList.resize(rid + 1);
                }

                // Record that Pass 'p' accesses Resource 'rid' with 'accessType'
                resourceTouchList[rid].emplace_back(p, accessType);
                m_passDependencies[p].push_back({ handle, accessType });
            }
        }
    }

    void RenderGraph::computeResourceLifetimes(const std::vector<std::vector<std::pair<uint32_t, AccessType>>>& resourceTouchList)
    {
        const uint32_t resCount = static_cast<uint32_t>(m_physicalResources.size());

        m_resourceFirstUse.assign(resCount, UINT32_MAX);
        m_resourceLastUse.assign(resCount, 0);

        uint32_t listSize = static_cast<uint32_t>(resourceTouchList.size());
        uint32_t iterCount = std::min(resCount, listSize);

        // To reuse GPU memory, we need to know exactly when a resource is "born" and when it "dies".
        // We look at all the passes that touch a specific resource. 
        // The first pass to touch it is its birth (firstUse). The last pass to touch it is its death (lastUse).
        for (uint32_t r = 0; r < iterCount; ++r) {
            if (resourceTouchList[r].empty()) continue;

            uint32_t first = UINT32_MAX;
            uint32_t last = 0;

            for (const auto& pr : resourceTouchList[r]) {
                first = std::min(first, pr.first);
                last = std::max(last, pr.first);
            }

            m_resourceFirstUse[r] = first;
            m_resourceLastUse[r] = last;
        }
    }

    void RenderGraph::buildAdjacencyGraph(const std::vector<std::vector<std::pair<uint32_t, AccessType>>>& resourceTouchList, std::vector<std::unordered_set<uint32_t>>& adj, std::vector<uint32_t>& indeg)
    {
        const uint32_t resCount = static_cast<uint32_t>(m_physicalResources.size());
        if (resCount > resourceTouchList.size()) return;
        // We evaluate every single resource to see which passes interact with it.
        // If Pass A and Pass B both touch the same texture, we likely need to force an execution order.
        for (uint32_t r = 0; r < resCount; ++r) {
            auto touches = resourceTouchList[r];

            // If a resource is only touched by 0 or 1 passes, it cannot cause a sync dependency between passes.
            if (touches.size() < 2) continue;

            // Sort the touches by the order the passes were originally declared in C++
            std::ranges::sort(touches, [](auto& a, auto& b) { return a.first < b.first; });

            const bool isTransient = m_physicalResources[r].isTransient && !m_physicalResources[r].isImported;

            // Compare every interaction against every other interaction for this resource
            for (size_t i = 0; i < touches.size(); ++i) {
                for (size_t j = i + 1; j < touches.size(); ++j) {
                    uint32_t passA_idx = touches[i].first;
                    AccessType accessA = touches[i].second;

                    uint32_t passB_idx = touches[j].first;
                    AccessType accessB = touches[j].second;

                    bool aWrites = IsWriteAccess(accessA);
                    bool bWrites = IsWriteAccess(accessB);

                    // Read-After-Read (RAR) is totally safe. If Pass A and Pass B both just want to 
                    // read the texture, they can execute simultaneously. No dependency is created.
                    if (!aWrites && !bWrites) continue;

                    // By default, the pass declared first (A) must execute before the pass declared second (B)
                    // This handles Write-After-Write (WAW), Read-After-Write (RAW), and Write-After-Read (WAR)
                    uint32_t src = passA_idx;
                    uint32_t dst = passB_idx;

                    // Transient resources only exist for a single frame and start with garbage data.
                    // If Pass A wants to READ the texture, but Pass B is the one WRITING to it, 
                    // Pass B MUST execute first to initialize the data, even if Pass A was declared first in C++.
                    // We flip the dependency graph edge so the Writer always executes before the Reader.
                    if (isTransient) {
                        if (!aWrites && bWrites) {
                            src = passB_idx;
                            dst = passA_idx;
                        }
                    }

                    // Add the directed edge to our Adjacency List (src -> dst).
                    // If this is a new dependency we haven't tracked yet, increment the In-Degree of the destination.
                    // (An In-Degree of 3 means the pass is waiting on 3 other passes to finish).
                    if (adj[src].insert(dst).second) {
                        indeg[dst]++;
                    }
                }
            }
        }
    }

    std::vector<bool> RenderGraph::CullPasses(const std::vector<std::vector<std::pair<uint32_t, AccessType>>>& resourceTouchList)
    {
        const auto passCount = static_cast<uint32_t>(m_passes.size());
        std::vector<bool> needed(passCount, false);
        std::vector<std::unordered_set<uint32_t>> dependencies(passCount);
        std::queue<uint32_t> q;

        const auto resCount = static_cast<uint32_t>(m_physicalResources.size());

        // Look at every resource and figure out which pass wrote to it, and which pass reads from it.
    	// If Pass B reads a texture that Pass A wrote to, Pass B depends on Pass A.
        for (uint32_t r = 0; r < resCount; ++r) {
            auto touches = resourceTouchList[r];
            if (touches.size() < 2) continue;

            std::ranges::sort(touches, [](auto& a, auto& b) { return a.first < b.first; });

            int32_t lastWriter = -1;

            for (const auto& [passIdx, access] : touches) {
                const bool isWrite = IsWriteAccess(access);
                const bool isRead = IsReadAccess(access);

                if (isRead && lastWriter != -1 && lastWriter != static_cast<int32_t>(passIdx)) {
                    dependencies[passIdx].insert(lastWriter);
                }

                if (isWrite) {
                    lastWriter = static_cast<int32_t>(passIdx);
                }
            }
        }
        for (uint32_t r = 0; r < resCount; ++r) {
            const auto& res = m_physicalResources[r];
            std::string name = res.isBuffer ? res.bufferDesc.name : res.textureDesc.name;
            if (name.find("SVGF") == std::string::npos) continue;

            std::cerr << "Resource '" << name << "' (id=" << r << ") touches:\n";
            for (const auto& [passIdx, access] : resourceTouchList[r]) {
                std::cerr << "  pass " << m_passes[passIdx]->GetName()
                    << " (" << passIdx << ") access=" << static_cast<int>(access)
                    << " isWrite=" << IsWriteAccess(access)
                    << " isRead=" << IsReadAccess(access) << "\n";
            }
        }
        // Find the passes that absolutely must execute (e.g., writing to the Swapchain screen, 
		// or specifically flagged as having a Side Effect). These are our starting "Roots".
        for (uint32_t p = 0; p < passCount; ++p) {
            bool isRoot = false;

            if (m_passes[p]->HasSideEffect()) {
                isRoot = true;
            }
            else {
                for (const auto& dep : m_passDependencies[p]) {
                    if (!dep.resource.IsValid()) continue;

                    uint32_t resId = dep.resource.id;
                    const auto& res = m_physicalResources[resId];
                    // If a pass writes to an imported resource (like the Backbuffer), it cannot be culled.
                    if (!res.isTransient || res.isImported) {
                        if (IsWriteAccess(dep.access)) {
                            isRoot = true;
                            break;
                        }
                    }
                }
            }

            if (isRoot) {
                if (!needed[p]) {
                    needed[p] = true;
                    q.push(p);
                }
            }
        }

        // Start from the roots and work backwards through the dependency graph. 
		// If a Root needs a texture from Pass B, Pass B becomes needed. If Pass B needs Pass A, Pass A becomes needed.
		// Anything not reached by this queue is "Dead" and will be culled!
        while (!q.empty()) {
            uint32_t u = q.front();
            q.pop();

            for (uint32_t producer : dependencies[u]) {
                if (!needed[producer]) {
                    needed[producer] = true;
                    q.push(producer);
                }
            }
        }

        for (uint32_t p = 0; p < passCount; ++p) {
            std::cerr << m_passes[p]->GetName() << " needed=" << needed[p] << "\n";
        }

        return needed;
    }

    void RenderGraph::buildTopologicalOrder(
        const std::vector<bool>& needed,
        const std::vector<std::unordered_set<uint32_t>>& adj,
        std::vector<uint32_t>& indeg)
    {
        const uint32_t passCount = static_cast<uint32_t>(m_passes.size());
        std::queue<uint32_t> q;

        //Khan's Algorithm
        // We look for any pass that is 'needed' but has an in-degree of 0.
        // An in-degree of 0 means this pass does not depend on the output of any other pass.
        // These passes are safe to execute first, so we push them into our execution queue.
        for (uint32_t p = 0; p < passCount; ++p) {
            if (needed[p] && indeg[p] == 0) {
                q.push(p);
            }
        }

        std::vector<uint32_t> passOrder;

        // We process passes one by one from the queue. 
        // When a pass is "executed" (added to our final passOrder), we look at all the passes 
        // that were waiting for it (its neighbors in the adjacency list) and reduce their dependency count.
        while (!q.empty()) {
            uint32_t u = q.front();
            q.pop();

            passOrder.push_back(u);

            for (uint32_t v : adj[u]) {
                if (needed[v]) {
                    indeg[v]--; // One less dependency waiting to be fulfilled

                    // If a pass has no more remaining dependencies, it is now safe to execute!
                    if (indeg[v] == 0) {
                        q.push(v);
                    }
                }
            }
        }
        // If we finished resolving dependencies but our final execution list doesn't contain 
        // every pass we flagged as "needed", it means two passes are stuck waiting for each other!
        // (e.g., Pass A needs Pass B's output, but Pass B needs Pass A's output).
        size_t neededCount = std::ranges::count(needed, true);
        if (passOrder.size() != neededCount) {
            std::unordered_set<uint32_t> resolved(passOrder.begin(), passOrder.end());
            for (uint32_t p = 0; p < needed.size(); ++p) {
                if (needed[p] && !resolved.count(p)) {
                    std::cerr << "Stuck pass: " << m_passes[p]->GetName()
                        << " (indeg=" << indeg[p] << ")\n";
                }
            }
        }
        assert(passOrder.size() == neededCount && "Cycle detected in Render Graph passes!");

        // Store the exact execution order and build a compiled list so the 
        // Execute() function can just loop through them blindly.
        m_passOrder = passOrder;

        m_compiledPasses.clear();
        m_compiledPasses.reserve(passOrder.size());

        for (uint32_t passIdx : passOrder) {
            CompiledPass cp;
            cp.pass = m_passes[passIdx].get();
            m_compiledPasses.push_back(cp);
        }
    }

    void RenderGraph::AllocatePhysicalResources(
        Core::ResourceManager& resourceManager,
        const std::vector<std::vector<std::pair<uint32_t, AccessType>>>& resourceTouchList,
        const std::vector<bool>& needed)
    {
        const uint32_t totalResCount = static_cast<uint32_t>(m_physicalResources.size());
        // Map pass indices to their topological execution order
        std::unordered_map<uint32_t, uint32_t> passToExecOrder;
        for (uint32_t i = 0; i < m_passOrder.size(); ++i) {
            passToExecOrder[m_passOrder[i]] = i;
        }

        std::vector<uint32_t> execFirstUse(totalResCount, UINT32_MAX);
        std::vector<uint32_t> execLastUse(totalResCount, 0);
        std::vector<bool> resIsNeeded(totalResCount, false);
        // Calculate the exact start and end "time" (execution index) for every transient resource
        for (uint32_t r = 0; r < totalResCount; ++r) {
            if (!m_physicalResources[r].isTransient || m_physicalResources[r].isImported) continue;
            if (r >= resourceTouchList.size()) continue;

            for (auto& [passIdx, accessType] : resourceTouchList[r]) {
                if (!needed[passIdx]) continue;

                auto it = passToExecOrder.find(passIdx);
                if (it == passToExecOrder.end()) continue;

                uint32_t execIdx = it->second;
                execFirstUse[r] = std::min(execFirstUse[r], execIdx);
                execLastUse[r] = std::max(execLastUse[r], execIdx);
                resIsNeeded[r] = true;
            }
        }

        struct ResAlloc {
            uint32_t resIdx;
            uint32_t firstUse;
            uint32_t lastUse;
        };
        std::vector<ResAlloc> toAllocate;

        for (uint32_t r = 0; r < totalResCount; ++r) {
            if (m_physicalResources[r].isTransient && resIsNeeded[r]) {
                toAllocate.push_back({ r, execFirstUse[r], execLastUse[r] });
            }
        }
        // Sort resources by when they are first needed in the frame
        std::ranges::sort(toAllocate, [](const ResAlloc& a, const ResAlloc& b) {
            return a.firstUse < b.firstUse;
            });

        struct ActiveResource {
            uint32_t endTime;
            uint32_t resIdx;
        };
        // Min-heap tracking when currently allocated textures will expire
        auto cmp = [](const ActiveResource& a, const ActiveResource& b) { return a.endTime > b.endTime; };
        std::priority_queue<ActiveResource, std::vector<ActiveResource>, decltype(cmp)> activeTextures(cmp);

        // As we step through the timeline, if an active texture's `lastUse` has passed, 
		// we release it back to the ResourceManager. The very next texture we allocate will instantly
		// recycle that memory, keeping the overall VRAM footprint of the frame incredibly low.
        for (const auto& alloc : toAllocate) {
            auto& res = m_physicalResources[alloc.resIdx];
            // Free up memory for any textures that died before our current 'firstUse' time
            while (!activeTextures.empty() && activeTextures.top().endTime < alloc.firstUse) {
                auto expired = activeTextures.top();
                auto& expiredRes = m_physicalResources[expired.resIdx];

                if (!expiredRes.isBuffer) {
                    resourceManager.ReleaseTransientTexture(expiredRes.physicalTexture, expiredRes.textureDesc);
                }
                else {
                    resourceManager.ReleaseTransientBuffer(expiredRes.physicalBuffer, expiredRes.bufferDesc);
                }
                activeTextures.pop();
            }
            // Allocate the current resource (which will likely reuse the memory we just freed)
            if (!res.isBuffer) {
                res.physicalTexture = resourceManager.AcquireTransientTexture(res.textureDesc);
            }
            else {
                res.physicalBuffer = resourceManager.AcquireTransientBuffer(res.bufferDesc);
            }
            // Push it into the queue to track when it will eventually die
            activeTextures.push({ alloc.lastUse, alloc.resIdx });
        }
    }

    void RenderGraph::buildBarriers(
        Core::ResourceManager& resourceManager,
        const std::vector<std::vector<std::pair<uint32_t, AccessType>>>& resourceTouchList,
        const std::vector<bool>& needed)
    {
        m_barriersPerPass.assign(m_passOrder.size(), {});
        const auto resCount = static_cast<uint32_t>(m_physicalResources.size());

        std::unordered_map<uint32_t, uint32_t> passIndexToOrderIndex;
        passIndexToOrderIndex.reserve(m_passOrder.size());
        for (uint32_t i = 0; i < m_passOrder.size(); ++i) {
            passIndexToOrderIndex[m_passOrder[i]] = i;
        }

        // Writing manual pipeline barriers in Vulkan is highly error-prone.
        // Here, we look at the timeline of a single resource across all active passes.
        // We compare what state it was in last (e.g., COLOR_ATTACHMENT) with what it needs 
        // to be next (e.g., SHADER_READ_ONLY) and automatically generate the optimal barrier.
        auto ProcessTimeline = [&](uint32_t resIdx) {
            auto& res = m_physicalResources[resIdx];

            // 1. Gather all touches by passes that weren't culled
            struct Touch {
                uint32_t passIdx;
                AccessType access;
            };
            std::vector<Touch> combinedTouches;

            for (const auto& t : resourceTouchList[resIdx]) {
                if (needed[t.first]) {
                    combinedTouches.push_back({ t.first, t.second });
                }
            }

            if (combinedTouches.empty()) return;

            // 2. Sort by actual execution order (topological order)
            std::ranges::sort(combinedTouches, [&](const auto& a, const auto& b) {
                return passIndexToOrderIndex.at(a.passIdx) < passIndexToOrderIndex.at(b.passIdx);
                });

            // 3. Handle Buffer Barriers
            if (res.isBuffer) {
                VkBuffer actualBuffer = GetPhysicalBuffer(RGHandle{ resIdx }, resourceManager);
                if (actualBuffer == VK_NULL_HANDLE) return;

                for (size_t i = 1; i < combinedTouches.size(); ++i) {
                    const auto& prevTouch = combinedTouches[i - 1];
                    const auto& curTouch = combinedTouches[i];

                    // Buffers only need barriers if a Write is involved (RAW, WAR, WAW)
                    if (IsWriteAccess(prevTouch.access) || IsWriteAccess(curTouch.access)) {
                        auto barrier = MakeBufferBarrier(actualBuffer, prevTouch.access, curTouch.access);
                        uint32_t orderIdx = passIndexToOrderIndex.at(curTouch.passIdx);
                        m_compiledPasses[orderIdx].bufferBarriers.push_back(barrier);
                    }
                }
                return; // Finished processing buffer
            }

            // 4. Handle Image Barriers
            VkImage actualImage = GetPhysicalImage(RGHandle{ resIdx }, resourceManager);
            if (actualImage == VK_NULL_HANDLE) return;

            // Initial transition (Undefined/Current -> First Use)
            {
                const auto& firstTouch = combinedTouches[0];
                ResourceAccessInfo curInfo = GetResourceAccessInfo(firstTouch.access, res.textureDesc);

                VkImageMemoryBarrier2 b{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2 };
                b.srcStageMask = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT;
                b.srcAccessMask = 0;
                b.dstStageMask = curInfo.stageMask;
                b.dstAccessMask = curInfo.accessMask;
                b.oldLayout = res.currentLayout;
                b.newLayout = curInfo.layout;
                b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                b.image = actualImage;
                b.subresourceRange = { res.textureDesc.aspect, 0, res.textureDesc.mipLevels > 0 ? res.textureDesc.mipLevels : VK_REMAINING_MIP_LEVELS, 0, VK_REMAINING_ARRAY_LAYERS };

                uint32_t orderIdx = passIndexToOrderIndex.at(firstTouch.passIdx);
                m_compiledPasses[orderIdx].imageBarriers.push_back(b);
                res.currentLayout = curInfo.layout;
            }

            // Subsequent transitions (e.g., Color Attachment -> Shader Read)
            for (size_t i = 1; i < combinedTouches.size(); ++i) {
                const auto& prevTouch = combinedTouches[i - 1];
                const auto& curTouch = combinedTouches[i];

                auto barrier = MakeBarrierForResourceTransition(res, actualImage, prevTouch.access, curTouch.access);
                res.currentLayout = barrier.newLayout;

                uint32_t orderIdx = passIndexToOrderIndex.at(curTouch.passIdx);
                m_compiledPasses[orderIdx].imageBarriers.push_back(barrier);
            }
            };

        for (uint32_t r = 0; r < resCount; ++r) {
            ProcessTimeline(r);
        }
    }


        VkImageMemoryBarrier2 RenderGraph::MakeBarrierForResourceTransition(const RGPhysicalResource& res,VkImage actualImage,AccessType prevAccess,AccessType curAccess)
        {
            ResourceAccessInfo prevInfo = GetResourceAccessInfo(prevAccess, res.textureDesc);
            ResourceAccessInfo curInfo = GetResourceAccessInfo(curAccess, res.textureDesc);

            VkImageMemoryBarrier2 b{};
            b.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
            b.srcStageMask = prevInfo.stageMask;
            b.srcAccessMask = prevInfo.accessMask;
            b.dstStageMask = curInfo.stageMask;
            b.dstAccessMask = curInfo.accessMask;
            b.oldLayout = res.currentLayout; //use the current layout tracked by the graph
            b.newLayout = curInfo.layout;
            b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;

            b.image = actualImage;

            b.subresourceRange.aspectMask = res.textureDesc.aspect != 0
                ? res.textureDesc.aspect
                : VK_IMAGE_ASPECT_COLOR_BIT;
            b.subresourceRange.baseArrayLayer = 0;
            b.subresourceRange.layerCount = res.textureDesc.arrayLayers;
            b.subresourceRange.baseMipLevel = 0;
            b.subresourceRange.levelCount = res.textureDesc.mipLevels > 0
                ? res.textureDesc.mipLevels
                : VK_REMAINING_MIP_LEVELS;

            return b;
        }

        VkBufferMemoryBarrier2 RenderGraph::MakeBufferBarrier(
            VkBuffer buffer,
            AccessType prevAccess,
            AccessType curAccess)
        {

            ResourceAccessInfo prev = GetResourceAccessInfo(prevAccess, {});
            ResourceAccessInfo cur = GetResourceAccessInfo(curAccess, {});

            VkBufferMemoryBarrier2 b{ VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2 };
            b.srcStageMask = prev.stageMask;
            b.srcAccessMask = prev.accessMask;
            b.dstStageMask = cur.stageMask;
            b.dstAccessMask = cur.accessMask;
            b.buffer = buffer;
            b.offset = 0;
            b.size = VK_WHOLE_SIZE;
            b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;

            return b;
        }

        void RenderGraph::Reset(Core::ResourceManager& resManager) {

            for (const auto& res : m_physicalResources) {
                if (res.isTransient) {
                    if (!res.isBuffer && res.physicalTexture.IsValid()) {
                        resManager.ReleaseTransientTexture(res.physicalTexture, res.textureDesc);
                    }
                    else if (res.isBuffer && res.physicalBuffer.IsValid()) {
                        resManager.ReleaseTransientBuffer(res.physicalBuffer, res.bufferDesc);
                    }
                }
            }

            m_passes.clear();
            m_compiledPasses.clear();
            m_physicalResources.clear();
            m_resourceRegistry.clear();
            m_passDependencies.clear();
            m_resourceFirstUse.clear();
            m_resourceLastUse.clear();
            m_barriersPerPass.clear();
            m_passOrder.clear();

            m_frameQueriesValid.assign(m_frameQueriesValid.size(), false);

        }

        RGHandle RenderGraph::RegisterImportedImage(VkImage image, VkFormat format, VkExtent3D extent, VkImageLayout currentLayout, uint32_t mipLevels, uint32_t arrayLayers) {
            RGHandle handle{ static_cast<uint32_t>(m_physicalResources.size()) };
            RGPhysicalResource res{};
            res.textureDesc.format = format;
            res.textureDesc.extent = extent;
            res.textureDesc.aspect = VK_IMAGE_ASPECT_COLOR_BIT;
            res.isBuffer = false;
            res.isTransient = false;
            res.isImported = true;
            res.importedImage = image; 
            res.textureDesc.mipLevels = mipLevels;
            res.textureDesc.arrayLayers = arrayLayers;
            res.currentLayout = currentLayout;
            m_physicalResources.push_back(res);
            return handle;
        }

        RGHandle RenderGraph::RegisterImportedBuffer(VkBuffer buffer, const Core::BufferDesc& desc) {
            RGHandle handle{ static_cast<uint32_t>(m_physicalResources.size()) };

            RGPhysicalResource res{};
            res.bufferDesc = desc;
            res.isBuffer = true;
            res.isTransient = false;
            res.isImported = true;
            res.importedBuffer = buffer;

            m_physicalResources.push_back(res);
            return handle;
        }
   

        VkImage RenderGraph::GetPhysicalImage(RGHandle handle, Core::ResourceManager& resManager) const {
            if (!handle.IsValid() || handle.id >= m_physicalResources.size()) return VK_NULL_HANDLE;

            const auto& res = m_physicalResources[handle.id];
            if (res.isImported) {
                return res.importedImage;
            }
            if (res.physicalTexture.IsValid()) {
                auto* tex = resManager.GetTexture(res.physicalTexture);
                if (tex) return tex->image;
            }

            return VK_NULL_HANDLE;
        }

    VkBuffer RenderGraph::GetPhysicalBuffer(RGHandle handle, Core::ResourceManager& resManager) const
    {
        if (!handle.IsValid() || handle.id >= m_physicalResources.size()) {
            return VK_NULL_HANDLE;
        }

        const auto& res = m_physicalResources[handle.id];

        if (res.isImported) {
            return res.importedBuffer;
        }

        // Handle transient buffers allocated by the graph
        if (res.physicalBuffer.IsValid()) {
            auto* bufferPtr = resManager.GetBuffer(res.physicalBuffer);
            if (bufferPtr) {
                return bufferPtr->buffer;
            }
        }

        return VK_NULL_HANDLE;
    }

    void RenderGraph::InitProfiling(VkDevice device, VkPhysicalDevice physicalDevice, uint32_t maxFramesInFlight)
    {
        m_maxFramesInFlight = maxFramesInFlight;

        VkPhysicalDeviceProperties deviceProperties;
        vkGetPhysicalDeviceProperties(physicalDevice, &deviceProperties);
        m_timestampPeriod = deviceProperties.limits.timestampPeriod;

        VkQueryPoolCreateInfo queryPoolInfo{};
        queryPoolInfo.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
        queryPoolInfo.queryType = VK_QUERY_TYPE_TIMESTAMP;
        queryPoolInfo.queryCount = m_maxQueriesPerFrame * m_maxFramesInFlight;

        if (vkCreateQueryPool(device, &queryPoolInfo, nullptr, &m_queryPool) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create timestamp query pool!");
        }

        // Reset all queries on the CPU right after creation
        vkResetQueryPool(device, m_queryPool, 0, queryPoolInfo.queryCount);

        m_cpuTimes.resize(256, 0.0f);
        m_gpuTimes.resize(256, 0.0f);

        m_frameQueriesValid.assign(maxFramesInFlight, false);
    }

    void RenderGraph::DestroyProfiling(VkDevice device)
    {
        if (m_queryPool != VK_NULL_HANDLE) {
            vkDestroyQueryPool(device, m_queryPool, nullptr);
            m_queryPool = VK_NULL_HANDLE;
        }
    }
    }
