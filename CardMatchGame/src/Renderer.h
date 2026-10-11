#pragma once

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <vector>
#include <string>

// This is the data for a single vertex. For now only stores position
struct Vertex {
    glm::vec2 pos;
    glm::vec2 uv; // (0,0) top left of the texture (1,1) is bottom right
};

// everything the renderer needs to draw one quad
struct QuadInstance {
    glm::mat4 model; // position / scale / rotation
    glm::vec4 color; // RGBA (each 0.0 - 1.0)
};

class Renderer {
public:
    // runs every vulkan setup step in order. returns -1 on failure
    int Init();

    void DrawFrame(const std::vector<QuadInstance>& quads);

    bool WindowShouldClose();
    void PollEvents();
    void WaitIdle(); // wait for the gpu to finish everything, used before Cleanup

    void Cleanup();

    void GetCursorPos(double& x, double& y); // mouse position in pixels. top left is (0,0)
    void GetFramebufferSize(int& width, int& height); // actual drawable size in pixels
    bool IsMouseButtonPressed(int button); // true while the given button is held down

    double GetTime(); // seconds since the window system started

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

    VkImage textureImage;
    VkDeviceMemory textureImageMemory;
    VkImageView textureImageView; // how shaders will look at the image
    VkSampler textureSampler; // how shaders will read from it (filtering / edges)

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

    void RecordCommandBuffer(VkCommandBuffer cmdBuffer, uint32_t imageIndex, const std::vector<QuadInstance>& quads);

    uint32_t FindMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties);
    void CreateBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties, VkBuffer& buffer, VkDeviceMemory& bufferMemory);
    void CreateVertexBuffer();
    void CreateIndexBuffer();

    void CleanupSwapchain();
    void RecreateSwapchain();

    VkCommandBuffer BeginSingleTimeCommands();
    void EndSingleTimeCommands(VkCommandBuffer cmd);
    void CreateImage(uint32_t width, uint32_t height, VkFormat format, VkImageUsageFlags usage, VkMemoryPropertyFlags properties, VkImage& image, VkDeviceMemory& imageMemory);
    void TransitionImageLayout(VkImage image, VkImageLayout oldLayout, VkImageLayout newLayout);
    int CreateTexture();
    int CreateTextureSampler();
};