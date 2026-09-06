#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <iostream>
#include <vector>
#include <cstring>
#include <fstream>
#include <chrono>

void CleanupSwapchain();

const std::vector<const char*> validationLayers = {
    "VK_LAYER_KHRONOS_validation" // comes with vulkan SDK
};

// vulkans validation layers help find errors by intercepting api calls.
// Without it, the gpu will just crash with no error info
#ifdef NDEBUG
const bool enableValidationLayers = false; // off in release builds
#else
const bool enableValidationLayers = true; // on in debug builds
#endif

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

VkBuffer uniformBuffer;
VkDeviceMemory uniformBufferMemory;
void* uniformBufferMapped; // kept mapped permanently and write to it every frame

VkDescriptorSetLayout descriptorSetLayout;
VkDescriptorPool descriptorPool;
VkDescriptorSet descriptorSet;

// This is the data for a single vertex. For now only stores position
// TODO: store color data in vertex
struct Vertex {
    glm::vec2 pos;
};

/*
Note for myself:
uniform buffers and MVP (model view projection)

A ubo is data that is the same for every vertex/fragment in a draw call
used for stuff like model/view/projection, light position or a time vlaue
things the draw call need "uniformaly" or glbal

the MVP idea
model matrix - moves/rotates/scales the mesh from its own local space into world space
view matrix - moves the world so it lines up with where the camera is looking (inverse of camera's transfom?)
projection matrix - convert 3d spac einto the clip space range vulkan expects also handfles fov/aspect/persepctive
final vertex pos - prjection * view * modle * vertexPos (done in vertex shader)
mutlipling all 3 togther on the cpu before upload as one matrix saves the shader from doing 3 matrix mutiplicatons per vertex


how to use ubo:

define the struct (cpu side)
matche the layout the shader expects liek glm::mat4 model; (or model/view/proj all three)
make sure it follows alignment rules (std140). some types need padding (im pretty sure glm has alignas helpers for this, need to check)

create the bufer
same as CreateBuffer() flow but usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT instead of vertex or index
memory props HOST_VISIBLE | HOST_COHERENT cos gets rewritten every frame anyways

persistent map (differnt from vertex buffer)
since this changes every frame, map once at creation and keep the pointer and memcpy new matrix data into it each frame without mpa and unmap (pretty sure that has overhead)

descriptor set layout
tell the pipeline to expect a uniform buffer at binding X - VkDescriptorSetLayout
done only  at pipeline creation time

descriptor pool + descriptor set
pool - where descriptors get allocated from (like command pool???)
descriptor set - the actual object that points a specific binding number to a specific VkBuffer

shader side
layout(binding = 0) uniform UniformBufferObject { mat4 model; mat4 view; mat4 proj; } ubo;
binding here is a different numbering system than location (vertex attributes) - read more about this

bind before drawing
vkCmdBindDescriptorSets(...) before vkCmdDraw/vkCmdDrawIndexed
tells the gpu which descriptor set (and therefore which buffer) to use for this draw call
*/

// read only memory buffer used to pass global data
struct UniformBufferObject {
    glm::mat4 model; // 16 floats
};

// in Vulkans NDC(Normalized Device Coordinates)/screen space, positive Y points downward
const std::vector<Vertex> vertices = {
    {{-0.5f, -0.5f}}, // top left
    {{ 0.5f, -0.5f}}, // top right
    {{ 0.5f,  0.5f}}, // bottom right
    {{-0.5f,  0.5f}}, // bottom left
};

// index buffer data
const std::vector<uint16_t> indices = {
    0, 1, 2,
    2, 3, 0
};


int CreateDescriptorSetLayout() {
    VkDescriptorSetLayoutBinding uboLayoutBinding{};
    uboLayoutBinding.binding = 0; // matches layout(binding = 0) in the shader
    uboLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    uboLayoutBinding.descriptorCount = 1;
    uboLayoutBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT; // only the vertex shader reads it

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = 1;
    layoutInfo.pBindings = &uboLayoutBinding;

    if (vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &descriptorSetLayout) != VK_SUCCESS) {
        std::cerr << "Failed to create descriptor set layout\n";
        return -1;
    }

    std::cout << "Descriptor set layout created\n";
    return 0;
}







// reads the compiled shader from disk, copys it to ram, then will give that address to vulkan
std::vector<char> ReadFile(const std::string& filename) {

    // input file stream
    // ios::ate starts at the end so we know file size
    // use binary means it will just read the raw bytes
    // uses bitwise or to combine them into a single mask
    std::ifstream file(filename, std::ios::ate | std::ios::binary);
    if (!file.is_open()) {
        throw std::runtime_error("Failed to open file: " + filename);
    }
    size_t fileSize = (size_t)file.tellg(); // current position == size
    std::vector<char> buffer(fileSize);

    file.seekg(0); // back to start
    file.read(buffer.data(), fileSize); // read start to end and save the bytes to a buffer in memory
    file.close();
    return buffer;
}

VkShaderModule CreateShaderModule(VkDevice device, const std::vector<char>& code) {
    VkShaderModuleCreateInfo createINfo{};
    createINfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    createINfo.codeSize = code.size();
    createINfo.pCode = reinterpret_cast<const uint32_t*>(code.data());

    VkShaderModule shaderModule;
    if (vkCreateShaderModule(device, &createINfo, nullptr, &shaderModule) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create shader module");
    }
    return shaderModule;
}

bool CheckValidationLayerSupport(const std::vector<const char*>& validationLayers) {
    uint32_t layerCount = 0;
    vkEnumerateInstanceLayerProperties(&layerCount, nullptr);
    std::vector<VkLayerProperties> availableLayers(layerCount);
    vkEnumerateInstanceLayerProperties(&layerCount, availableLayers.data());

    // for every layer, check it exists somwhere in the available list
    for (const char* layerName : validationLayers) {
        bool found = false;
        for (const auto& layerProps : availableLayers) {
            if (strcmp(layerName, layerProps.layerName) == 0) { // compare strings
                found = true;
                break;
            }
        }
        if (!found) return false;
    }
    return true;
}

int InitWindow() {
    // chek glfw initilizes
    if (!glfwInit()) {
        std::cerr << "Failed to init GLFW\n";
        return -1; // in c++ a non zero return value is considered an error
    }

    if (enableValidationLayers && !CheckValidationLayerSupport(validationLayers)) {
        std::cerr << "Validation layers requested but not available\n";
        return -1;
    }

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API); // specifiy no OpenGL context beaucse im using Vulkan
    window = glfwCreateWindow(640, 480, "Test", nullptr, nullptr);
    if (!window) {
        std::cerr << "Failed to create GLFW window\n";
        glfwTerminate();
        return -1;
    }

    return 0;
}

int CreateInstance() {
    // Vulkn check
    VkApplicationInfo appInfo{};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = "test";
    appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.pEngineName = "no engine";
    appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.apiVersion = VK_API_VERSION_1_0;

    // GLFW extensions
    uint32_t glfwExtensionCount = 0;
    const char** glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);

    VkInstanceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    createInfo.pApplicationInfo = &appInfo;
    createInfo.enabledExtensionCount = glfwExtensionCount;
    createInfo.ppEnabledExtensionNames = glfwExtensions;
    if (enableValidationLayers) {
        createInfo.enabledLayerCount = static_cast<uint32_t>(validationLayers.size());
        createInfo.ppEnabledLayerNames = validationLayers.data();
    }
    else {
        createInfo.enabledLayerCount = 0;
    }

    VkResult result = vkCreateInstance(&createInfo, nullptr, &instance);
    if (result != VK_SUCCESS) {
        std::cerr << "Failed to create Vulkan instance " << result << "\n";
        glfwDestroyWindow(window);
        glfwTerminate();
        return -1;
    }

    return 0;
}

int CreateSurface() {
    // to draw stuff to the screen we need a bridge between vulkan and the glfw window
    VkResult surfaceResult = glfwCreateWindowSurface(instance, window, nullptr, &surface); // glfw can do this automaticly based on operating system & graphics api
    if (surfaceResult != VK_SUCCESS) {
        std::cerr << "Failed to create window surface\n";
        return -1;

    }
    std::cout << "Window surface created\n";

    return 0;
}

int PickPhysicalDevice() {
    // Pick a physical device. Somone might have multiple GPUs
    uint32_t deviceCount = 0; // Vulkan uses explicit, fixed width integer types. A normal ints size is compiler dependant
    vkEnumeratePhysicalDevices(instance, &deviceCount, nullptr); // vulkan will count all the gpus that support it
    if (deviceCount == 0) { // couldnt find a compatable gpu
        std::cerr << "No GPU with Vulkan support\n";
        return -1;
    }

    std::vector<VkPhysicalDevice> devices(deviceCount);
    vkEnumeratePhysicalDevices(instance, &deviceCount, devices.data()); // .data beacuse vulkan expects c style pointers

    // null handel is the vulkan defined null value for handles
    physicalDevice = VK_NULL_HANDLE;
    graphicsQueueFamilyIndex = 0;

    // a gpu can have multiple queue families, there are groups of commands it supports.
    // in this case we need one that supports graphics commands
    for (const auto& device : devices) {
        uint32_t queueFamilyCount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, nullptr);

        std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
        vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, queueFamilies.data());

        for (uint32_t i = 0; i < queueFamilyCount; i++) {

            // queueFlags is a bitmask (a sinlge int repersenting differnt capabilitys)
            // thats why we need to check with bitwise & operator insead of ==
            bool hasGraphics = queueFamilies[i].queueFlags & VK_QUEUE_GRAPHICS_BIT; // VK_QUEUE_GRAPHICS_BIT means it supports graphics commands

            // Even if the gpu can do graphics commands it dosnt nesseserily mean it can present the images to our surface
            VkBool32 presentSupport = false;
            vkGetPhysicalDeviceSurfaceSupportKHR(device, i, surface, &presentSupport);

            if (hasGraphics && presentSupport) {
                physicalDevice = device;
                graphicsQueueFamilyIndex = i; // save what index the queue family for graphics is
                break;
            }
        }

        //for now this is just getting the first compatable gpu with a graphics famility
        // later, we can check for a discrete graphics card for better performance
        if (physicalDevice != VK_NULL_HANDLE) break;
    }

    if (physicalDevice == VK_NULL_HANDLE) {
        std::cerr << "No GPU with graphics support found\n";
        return -1;
    }

    VkPhysicalDeviceProperties deviceProps;
    vkGetPhysicalDeviceProperties(physicalDevice, &deviceProps);
    std::cout << "Using GPU: " << deviceProps.deviceName << "\n";

    return 0;
}

int CreateLogicalDevice() {
    // create logical device. the logical device is the interface to the gpu
    // vulkan needs to know how many queues we want from the graphics queue faimily and what priority 0-1
    float queuePriority = 1.0f;

    // a queue is where we submit work for the gpu to execute
    VkDeviceQueueCreateInfo queueCreateInfo{};
    queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO; //sType tells vulkan what struct it has been given
    queueCreateInfo.queueFamilyIndex = graphicsQueueFamilyIndex;
    queueCreateInfo.pQueuePriorities = &queuePriority;
    queueCreateInfo.queueCount = 1;
    queueCreateInfo.pQueuePriorities = &queuePriority;

    // the special GPU features we want enabled (for now, none)
    VkPhysicalDeviceFeatures deviceFeatures{};

    const std::vector<const char*> deviceExtensions = {
        VK_KHR_SWAPCHAIN_EXTENSION_NAME
    };

    VkDeviceCreateInfo deviceCreateInfo{};
    deviceCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    deviceCreateInfo.pQueueCreateInfos = &queueCreateInfo;
    deviceCreateInfo.queueCreateInfoCount = 1;
    deviceCreateInfo.pEnabledFeatures = &deviceFeatures;
    // automaticly update the size. must cast to uint32_t beaucse .size() returns a size_t
    // specify the count so that vulkan knows how many entries to go along from the start of the array pointer
    deviceCreateInfo.enabledExtensionCount = static_cast<uint32_t>(deviceExtensions.size());
    deviceCreateInfo.ppEnabledExtensionNames = deviceExtensions.data();

    VkResult deviceResult = vkCreateDevice(physicalDevice, &deviceCreateInfo, nullptr, &device); // create a device for our physical device

    if (deviceResult != VK_SUCCESS) {
        std::cerr << "Failed to create logical device\n";
        return -1;
    }

    return 0;
}

int CreateSwapchain() {
    // create the swapchain (the images we render into and present)
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDevice, surface, &capabilities); // get info about the window

    // check what pixel formats the surface supports
    // surface format tells vulkan how the images in the swapchain should store their pixles
    // some windows/gpus cant necessarily display every pixel format
    uint32_t formatCount = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface, &formatCount, nullptr); // ask how many formats exist
    std::vector<VkSurfaceFormatKHR> formats(formatCount); // allocate enough space
    vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface, &formatCount, formats.data()); // fill that allocated array

    // just get the first available format, good enough for now
    surfaceFormat = formats[0];

    // how many images to put in the swapchain
    // Im using double buffering here so that CPU can work on the next frame while the previous one is still being rendered
    uint32_t imageCount = 2;
    if (imageCount < capabilities.minImageCount) {
        imageCount = capabilities.minImageCount; // if minimum is higher, set it to that minimum
    }

    VkSwapchainCreateInfoKHR swapchainCreateInfo{};
    swapchainCreateInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    swapchainCreateInfo.surface = surface;
    swapchainCreateInfo.minImageCount = imageCount;
    swapchainCreateInfo.imageFormat = surfaceFormat.format;
    swapchainCreateInfo.imageColorSpace = surfaceFormat.colorSpace;
    swapchainCreateInfo.imageExtent = capabilities.currentExtent; // resolution width and height
    swapchainCreateInfo.imageArrayLayers = 1; // each swapchain image has 1 layer
    swapchainCreateInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT; // tells vulkan we will render color into these images
    swapchainCreateInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE; // only one queue family uses the images
    swapchainCreateInfo.preTransform = capabilities.currentTransform; // no flip or rotation
    swapchainCreateInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR; // fully opaque image
    swapchainCreateInfo.presentMode = VK_PRESENT_MODE_FIFO_KHR; // VSync. the vulkan spec guarantes this to be supported
    swapchainCreateInfo.clipped = VK_TRUE; // discard pixels hidden behind other windows

    VkResult swapchainResult = vkCreateSwapchainKHR(device, &swapchainCreateInfo, nullptr, &swapchain);
    if (swapchainResult != VK_SUCCESS) {
        std::cerr << "Failed to create swapchain\n";
        return -1;
    }

    std::cout << "Swapchain created with " << imageCount << " images\n";

    // Fetch handles to the actual images the swapchain created for us
    vkGetSwapchainImagesKHR(device, swapchain, &imageCount, nullptr);
    swapchainImages.resize(imageCount);
    // We only specify a minimum count and it may have created more
    vkGetSwapchainImagesKHR(device, swapchain, &imageCount, swapchainImages.data());

    return 0;
}

int CreateImageViews() {
    // create image view for each swapchain image
    // image views descrie how to access an image. stuff like format and dimensions
    // This is beacuse vulkan wont let you render into raw images
    swapchainImageViews.resize(swapchainImages.size());
    for (size_t i = 0; i < swapchainImages.size(); i++) {
        VkImageViewCreateInfo viewCreateInfo{};
        viewCreateInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewCreateInfo.image = swapchainImages[i]; // assosiated image
        viewCreateInfo.viewType = VK_IMAGE_VIEW_TYPE_2D; // normal 2d image
        viewCreateInfo.format = surfaceFormat.format; // must match the swapchains format

        // swizzle lets you remap color chanels, im not going to do that
        viewCreateInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
        viewCreateInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
        viewCreateInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
        viewCreateInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;

        // subresourceRnage is which part of the image this view covers
        viewCreateInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT; // color data not depth or stencil
        viewCreateInfo.subresourceRange.baseMipLevel = 0;
        viewCreateInfo.subresourceRange.levelCount = 1; // no mip mapping, only full resolution
        viewCreateInfo.subresourceRange.baseArrayLayer = 0;
        viewCreateInfo.subresourceRange.layerCount = 1; // not an array texture

        VkResult viewResult = vkCreateImageView(device, &viewCreateInfo, nullptr, &swapchainImageViews[i]);
        if (viewResult != VK_SUCCESS) {
            std::cerr << "Failed to create image view " << i << "\n";
            return -1;
        }

    }

    std::cout << "Created " << swapchainImageViews.size() << " image views\n";

    return 0;
}

int CreateRenderPass() {
    // create the render pass
    // a render pass describes what happens to the images during a frame

    VkAttachmentDescription colorAttachment{}; // storeed recource list for whole render pass
    colorAttachment.format = surfaceFormat.format; // match swapchain format
    colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT; // 1 sample per pixel

    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR; // clear the previous image with solid color
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE; // keep the rendered result so it can be presented

    colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE; // not using stencils
    colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;

    colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED; // done care about previous images layout
    colorAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR; // must be ready to present to the screen when done

    // a render pass is made of one or more subpasses
    // but we only need a sinlge subpass that writes color to our only attachment

    VkAttachmentReference colorAttachmentRef{}; // stores info for a subpass
    colorAttachmentRef.attachment = 0; // index into attachment array
    colorAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL; // best layout for writing color during rendering

    VkSubpassDescription subpass{}; // TODO: where we will record draw commands
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS; // this is a graphics subpass (not compute)
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorAttachmentRef;

    // the swapchain might still be being presented from the previous frame
    // the gpu wants to write the next frames colour into the same memroy
    // dependancy is a syncronisation rule for this render pass that waits until the image is safe to write to
    VkSubpassDependency dependency{};
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL; // wait on: stuff before this render pass
    dependency.dstSubpass = 0; // applies to: our subpass
    dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT; // wait at: color attachment output stage
    dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT; // block the color write stage
    dependency.srcAccessMask = 0; // no prior access to sync with
    dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT; // guard: writing color

    VkRenderPassCreateInfo renderPassCreateInfo{};
    renderPassCreateInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    renderPassCreateInfo.attachmentCount = 1;
    renderPassCreateInfo.pAttachments = &colorAttachment;
    renderPassCreateInfo.subpassCount = 1;
    renderPassCreateInfo.pSubpasses = &subpass;
    renderPassCreateInfo.dependencyCount = 1;
    renderPassCreateInfo.pDependencies = &dependency;

    VkResult renderPassResult = vkCreateRenderPass(device, &renderPassCreateInfo, nullptr, &renderPass);
    if (renderPassResult != VK_SUCCESS) {
        std::cerr << "Failed to create render pass\n";
        return -1;
    }

    std::cout << "Render pass created\n";

    return 0;
}

int CreateGraphicsPipeline() {
    // load shaders

    std::vector<char> vertShaderCode = ReadFile("shaders/vert.spv");
    std::vector<char> fragShaderCode = ReadFile("shaders/frag.spv");

    VkShaderModule vertShaderModule = CreateShaderModule(device, vertShaderCode);
    VkShaderModule fragShaderModule = CreateShaderModule(device, fragShaderCode);

    // tell Vulkan which shader stage each module is used for and its entry point
    VkPipelineShaderStageCreateInfo vertStageInfo{};
    vertStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    vertStageInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
    vertStageInfo.module = vertShaderModule;
    vertStageInfo.pName = "main"; // the function name in the glsl file

    VkPipelineShaderStageCreateInfo fragStageInfo{};
    fragStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    fragStageInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    fragStageInfo.module = fragShaderModule;
    fragStageInfo.pName = "main";


    VkPipelineShaderStageCreateInfo shaderStages[] = {
        vertStageInfo,
        fragStageInfo
    };

    // interpet vertex structs raw bytes as shader input
    VkVertexInputBindingDescription bindingDescription{};
    bindingDescription.binding = 0; // this buffers slot in the command state. there migh be another buffer for color. TODO: add color binding
    bindingDescription.stride = sizeof(Vertex); // how many bytes to skip to get to the next in the buffer
    bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX; // go to next vertex's data for ever vertex processed

    VkVertexInputAttributeDescription attributeDescription{};
    attributeDescription.binding = 0; // which binding (vertex buffer stream) this attribute's data comes from. matches the binding description
    attributeDescription.location = 0; // matches location = 0 in the vertex shader. TODO: add another attribute for colour
    attributeDescription.format = VK_FORMAT_R32G32_SFLOAT; // vec2 of floats
    attributeDescription.offset = offsetof(Vertex, pos); // byte offset of the pos field within the vertex struct

    // combine binding and attribute descipritons and attachs them to the pipline
    VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
    vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInputInfo.vertexBindingDescriptionCount = 1;
    vertexInputInfo.pVertexBindingDescriptions = &bindingDescription;
    vertexInputInfo.vertexAttributeDescriptionCount = 1;
    vertexInputInfo.pVertexAttributeDescriptions = &attributeDescription;

    // input assembly is how vertices are grouped into shapes
    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST; // every 3 vertices makess 1 trianlge
    inputAssembly.primitiveRestartEnable = VK_FALSE; // disable restarting primitive strips

    VkViewport viewport{};
    viewport.x = 0.0f; // no offset, start in top left
    viewport.y = 0.0f;
    viewport.width = (float)capabilities.currentExtent.width; // stretch NDC across the full width of window
    viewport.height = (float)capabilities.currentExtent.height; // NDC is Normalized Device Coordinates so (-1 to 1)
    viewport.minDepth = 0.0f; // write to depth buffer between 0 and 1
    viewport.maxDepth = 1.0f;

    VkRect2D scissor{}; // anything outside the scissor gets discaarded, we want full image
    scissor.offset = { 0, 0 }; // keep top left
    scissor.extent = capabilities.currentExtent; // to bottom right

    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.pViewports = &viewport;
    viewportState.scissorCount = 1;
    viewportState.pScissors = &scissor;


    // the rasterizer turns triangles into pixels
    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.depthClampEnable = VK_FALSE; // just discard anything outside near / far clip plane
    rasterizer.rasterizerDiscardEnable = VK_FALSE; // enable/disable the entire rasterizer stage
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL; // fill trianlges solid (not wireframe or points)
    rasterizer.lineWidth = 1.0f;
    rasterizer.cullMode = VK_CULL_MODE_NONE; // shade all trangles
    // TODO: dont shade trangles not facing the camera
    rasterizer.frontFace = VK_FRONT_FACE_CLOCKWISE; // winding order
    rasterizer.depthBiasEnable = VK_FALSE; // dont change depth values

    // ill keep multisampling disabled for now
    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.sampleShadingEnable = VK_FALSE;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    // Color blending is how new pixels combine with whats already there. this is used for transparency
    VkPipelineColorBlendAttachmentState colorBlendAttachment{};
    colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT; // bitmask for what color channels get written to the framebuffer for this attachment
    colorBlendAttachment.blendEnable = VK_FALSE;

    // even though colorblending is disabled, its still required by vulkan
    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.logicOpEnable = VK_FALSE; // i think its only enabled for super niche blending math
    colorBlending.attachmentCount = 1; // only write to one color attachment
    colorBlending.pAttachments = &colorBlendAttachment;

    // pipline layout is for passing uniform buffer objects (pretty sure it can pass in other stuff also)
    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &descriptorSetLayout;
    pipelineLayoutInfo.pushConstantRangeCount = 0;

    if (vkCreatePipelineLayout(device, &pipelineLayoutInfo, nullptr, &pipelineLayout) != VK_SUCCESS) {
        std::cerr << "Failed to create pipeline layout\n";
        return -1;
    }

    // now combine all of this into a pipeline
    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = 2; // only have vertex and fragment shader
    pipelineInfo.pStages = shaderStages;
    pipelineInfo.pVertexInputState = &vertexInputInfo;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.layout = pipelineLayout;
    pipelineInfo.renderPass = renderPass;
    pipelineInfo.subpass = 0; // 0 means the 1st/only one
    //TODO: add depth testing


    if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &graphicsPipeline) != VK_SUCCESS) {
        std::cerr << "Failed to create graphics pipeline\n";
        return -1;
    }

    // shader modules are only needed to build the pipeline
    vkDestroyShaderModule(device, vertShaderModule, nullptr);
    vkDestroyShaderModule(device, fragShaderModule, nullptr);
    std::cout << "Graphics pipeline created\n";

    return 0;
}

void GetGraphicsQueue() {
    // get a handle to the graphics queue so we can submit commands to it
    // queueIndex 0 is the first (and only) queue we requested
    vkGetDeviceQueue(device, graphicsQueueFamilyIndex, 0, &graphicsQueue);
    std::cout << "Device and graphics queue created\n";
}

void Cleanup() {
    // we need to destroy the vulkan stuff in the reverse order they were created
    // this is beacuse they depend on each other
    vkDestroySemaphore(device, imageAvailableSemaphore, nullptr);
    vkDestroyFence(device, inFlightFence, nullptr);
    for (auto semaphore : renderFinishedSemaphores) {
        vkDestroySemaphore(device, semaphore, nullptr);
    }
    vkDestroyCommandPool(device, commandPool, nullptr); // also frees the command buffer allocated from it
    vkDestroyBuffer(device, indexBuffer, nullptr);
    vkFreeMemory(device, indexBufferMemory, nullptr);
    vkDestroyBuffer(device, vertexBuffer, nullptr);
    vkFreeMemory(device, vertexBufferMemory, nullptr);
    /*
    for (auto framebuffer : swapchainFramebuffers) {
        vkDestroyFramebuffer(device, framebuffer, nullptr);
    }
    */
    vkDestroyPipeline(device, graphicsPipeline, nullptr);
    vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
    vkDestroyRenderPass(device, renderPass, nullptr);
    /*
    for (auto imageView : swapchainImageViews) {
        vkDestroyImageView(device, imageView, nullptr);
    }
    */
    CleanupSwapchain();
    vkDestroySurfaceKHR(instance, surface, nullptr);
    vkDestroyDescriptorPool(device, descriptorPool, nullptr); // also frees the descriptor set allocated from it
    vkDestroyDescriptorSetLayout(device, descriptorSetLayout, nullptr);
    vkDestroyBuffer(device, uniformBuffer, nullptr);
    vkFreeMemory(device, uniformBufferMemory, nullptr);
    vkDestroyDevice(device, nullptr);
    vkDestroyInstance(instance, nullptr);
    glfwDestroyWindow(window);
    glfwTerminate();
}









int CreateFramebuffers() {
    // a framebuffer links the render pass to the actual images to render into
    // there is one per swapchain image view
    swapchainFramebuffers.resize(swapchainImageViews.size());
    for (size_t i = 0; i < swapchainImageViews.size(); i++) {
        VkImageView attachments[] = {
            swapchainImageViews[i]
        };

        VkFramebufferCreateInfo framebufferInfo{};
        framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        framebufferInfo.renderPass = renderPass; // render pass this frame buffer will use. must match layout and be compatable
        framebufferInfo.attachmentCount = 1; // must match attachment count declared in the render pass
        framebufferInfo.pAttachments = attachments; // pointer to array of VkImageView handels. One per attachment slot defined in the render pass
        framebufferInfo.width = capabilities.currentExtent.width;
        framebufferInfo.height = capabilities.currentExtent.height;
        framebufferInfo.layers = 1; // only need a single 2d render target

        if (vkCreateFramebuffer(device, &framebufferInfo, nullptr, &swapchainFramebuffers[i]) != VK_SUCCESS) {
            std::cerr << "Failed to create framebuffer " << i << "\n";
            return -1;
        }
    }

    std::cout << "Created " << swapchainFramebuffers.size() << " framebuffers\n";
    return 0;
}

int CreateCommandBuffer() {
    // allocate a single command buffer from the pool
    // it gets re recorded every frame
    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = commandPool;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY; // can be submitted directly to a queue
    allocInfo.commandBufferCount = 1;

    if (vkAllocateCommandBuffers(device, &allocInfo, &commandBuffer) != VK_SUCCESS) {
        std::cerr << "Failed to allocate command buffer\n";
        return -1;
    }

    std::cout << "Command buffer allocated\n";
    return 0;
}

int CreateCommandPool() {
    // command buffers are allocated from a command pool
    // the pool is tied to a specific queue family - ours is the graphics family
    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT; // allow re-recording buffers individually
    poolInfo.queueFamilyIndex = graphicsQueueFamilyIndex;

    if (vkCreateCommandPool(device, &poolInfo, nullptr, &commandPool) != VK_SUCCESS) {
        std::cerr << "Failed to create command pool\n";
        return -1;
    }

    std::cout << "Command pool created\n";
    return 0;
}

// needed to coordinate work between cpu and gpu beacsue they run in parrellel and at very different speeds
int CreateSyncObjects() {
    // semaphores sync gpu to gpu work (wait until image is available before drawing)
    VkSemaphoreCreateInfo semaphoreInfo{};
    semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    // fences sync gpu to cpu work (cpu wait until the gpu is done for this frame)
    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    // signaled and unsignaled means done or not
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT; // start signaled so the first frame doesn't wait forever

    if (vkCreateSemaphore(device, &semaphoreInfo, nullptr, &imageAvailableSemaphore) != VK_SUCCESS ||
        vkCreateFence(device, &fenceInfo, nullptr, &inFlightFence) != VK_SUCCESS) {
        std::cerr << "Failed to create sync objects\n";
        return -1;
    }

    // one semaphore per swapchain image to say when render is finsihed
    // cos each image might be presented in a different order than it was submitted
    renderFinishedSemaphores.resize(swapchainImages.size());
    for (size_t i = 0; i < renderFinishedSemaphores.size(); i++) {
        if (vkCreateSemaphore(device, &semaphoreInfo, nullptr, &renderFinishedSemaphores[i]) != VK_SUCCESS) {
            std::cerr << "Failed to create render finished semaphore " << i << "\n";
            return -1;
        }
    }

    std::cout << "Sync objects created\n";
    return 0;
}

// records the draw commands into the command buffer, targeting a specific framebuffer
void RecordCommandBuffer(VkCommandBuffer cmdBuffer, uint32_t imageIndex) {
    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;

    // puts the command buffer into recording state
    // now can do vkCmd* fucntions on it
    vkBeginCommandBuffer(cmdBuffer, &beginInfo);

    // what value to reset an attachment to at the start of a render pass
    // when the attachments loadOp is VK_ATTACHMENT_LOAD_OP_CLEAR
    VkClearValue clearColor = { {{0.0f, 0.0f, 0.0f, 1.0f}} }; // clear to black

    VkRenderPassBeginInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    renderPassInfo.renderPass = renderPass;
    renderPassInfo.framebuffer = swapchainFramebuffers[imageIndex];
    renderPassInfo.renderArea.offset = { 0, 0 }; // start at top left corner of frame buffer
    renderPassInfo.renderArea.extent = capabilities.currentExtent; // match width and height (full spawchain size)
    // TODO add depth attachment
    renderPassInfo.clearValueCount = 1; // right now only sinlge color attachment
    renderPassInfo.pClearValues = &clearColor; // default starting color

    // start render pass
    // VK_SUBPASS_CONTENTS_INLINE means to record comands for this subpass directly here instead of secondard command buffer
    vkCmdBeginRenderPass(cmdBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

    // binds the previously creaAted graphics pipline (vertex/frag shaders, rasterizer, blend state, viewport ect) as the active pipline for the following draw calls
    // need VK_PIPELINE_BIND_POINT_GRAPHICS cos this isnt a compute shader
    vkCmdBindPipeline(cmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, graphicsPipeline);

    // binds our uniform buffer's descriptor set so the shader can access ubo.model on the upcoming draw call
    vkCmdBindDescriptorSets(cmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &descriptorSet, 0, nullptr);

    VkBuffer vertexBuffers[] = { vertexBuffer };
    VkDeviceSize offsets[] = { 0 };
    vkCmdBindVertexBuffers(cmdBuffer, 0, 1, vertexBuffers, offsets);
    vkCmdBindIndexBuffer(cmdBuffer, indexBuffer, 0, VK_INDEX_TYPE_UINT16);

    vkCmdDrawIndexed(cmdBuffer, static_cast<uint32_t>(indices.size()), 1, 0, 0, 0);

    vkCmdEndRenderPass(cmdBuffer); // finishes subpass 0 tehn does automatic layout transiton to finalLayout (COLOR_ATTACHMENT_OPTIMAL to PRESENT_SRC_KHR)
    vkEndCommandBuffer(cmdBuffer); // stop recording

    /*
    if (vkEndCommandBuffer(cmdBuffer) != VK_SUCCESS) {
        throw std::runtime_error("Failed to record command buffer");
    }
    */
}

uint32_t FindMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties) {
    // ask the gpu what memory types it has and their properties
    VkPhysicalDeviceMemoryProperties memProperties;
    vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memProperties);

    for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++) {
        // typeFilter is a bitmask of memory types the buffer can use
        // we also need the memory type to have all the properties we asked for (like HOST_VISABLE)
        bool typeAllowed = typeFilter & (1 << i);
        bool hasProperties = (memProperties.memoryTypes[i].propertyFlags & properties) == properties;

        if (typeAllowed && hasProperties) {
            return i;
        }
    }

    throw std::runtime_error("Failed to find suitable memry type");
}

void CreateBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties, VkBuffer& buffer, VkDeviceMemory& bufferMemory) {
    /*
    Note for myself:
    allocating and uploading data to a gpu buffer

    1. create buffer
    define its size (in bytes) and its useage (vertex buffer, index buffer or uniform buffer)
    Nothing has been alocated yet

    2. find memory type
    gpus have differnt types of memory heaps (vram or system ram accesiable by the gpu)
    it must meet buffer requirments the memory must support the specific buffer usage created
    and property flags, choose whether the memory needs to be host visable (cpu can write directly) or device local (fast but gpu only)

    3. allocate
    request a chunck of physical memory from the gpus memory mannager\

    4. bind
    connect the logical buffer object to physical gpu memoy
    assocates the virtual hande;l the code uses to hardware addresses on the gpu

    5. copy data
    move the raw data (might be 3d mesh verties or textures) form cpu ram into the allocated gpu memory.
    direct mapping (staging / host visable) - map the gpu memory to a cpu pointer and use memcpy to copy data directly
    staging buffer transfer (device local) - copy data to a temporary staging buffer first then give a command to the gpu to copy it internally from staging into fast device local buffer
    */

    // descripbe and create buffer
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = size;
    bufferInfo.usage = usage; // eg vertex byffer / index buffer
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE; // only accessed by one queue family at a time

    if (vkCreateBuffer(device, &bufferInfo, nullptr, &buffer) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create buffer");
    }

    // ask what memory requreiments this specific buffer has
    VkMemoryRequirements memRequirments;
    vkGetBufferMemoryRequirements(device, buffer, &memRequirments);

    // allocate memory of the right type and size
    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirments.size;
    allocInfo.memoryTypeIndex = FindMemoryType(memRequirments.memoryTypeBits, properties);

    if (vkAllocateMemory(device, &allocInfo, nullptr, &bufferMemory) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate buffer memory");
    }

    // bind the memory to the buffer
    // offset 0 means use it from the start
    vkBindBufferMemory(device, buffer, bufferMemory, 0);
}

void CreateVertexBuffer() {
    VkDeviceSize bufferSize = sizeof(vertices[0]) * vertices.size();

    // VK_BUFFER_USAGE_VERTEX_BUFFER_BIT - this buffer will hold vertex data used in draw calls
    // VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT - cpu can map this memory and write to it directly
    // VK_MEMORY_PROPERTY_HOST_COHERENT_BIT - cpu writes are automatically visible to the GPU, no manual flush needed
    CreateBuffer(bufferSize,VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, vertexBuffer, vertexBufferMemory);

    void* data;
    vkMapMemory(device, vertexBufferMemory, 0, bufferSize, 0, &data); // get a cpu pointer to the gpu memory
    memcpy(data, vertices.data(), (size_t)bufferSize); // standard c++ fucntion
    vkUnmapMemory(device, vertexBufferMemory); // must unmap when done with cpu access

    std::cout << "Vertex buffer created\n";
}



void CreateUniformBuffer() {
    VkDeviceSize bufferSize = sizeof(UniformBufferObject);

    CreateBuffer(bufferSize, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, uniformBuffer, uniformBufferMemory);

    // map it once and leave it mapped
    // write new data into it every frame
    vkMapMemory(device, uniformBufferMemory, 0, bufferSize, 0, &uniformBufferMapped);

    std::cout << "Uniform buffer created\n";
}





// Create a descriptor pool and allocate one descriptor set from it
int CreateDescriptorPoolAndSet() {
    VkDescriptorPoolSize poolSize{};
    poolSize.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER; // this pool needs to supply uniform buffer type descriptors
    poolSize.descriptorCount = 1; // we only need one

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = 1; // one VkDescriptorPoolSize entry
    poolInfo.pPoolSizes = &poolSize; // that entry
    poolInfo.maxSets = 1; // total number of desciptior SETS this pool can allocate (only want 1)

    if (vkCreateDescriptorPool(device, &poolInfo, nullptr, &descriptorPool) != VK_SUCCESS) {
        std::cerr << "Failed to create descriptor pool\n";
        return -1;
    }

    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = descriptorPool; // allocate from this pool
    allocInfo.descriptorSetCount = 1; // allocate 1 set
    allocInfo.pSetLayouts = &descriptorSetLayout;

    if (vkAllocateDescriptorSets(device, &allocInfo, &descriptorSet) != VK_SUCCESS) {
        std::cerr << "Failed to allocate descriptor set\n";
        return -1;
    }

    // point the descriptor set at the uniform buffer
    VkDescriptorBufferInfo bufferInfo{};
    bufferInfo.buffer = uniformBuffer;
    bufferInfo.offset = 0; // start at byte 0
    bufferInfo.range = sizeof(UniformBufferObject); // how many btes this descriptor covers

    // stup uniform buffer to specifc binding slot in the descriptor set
    VkWriteDescriptorSet descriptorWrite{};
    descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptorWrite.dstSet = descriptorSet; // which descriptor set to write into
    descriptorWrite.dstBinding = 0; // which binding within that set. matches shaders binding = 0
    descriptorWrite.dstArrayElement = 0; // which array element, if this binding were an array (it's not, so 0)
    descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER; // must match the layouts declared type at this binding
    descriptorWrite.descriptorCount = 1; // writing 1 descriptor
    descriptorWrite.pBufferInfo = &bufferInfo; // the actual buffer info from above

    vkUpdateDescriptorSets(device, 1, &descriptorWrite, 0, nullptr);

    std::cout << "Descriptor pool and set created\n";
    return 0;
}


void CreateIndexBuffer() { // basicly the same as for the vertex buffer
    VkDeviceSize bufferSize = sizeof(indices[0]) * indices.size();

    CreateBuffer(bufferSize, VK_BUFFER_USAGE_INDEX_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, indexBuffer, indexBufferMemory);

    void* data;
    vkMapMemory(device, indexBufferMemory, 0, bufferSize, 0, &data);
    memcpy(data, indices.data(), (size_t)bufferSize);
    vkUnmapMemory(device, indexBufferMemory);

    std::cout << "Index buffer created\n";
}

void CleanupSwapchain() {
    // destroy everything that depends on the swapchain/window size
    for (auto framebuffer : swapchainFramebuffers) {
        vkDestroyFramebuffer(device, framebuffer, nullptr);
    }
    for (auto imageView : swapchainImageViews) {
        vkDestroyImageView(device, imageView, nullptr);
    }
    vkDestroySwapchainKHR(device, swapchain, nullptr);
}

void RecreateSwapchain() {
    std::cout << "\nRecreating Swapchain...\n";
    // handle minimization, the windoww size becomes 0x0 and Vulkan dosnt allow a 0x0 swapchain
    // so just wait until the window has a real size again
    int width = 0, height = 0;
    glfwGetFramebufferSize(window, &width, &height);
    while (width == 0 || height == 0) {
        glfwGetFramebufferSize(window, &width, &height);
        glfwWaitEvents(); // sleep until something happens
    }

    // wait for the gpu to finish using the old swapchain before destroying it
    vkDeviceWaitIdle(device);

    CleanupSwapchain();

    // fetch capabilities again since the window size (and possibly other properties) changed
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDevice, surface, &capabilities);

    CreateSwapchain();
    CreateImageViews();
    CreateFramebuffers();
}

void UpdateUniformBuffer() {
    static auto startTime = std::chrono::high_resolution_clock::now(); // will be skipped after initilization

    auto currentTime = std::chrono::high_resolution_clock::now();
    float time = std::chrono::duration<float>(currentTime - startTime).count(); // seconds since start

    UniformBufferObject ubo{};
    // rotate around the Z axis cos this is in 2d at a constant speed
    ubo.model = glm::rotate(glm::mat4(1.0f), time * glm::radians(90.0f), glm::vec3(0.0f, 0.0f, 1.0f));

    memcpy(uniformBufferMapped, &ubo, sizeof(ubo)); // already mapped, so just overwrite directly
}


void DrawFrame() {
    // will block the main thread until vkWaitForFence returns
    // VK_TRUE means wait until ALL are signaled
    // UINT64_MAX is timeout in nanosceonds. in this case we are waiting 600 years :)
    vkWaitForFences(device, 1, &inFlightFence, VK_TRUE, UINT64_MAX);


    UpdateUniformBuffer(); // recompute rotation for this frame

    // grab the next available image from the swapchain
    // so we know which framebuffer (swapchainFramebuffers[imageIndex]) is safe to draw into
    uint32_t imageIndex;
    VkResult acquireResult = vkAcquireNextImageKHR(device, swapchain, UINT64_MAX, imageAvailableSemaphore, VK_NULL_HANDLE, &imageIndex);

    if (acquireResult == VK_ERROR_OUT_OF_DATE_KHR) {
        // swapchain no longer matches the surface
        // so rebuild it and skip this frame
        RecreateSwapchain();
        return;
    }

    vkResetFences(device, 1, &inFlightFence); // change back to unsignaled state

    // reset command buffer to initial state so it can be re-recorded
    vkResetCommandBuffer(commandBuffer, 0);
    RecordCommandBuffer(commandBuffer, imageIndex); // record the next frame

    // tells the gpu to not start executing this submission until imageAvailableSemaphore is signaled
    VkSemaphore waitSemaphores[] = { imageAvailableSemaphore };
    VkPipelineStageFlags waitStages[] = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT }; // wait when it reaches color output stage. can overlap vertex work with waiting for image
    VkSemaphore signalSemaphores[] = { renderFinishedSemaphores[imageIndex] }; // what semaphore to signal wwhen all commands in this specific submission finish execution. use the one tied to THIS iamge


    // submits the work to the gpu queue
    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.waitSemaphoreCount = 1; 
    submitInfo.pWaitSemaphores = waitSemaphores;// presentation waits on this before displaying
    submitInfo.pWaitDstStageMask = waitStages;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffer;
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = signalSemaphores; // signal when drawing is done

    // sumbit the work to the GPu queue
    vkQueueSubmit(graphicsQueue, 1, &submitInfo, inFlightFence);

    VkPresentInfoKHR presentInfo{};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = signalSemaphores; // wait for drawing to finish before presenting
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = &swapchain; // present this swapchin
    presentInfo.pImageIndices = &imageIndex; // the aquired image

    // request the presentation engine to display this image
    VkResult presentResult = vkQueuePresentKHR(graphicsQueue, &presentInfo);

    if (presentResult == VK_ERROR_OUT_OF_DATE_KHR || presentResult == VK_SUBOPTIMAL_KHR) {
        // suboptimal means it still worked but isn't ideal (size mismatch)
        // rebuild for next frame
        RecreateSwapchain();
    }
}

void MainLoop() {
    // keep window open
    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();
        DrawFrame();
    }

    // wait for the gpu to finish everything before we start destroying stuff in cleanup
    vkDeviceWaitIdle(device);
}

int main() {
    // if any of these return a non zero vlaue, there was an error
    if (InitWindow() != 0) return -1;
    if (CreateInstance() != 0) return -1;
    if (CreateSurface() != 0) return -1;
    if (PickPhysicalDevice() != 0) return -1;
    if (CreateLogicalDevice() != 0) return -1;
    if (CreateSwapchain() != 0) return -1;
    if (CreateImageViews() != 0) return -1;
    if (CreateRenderPass() != 0) return -1;
    if (CreateDescriptorSetLayout() != 0) return -1;
    if (CreateGraphicsPipeline() != 0) return -1;
    GetGraphicsQueue();
    if (CreateFramebuffers() != 0) return -1;
    if (CreateCommandPool() != 0) return -1;
    if (CreateCommandBuffer() != 0) return -1;
    if (CreateSyncObjects() != 0) return -1;
    CreateVertexBuffer();
    CreateIndexBuffer();
    CreateUniformBuffer();
    if (CreateDescriptorPoolAndSet() != 0) return -1;

    MainLoop();

    Cleanup();
    return 0;
}

