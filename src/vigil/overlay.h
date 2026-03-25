#pragma once

#include "petra/window.h"
#include "render/context.h"
#include <vulkan/vulkan.h>
#include <glm/glm.hpp>

namespace Vigil {

struct DebugStats {
    float fps = 0.0f;
    float frameTime = 0.0f;
    uint32_t drawCalls = 0;
    uint32_t vertexCount = 0;
    glm::vec3 cameraPos{0.0f};
    glm::vec3 cameraFront{0.0f};
};

class Overlay {
public:
    Overlay(const Petra::Window& window, const Render::Context& renderCtx);
    ~Overlay();

    Overlay(const Overlay&) = delete;
    Overlay& operator=(const Overlay&) = delete;

    // Starts a new ImGui frame
    void beginFrame();
    
    // Renders the custom engine overlay
    void drawUI(const Render::Context& renderCtx, const DebugStats& stats);

    // Renders ImGui data to the command buffer
    void endFrameAndRecord(VkCommandBuffer cmdBuf);

private:
    VkDevice m_device = VK_NULL_HANDLE;
    VkDescriptorPool m_descriptorPool = VK_NULL_HANDLE;
};

} // namespace Vigil
