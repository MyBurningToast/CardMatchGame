#pragma once

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <vector>
#include <string>

// This is the data for a single vertex. For now only stores position
// TODO: store color data in vertex
struct Vertex {
    glm::vec2 pos;
};

class Renderer {
public:
    // runs every vulkan setup step in order. returns -1 on failure
    int Init();

    // draws one frame. transforms is one model matrix per quad to draw (eg one per card)
    void DrawFrame(const std::vector<glm::mat4>& transforms);

    bool WindowShouldClose();
    void PollEvents();
    void WaitIdle(); // wait for the gpu to finish everything, used before Cleanup

    void Cleanup();

private:
    GLFWwindow* window;
    VkInstance instance;
    VkSurfaceKHR surface;
    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
    uint32_t graphicsQueueFamilyIndex = 0;
    VkDevice device;
    VkSurfaceFormatKHR surfaceFormat;
    VkSurfaceCapabilitiesKHR capabilities;
    VkSwapchainKHR swapchain;
    std::vector<VkImage> swapchainImages;
    std::vector<VkImageView> swapchainImageViews;
    VkRenderPass renderPass;
    VkPipelineLayout pipelineLayout;
    VkPipeline graphicsPipeline;
    VkQueue graphicsQueue;

    std::vector<VkFramebuffer> swapchainFramebuffers;
    VkCommandPool commandPool;
    VkCommandBuffer commandBuffer;
    VkSemaphore imageAvailableSemaphore;
    std::vector<VkSemaphore> renderFinishedSemaphores;
    VkFence inFlightFence;

    VkBuffer vertexBuffer;
    VkDeviceMemory vertexBufferMemory;
    VkBuffer indexBuffer;
    VkDeviceMemory indexBufferMemory;

    bool CheckValidationLayerSupport(const std::vector<const char*>& validationLayers);

    int InitWindow();
    int CreateInstance();
    int CreateSurface();
    int PickPhysicalDevice();
    int CreateLogicalDevice();
    int CreateSwapchain();
    int CreateImageViews();
    int CreateRenderPass();
    int CreateGraphicsPipeline();
    void GetGraphicsQueue();
    int CreateFramebuffers();
    int CreateCommandPool();
    int CreateCommandBuffer();
    int CreateSyncObjects();

    void RecordCommandBuffer(VkCommandBuffer cmdBuffer, uint32_t imageIndex, const std::vector<glm::mat4>& transforms);

    uint32_t FindMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties);
    void CreateBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties, VkBuffer& buffer, VkDeviceMemory& bufferMemory);
    void CreateVertexBuffer();
    void CreateIndexBuffer();

    void CleanupSwapchain();
    void RecreateSwapchain();
};