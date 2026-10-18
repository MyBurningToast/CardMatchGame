#include "Renderer.h"

#include <iostream>
#include <cstring>
#include <fstream>

const std::vector<const char*> validationLayers = {
    "VK_LAYER_KHRONOS_validation" // comes with vulkan SDK
};

// vulkan's validation layers help find errors by intercepting api calls.
// Without it, the gpu will just crash with no error info
#ifdef NDEBUG
const bool enableValidationLayers = false; // off in release builds
#else
const bool enableValidationLayers = true; // on in debug builds
#endif

/*
Note for myself:
(push constants are just small but fast ubos. vulkan guarantees 128 bytes)

uniform buffers and MVP (model view projection)

A ubo is data that is the same for every vertex/fragment in a draw call
used for stuff like model/view/projection, light position or a time value
things the draw call need "uniformly" or global

the MVP idea
model matrix - moves/rotates/scales the mesh from its own local space into world space
view matrix - moves the world so it lines up with where the camera is looking (inverse of camera's transform?)
projection matrix - convert 3d space into the clip space range vulkan expects also handles fov/aspect/perspective
final vertex pos - projection * view * model * vertexPos (done in vertex shader)
multiplying all 3 together on the cpu before upload as one matrix saves the shader from doing 3 matrix multiplications per vertex


how to use ubo:

define the struct (cpu side)
matches the layout the shader expects like glm::mat4 model; (or model/view/proj all three)
make sure it follows alignment rules (std140). some types need padding (i'm pretty sure glm has alignas helpers for this, need to check)

create the buffer
same as CreateBuffer() flow but usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT instead of vertex or index
memory props HOST_VISIBLE | HOST_COHERENT cos gets rewritten every frame anyways

persistent map (different from vertex buffer)
since this changes every frame, map once at creation and keep the pointer and memcpy new matrix data into it each frame without map and unmap (pretty sure that has overhead)

descriptor set layout
tell the pipeline to expect a uniform buffer at binding X - VkDescriptorSetLayout
done only at pipeline creation time

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

// in Vulkan's NDC (Normalized Device Coordinates)/screen space, positive Y points downward
const std::vector<Vertex> vertices = {
    { { -0.5f, -0.5f },{ 0.0f, 0.0f } }, // top left
    { { 0.5f, -0.5f },{ 1.0f, 0.0f } }, // top right
    { { 0.5f,  0.5f },{ 1.0f, 1.0f } }, // bottom right
    { { -0.5f,  0.5f },{ 0.0f, 1.0f } }, // bottom left
};

// index buffer data
const std::vector<uint16_t> indices = {
    0, 1, 2,
    2, 3, 0
};

// reads the compiled shader from disk, copies it to ram, then will give that address to vulkan
static std::vector<char> ReadFile(const std::string& filename) {

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

static VkShaderModule CreateShaderModule(VkDevice device, const std::vector<char>& code) {
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

bool Renderer::CheckValidationLayerSupport(const std::vector<const char*>& validationLayers) {
    uint32_t layerCount = 0;
    vkEnumerateInstanceLayerProperties(&layerCount, nullptr);
    std::vector<VkLayerProperties> availableLayers(layerCount);
    vkEnumerateInstanceLayerProperties(&layerCount, availableLayers.data());

    // for every layer, check it exists somewhere in the available list
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

int Renderer::InitWindow() {
    // check glfw initializes
    if (!glfwInit()) {
        std::cerr << "Failed to init GLFW\n";
        return -1; // in c++ a non zero return value is considered an error
    }

    if (enableValidationLayers && !CheckValidationLayerSupport(validationLayers)) {
        std::cerr << "Validation layers requested but not available\n";
        return -1;
    }

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API); // specify no OpenGL context because i'm using Vulkan
    glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE); // fixed size window, so the swapchain never needs rebuilding
    window = glfwCreateWindow(800, 800, "Test", nullptr, nullptr);
    if (!window) {
        std::cerr << "Failed to create GLFW window\n";
        glfwTerminate();
        return -1;
    }

    return 0;
}

int Renderer::CreateInstance() {
    // Vulkan check
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

int Renderer::CreateSurface() {
    // to draw stuff to the screen we need a bridge between vulkan and the glfw window
    VkResult surfaceResult = glfwCreateWindowSurface(instance, window, nullptr, &surface); // glfw can do this automatically based on operating system & graphics api
    if (surfaceResult != VK_SUCCESS) {
        std::cerr << "Failed to create window surface\n";
        return -1;

    }
    std::cout << "Window surface created\n";

    return 0;
}

int Renderer::PickPhysicalDevice() {
    // Pick a physical device. Someone might have multiple GPUs
    uint32_t deviceCount = 0; // Vulkan uses explicit, fixed width integer types. A normal int's size is compiler dependent
    vkEnumeratePhysicalDevices(instance, &deviceCount, nullptr); // vulkan will count all the gpus that support it
    if (deviceCount == 0) { // couldn't find a compatible gpu
        std::cerr << "No GPU with Vulkan support\n";
        return -1;
    }

    std::vector<VkPhysicalDevice> devices(deviceCount);
    vkEnumeratePhysicalDevices(instance, &deviceCount, devices.data()); // .data because vulkan expects c style pointers

    // null handle is the vulkan defined null value for handles
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

            // queueFlags is a bitmask (a single int representing different capabilities)
            // that's why we need to check with bitwise & operator instead of ==
            bool hasGraphics = queueFamilies[i].queueFlags & VK_QUEUE_GRAPHICS_BIT; // VK_QUEUE_GRAPHICS_BIT means it supports graphics commands

            // Even if the gpu can do graphics commands it doesn't necessarily mean it can present the images to our surface
            VkBool32 presentSupport = false;
            vkGetPhysicalDeviceSurfaceSupportKHR(device, i, surface, &presentSupport);

            if (hasGraphics && presentSupport) {
                physicalDevice = device;
                graphicsQueueFamilyIndex = i; // save what index the queue family for graphics is
                break;
            }
        }

        // for now this is just getting the first compatible gpu with a graphics family
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

int Renderer::CreateLogicalDevice() {
    // create logical device. the logical device is the interface to the gpu
    // vulkan needs to know how many queues we want from the graphics queue family and what priority 0-1
    float queuePriority = 1.0f;

    // a queue is where we submit work for the gpu to execute
    VkDeviceQueueCreateInfo queueCreateInfo{};
    queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO; // sType tells vulkan what struct it has been given
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
    // automatically update the size. must cast to uint32_t because .size() returns a size_t
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

int Renderer::CreateSwapchain() {
    // create the swapchain (the images we render into and present)
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDevice, surface, &capabilities); // get info about the window

    // check what pixel formats the surface supports
    // surface format tells vulkan how the images in the swapchain should store their pixels
    // some windows/gpus can't necessarily display every pixel format
    uint32_t formatCount = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface, &formatCount, nullptr); // ask how many formats exist
    std::vector<VkSurfaceFormatKHR> formats(formatCount); // allocate enough space
    vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface, &formatCount, formats.data()); // fill that allocated array

    // just get the first available format, good enough for now
    surfaceFormat = formats[0];

    // how many images to put in the swapchain
    // I'm using double buffering here so that CPU can work on the next frame while the previous one is still being rendered
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
    swapchainCreateInfo.presentMode = VK_PRESENT_MODE_FIFO_KHR; // VSync. the vulkan spec guarantees this to be supported
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

int Renderer::CreateImageViews() {
    // create image view for each swapchain image
    // image views describe how to access an image. stuff like format and dimensions
    // This is because vulkan won't let you render into raw images
    swapchainImageViews.resize(swapchainImages.size());
    for (size_t i = 0; i < swapchainImages.size(); i++) {
        VkImageViewCreateInfo viewCreateInfo{};
        viewCreateInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewCreateInfo.image = swapchainImages[i]; // associated image
        viewCreateInfo.viewType = VK_IMAGE_VIEW_TYPE_2D; // normal 2d image
        viewCreateInfo.format = surfaceFormat.format; // must match the swapchain's format

        // swizzle lets you remap color channels, i'm not going to do that
        viewCreateInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
        viewCreateInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
        viewCreateInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
        viewCreateInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;

        // subresourceRange is which part of the image this view covers
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

int Renderer::CreateRenderPass() {
    // create the render pass
    // a render pass describes what happens to the images during a frame

    VkAttachmentDescription colorAttachment{}; // stored resource list for whole render pass
    colorAttachment.format = surfaceFormat.format; // match swapchain format
    colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT; // 1 sample per pixel

    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR; // clear the previous image with solid color
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE; // keep the rendered result so it can be presented

    colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE; // not using stencils
    colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;

    colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED; // don't care about previous image's layout
    colorAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR; // must be ready to present to the screen when done

    // a render pass is made of one or more subpasses
    // but we only need a single subpass that writes color to our only attachment

    VkAttachmentReference colorAttachmentRef{}; // stores info for a subpass
    colorAttachmentRef.attachment = 0; // index into attachment array
    colorAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL; // best layout for writing color during rendering

    VkSubpassDescription subpass{}; // TODO: where we will record draw commands
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS; // this is a graphics subpass (not compute)
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorAttachmentRef;

    // the swapchain might still be being presented from the previous frame
    // the gpu wants to write the next frame's colour into the same memory
    // dependency is a synchronisation rule for this render pass that waits until the image is safe to write to
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

int Renderer::CreateGraphicsPipeline() {
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

    // interpret vertex struct's raw bytes as shader input
    VkVertexInputBindingDescription bindingDescription{};
    bindingDescription.binding = 0; // this buffer's slot in the command state. pos and uv in this one buffer
    bindingDescription.stride = sizeof(Vertex); // how many bytes to skip to get to the next in the buffer
    bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX; // go to next vertex's data for every vertex processed

    // one description per field in the Vertex struct (pos and uv)
    VkVertexInputAttributeDescription attributeDescriptions[2]{};

    attributeDescriptions[0].binding = 0; // which binding (vertex buffer stream) this attribute's data comes from. matches the binding description
    attributeDescriptions[0].location = 0; // matches location = 0 in the vertex shader (inPosition)
    attributeDescriptions[0].format = VK_FORMAT_R32G32_SFLOAT; // vec2 of floats
    attributeDescriptions[0].offset = offsetof(Vertex, pos); // byte offset of the pos field within the vertex struct

    attributeDescriptions[1].binding = 0; // same buffer, just a different field in each vertex
    attributeDescriptions[1].location = 1; // matches location = 1 in the vertex shader (inUV)
    attributeDescriptions[1].format = VK_FORMAT_R32G32_SFLOAT; // vec2 of floats
    attributeDescriptions[1].offset = offsetof(Vertex, uv); // byte offset of the uv field within the vertex struct


    // combine binding and attribute descriptions and attaches them to the pipeline
    VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
    vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInputInfo.vertexBindingDescriptionCount = 1;
    vertexInputInfo.pVertexBindingDescriptions = &bindingDescription;
    vertexInputInfo.vertexAttributeDescriptionCount = 2; // pos and uv
    vertexInputInfo.pVertexAttributeDescriptions = attributeDescriptions;

    // input assembly is how vertices are grouped into shapes
    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST; // every 3 vertices makes 1 triangle
    inputAssembly.primitiveRestartEnable = VK_FALSE; // disable restarting primitive strips

    VkViewport viewport{};
    viewport.x = 0.0f; // no offset, start in top left
    viewport.y = 0.0f;
    viewport.width = (float)capabilities.currentExtent.width; // stretch NDC across the full width of window
    viewport.height = (float)capabilities.currentExtent.height; // NDC is Normalized Device Coordinates so (-1 to 1)
    viewport.minDepth = 0.0f; // write to depth buffer between 0 and 1
    viewport.maxDepth = 1.0f;

    VkRect2D scissor{}; // anything outside the scissor gets discarded, we want full image
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
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL; // fill triangles solid (not wireframe or points)
    rasterizer.lineWidth = 1.0f;
    rasterizer.cullMode = VK_CULL_MODE_NONE; // shade all triangles
    // TODO: don't shade triangles not facing the camera
    rasterizer.frontFace = VK_FRONT_FACE_CLOCKWISE; // winding order
    rasterizer.depthBiasEnable = VK_FALSE; // don't change depth values

    // i'll keep multisampling disabled for now
    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.sampleShadingEnable = VK_FALSE;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    // Color blending is how new pixels combine with what's already there. this is used for transparency
    VkPipelineColorBlendAttachmentState colorBlendAttachment{};
    colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT; // bitmask for what color channels get written to the framebuffer for this attachment
    colorBlendAttachment.blendEnable = VK_TRUE; // blend instead of overwriting
    // colour: final = src * srcAlpha + dst * (1 - srcAlpha), this is so the edge of letters is a soft fade
    colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA; // weight the new colour by its alpha
    colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA; // weight the existing colour by the remainder
    colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD; // add the two weighted colours
    // alpha: just keep the new alpha. the window is opaque and it doesn't matter
    colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
    colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;

    // global blend state for the pipeline (uses settings above)
    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.logicOpEnable = VK_FALSE; // i think it's only enabled for super niche blending math
    colorBlending.attachmentCount = 1; // only write to one color attachment
    colorBlending.pAttachments = &colorBlendAttachment;

    VkPushConstantRange pushConstantRange{};
    pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    pushConstantRange.offset = 0;
    pushConstantRange.size = sizeof(QuadInstance); // matrix + color + uvRect (96 bytes total), vulkan guarantees a minimum of 128 byte

    // pipeline layout defines how shaders access external resources (textures/buffers/push constants)
    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1; // the texture descriptor set layout
    pipelineLayoutInfo.pSetLayouts = &descriptorSetLayout;
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;


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
    // TODO: add depth testing


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

void Renderer::GetGraphicsQueue() {
    // get a handle to the graphics queue so we can submit commands to it
    // queueIndex 0 is the first (and only) queue we requested
    vkGetDeviceQueue(device, graphicsQueueFamilyIndex, 0, &graphicsQueue);
    std::cout << "Device and graphics queue created\n";
}

int Renderer::CreateFramebuffers() {
    // a framebuffer links the render pass to the actual images to render into
    // there is one per swapchain image view
    swapchainFramebuffers.resize(swapchainImageViews.size());
    for (size_t i = 0; i < swapchainImageViews.size(); i++) {
        VkImageView attachments[] = {
            swapchainImageViews[i]
        };

        VkFramebufferCreateInfo framebufferInfo{};
        framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        framebufferInfo.renderPass = renderPass; // render pass this frame buffer will use. must match layout and be compatible
        framebufferInfo.attachmentCount = 1; // must match attachment count declared in the render pass
        framebufferInfo.pAttachments = attachments; // pointer to array of VkImageView handles. One per attachment slot defined in the render pass
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

int Renderer::CreateCommandBuffer() {
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

int Renderer::CreateCommandPool() {
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

// needed to coordinate work between cpu and gpu because they run in parallel and at very different speeds
int Renderer::CreateSyncObjects() {
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

    // one semaphore per swapchain image to say when render is finished
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
void Renderer::RecordCommandBuffer(VkCommandBuffer cmdBuffer, uint32_t imageIndex, const std::vector<QuadInstance>& quads) {
    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;

    // puts the command buffer into recording state
    // now can do vkCmd* functions on it
    vkBeginCommandBuffer(cmdBuffer, &beginInfo);

    // what value to reset an attachment to at the start of a render pass
    // when the attachments loadOp is VK_ATTACHMENT_LOAD_OP_CLEAR
    VkClearValue clearColor = { { { 0.0f, 0.0f, 0.0f, 1.0f } } }; // clear to black

    VkRenderPassBeginInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    renderPassInfo.renderPass = renderPass;
    renderPassInfo.framebuffer = swapchainFramebuffers[imageIndex];
    renderPassInfo.renderArea.offset = { 0, 0 }; // start at top left corner of frame buffer
    renderPassInfo.renderArea.extent = capabilities.currentExtent; // match width and height (full swapchain size)
    // TODO add depth attachment
    renderPassInfo.clearValueCount = 1; // right now only single color attachment
    renderPassInfo.pClearValues = &clearColor; // default starting color

    // start render pass
    // VK_SUBPASS_CONTENTS_INLINE means to record commands for this subpass directly here instead of secondary command buffer
    vkCmdBeginRenderPass(cmdBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

    // binds the previously created graphics pipeline (vertex/frag shaders, rasterizer, blend state, viewport etc) as the active pipeline for the following draw calls
    // need VK_PIPELINE_BIND_POINT_GRAPHICS cos this isn't a compute shader
    vkCmdBindPipeline(cmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, graphicsPipeline);

    VkBuffer vertexBuffers[] = { vertexBuffer };
    VkDeviceSize offsets[] = { 0 };
    vkCmdBindVertexBuffers(cmdBuffer, 0, 1, vertexBuffers, offsets);
    vkCmdBindIndexBuffer(cmdBuffer, indexBuffer, 0, VK_INDEX_TYPE_UINT16);

    // bind the texture descriptor set once. every quad shares it, so it doesn't need rebinding per card
    vkCmdBindDescriptorSets(cmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &descriptorSet, 0, nullptr);

    // draw one quad per QuadInstance. push model matrix + color, then draw the shared quad mesh
    for (const auto& quad : quads) {
        // push constants are re sent before every draw. each quad gets its own matrix and color
        vkCmdPushConstants(cmdBuffer, pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(QuadInstance), &quad);
        vkCmdDrawIndexed(cmdBuffer, static_cast<uint32_t>(indices.size()), 1, 0, 0, 0);
    }


    vkCmdEndRenderPass(cmdBuffer); // finishes subpass 0 then does automatic layout transition to finalLayout (COLOR_ATTACHMENT_OPTIMAL to PRESENT_SRC_KHR)
    vkEndCommandBuffer(cmdBuffer); // stop recording

    /*
    if (vkEndCommandBuffer(cmdBuffer) != VK_SUCCESS) {
        throw std::runtime_error("Failed to record command buffer");
    }
    */
}

uint32_t Renderer::FindMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties) {
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

void Renderer::CreateBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties, VkBuffer& buffer, VkDeviceMemory& bufferMemory) {
    /*
    Note for myself:
    allocating and uploading data to a gpu buffer

    1. create buffer
    define its size (in bytes) and its usage (vertex buffer, index buffer or uniform buffer)
    Nothing has been allocated yet

    2. find memory type
    gpus have different types of memory heaps (vram or system ram accessible by the gpu)
    it must meet buffer requirements the memory must support the specific buffer usage created
    and property flags, choose whether the memory needs to be host visible (cpu can write directly) or device local (fast but gpu only)

    3. allocate
    request a chunk of physical memory from the gpu's memory manager

    4. bind
    connect the logical buffer object to physical gpu memory
    associates the virtual handle the code uses to hardware addresses on the gpu

    5. copy data
    move the raw data (might be 3d mesh vertices or textures) from cpu ram into the allocated gpu memory.
    direct mapping (staging / host visible) - map the gpu memory to a cpu pointer and use memcpy to copy data directly
    staging buffer transfer (device local) - copy data to a temporary staging buffer first then give a command to the gpu to copy it internally from staging into fast device local buffer
    */

    // describe and create buffer
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = size;
    bufferInfo.usage = usage; // eg vertex buffer / index buffer
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE; // only accessed by one queue family at a time

    if (vkCreateBuffer(device, &bufferInfo, nullptr, &buffer) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create buffer");
    }

    // ask what memory requirements this specific buffer has
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

void Renderer::CreateVertexBuffer() {
    VkDeviceSize bufferSize = sizeof(vertices[0]) * vertices.size();

    // VK_BUFFER_USAGE_VERTEX_BUFFER_BIT - this buffer will hold vertex data used in draw calls
    // VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT - cpu can map this memory and write to it directly
    // VK_MEMORY_PROPERTY_HOST_COHERENT_BIT - cpu writes are automatically visible to the GPU, no manual flush needed
    CreateBuffer(bufferSize, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, vertexBuffer, vertexBufferMemory);

    void* data;
    vkMapMemory(device, vertexBufferMemory, 0, bufferSize, 0, &data); // get a cpu pointer to the gpu memory
    memcpy(data, vertices.data(), (size_t)bufferSize); // standard c++ function
    vkUnmapMemory(device, vertexBufferMemory); // must unmap when done with cpu access

    std::cout << "Vertex buffer created\n";
}

// allocates a throwaway command buffer for some one off work (like uploading a texture) and starts recording
VkCommandBuffer Renderer::BeginSingleTimeCommands() {
    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY; // can be submitted directly to a queue
    allocInfo.commandPool = commandPool;
    allocInfo.commandBufferCount = 1;

    VkCommandBuffer cmd;
    vkAllocateCommandBuffers(device, &allocInfo, &cmd);

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT; // we only submit this once

    vkBeginCommandBuffer(cmd, &beginInfo);
    return cmd;
}

// stops recording, runs the commands, waits for the gpu to finish, then frees the command buffer
void Renderer::EndSingleTimeCommands(VkCommandBuffer cmd) {
    vkEndCommandBuffer(cmd);

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &cmd;

    vkQueueSubmit(graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE); // no fence, we just wait on the whole queue
    vkQueueWaitIdle(graphicsQueue); // cpu blocks until everything on this queue is done

    vkFreeCommandBuffers(device, commandPool, 1, &cmd);
}

// similar to CreateBuffer. it creates the image, find memory, allocate, bind
void Renderer::CreateImage(uint32_t width, uint32_t height, VkFormat format, VkImageUsageFlags usage, VkMemoryPropertyFlags properties, VkImage& image, VkDeviceMemory& imageMemory) {
    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D; // has width and height
    imageInfo.extent = { width, height, 1 }; // depth 1 because 2d image
    imageInfo.mipLevels = 1; // no mipmaps
    imageInfo.arrayLayers = 1; // not an array texture
    imageInfo.format = format;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL; // gpu picks the memory layout that's fastest to sample (but the cpu can't read it directly)
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED; // layout the image starts in. UNDEFINED means no valid contents yet
    imageInfo.usage = usage;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT; // no multisampling
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE; // only the graphics queue family uses it

    if (vkCreateImage(device, &imageInfo, nullptr, &image) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create image");
    }


    VkMemoryRequirements memRequirements;
    vkGetImageMemoryRequirements(device, image, &memRequirements); // ask the driver what memory this image needs

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = FindMemoryType(memRequirements.memoryTypeBits, properties); // memory type the image allows + has our properties

    if (vkAllocateMemory(device, &allocInfo, nullptr, &imageMemory) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate image memory");
    }

    vkBindImageMemory(device, image, imageMemory, 0);
}

// moves an image from one layout to another using a pipeline barrier
void Renderer::TransitionImageLayout(VkImage image, VkImageLayout oldLayout, VkImageLayout newLayout) {
    VkCommandBuffer cmd = BeginSingleTimeCommands();

    // a barrier says to finish the earlier work before the later work starts
    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = oldLayout; // layout the image is in now
    barrier.newLayout = newLayout; // layout to convert it to
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED; // no queue ownership transfer
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED; // both must be same if ignored
    barrier.image = image; // the image this barrier applies to
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT; // color data (not depth/stencil)
    barrier.subresourceRange.baseMipLevel = 0; // first mip level affected
    barrier.subresourceRange.levelCount = 1; // how many mip levels (just one)
    barrier.subresourceRange.baseArrayLayer = 0; // first array layer affected
    barrier.subresourceRange.layerCount = 1; // how many layers (not an array texture)

    VkPipelineStageFlags sourceStage; // pipeline stage that must finish before the barrier (what we wait on)
    VkPipelineStageFlags destinationStage; // pipeline stage that must wait for the barrier (what's blocked until then)

    if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
        // nothing to wait for, but the copy must not start until the layout change is done
        barrier.srcAccessMask = 0; // no earlier writes to make visible
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT; // the copy will write, so make the layout change visible to it
        sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT; // wait on nothing (very start of the pipe)
        destinationStage = VK_PIPELINE_STAGE_TRANSFER_BIT; // the copy stage is what gets blocked
    }
    else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
        // the copy must finish writing before the fragment shader is allowed to read
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT; // the copy's writes must be flushed/made available
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT; // so shader reads see the data
        sourceStage = VK_PIPELINE_STAGE_TRANSFER_BIT; // wait for copy to finish
        destinationStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT; // block fragment shader until then
    }
    else {
        throw std::runtime_error("Unsupported layout transition");
    }

    vkCmdPipelineBarrier(cmd, sourceStage, destinationStage, 0, 0, nullptr, 0, nullptr, 1, &barrier);

    EndSingleTimeCommands(cmd);
}

int Renderer::CreateTexture(const std::vector<uint8_t>& pixels, uint32_t texWidth, uint32_t texHeight) {
    // pixels is the font atlas
    // grayscale, one byte per pixel, so the total size in bytes equals the pixel count
    VkDeviceSize imageSize = pixels.size();

    // staging buffer is cpu visible memory that the gpu copies from
    VkBuffer stagingBuffer;
    VkDeviceMemory stagingBufferMemory;
    CreateBuffer(imageSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, stagingBuffer, stagingBufferMemory);

    void* data; // cpu pointer to gpu memory
    // map is a cpu side virtual pointer into gpu memory
    vkMapMemory(device, stagingBufferMemory, 0, imageSize, 0, &data); // map so cpu can access it via data
    memcpy(data, pixels.data(), (size_t)imageSize); // copy pixel data from cpu ram into mapped memory
    vkUnmapMemory(device, stagingBufferMemory); // done writing, release the mapping

    // the real image, in fast gpu only memory
    // R8_UNORM is a single 8 bit channel, read by the shader as a 0.0 - 1.0 value
    // TRANSFER_DST means we copy into it. SAMPLED means shaders read from it
    CreateImage(texWidth, texHeight, VK_FORMAT_R8_UNORM, VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, textureImage, textureImageMemory);

    // get the image ready to receive the copy
    TransitionImageLayout(textureImage, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

    // copy the staging buffer into the image
    VkCommandBuffer cmd = BeginSingleTimeCommands();

    // describes one buffer to image copy
    // which bytes of the buffer go to which part of the image
    VkBufferImageCopy region{};
    region.bufferOffset = 0; // start at the beginning of the buffer
    region.bufferRowLength = 0; // 0 means the pixels are tightly packed (row length = imageExtent.width)
    region.bufferImageHeight = 0;
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;  // copy into the color data
    region.imageSubresource.mipLevel = 0; // destination mipmap (base level)
    region.imageSubresource.baseArrayLayer = 0; // final destination array layer
    region.imageSubresource.layerCount = 1; // number of layers to copy
    region.imageOffset = { 0, 0, 0 }; // top left corner in the image to start writing at
    region.imageExtent = { texWidth, texHeight, 1 }; // size of the region to write (full image and depth 1 for 2d)

    vkCmdCopyBufferToImage(cmd, stagingBuffer, textureImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
    EndSingleTimeCommands(cmd);

    // get the image ready for shaders to read
    TransitionImageLayout(textureImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    // the staging buffer did its job
    vkDestroyBuffer(device, stagingBuffer, nullptr);
    vkFreeMemory(device, stagingBufferMemory, nullptr);

    // image view (similar to the swapchain image views)
    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = textureImage;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = VK_FORMAT_R8_UNORM; // must match the image
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;

    if (vkCreateImageView(device, &viewInfo, nullptr, &textureImageView) != VK_SUCCESS) {
        std::cerr << "Failed to create texture image view\n";
        return -1;
    }

    std::cout << "Texture created\n";
    return 0;
}

int Renderer::CreateTextureSampler() {
    // a sampler describes how the shader reads from a texture
    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR; // when magnified. blend the nearest pixels (smooth letters)
    samplerInfo.minFilter = VK_FILTER_LINEAR; // same when shrunk
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE; // uv outside 0-1 repeats the edge pixel
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.anisotropyEnable = VK_FALSE; // gpu feature not enabled in CreateLogicalDeviece
    samplerInfo.maxAnisotropy = 1.0f;
    samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK; // color of out of range uvs (unused with clamp but still needs to be set)
    samplerInfo.unnormalizedCoordinates = VK_FALSE; // uvs are 0-1, not pixel coordinates
    samplerInfo.compareEnable = VK_FALSE; // only used for shadow maps
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST; // no mipmaps
    samplerInfo.minLod = 0.0f; // lowest mip level allowed, 0 means full resolution
    samplerInfo.maxLod = 0.0f; // highest mip level allowed, 0 means only the base level

    if (vkCreateSampler(device, &samplerInfo, nullptr, &textureSampler) != VK_SUCCESS) {
        std::cerr << "Failed to create texture sampler\n";
        return -1;
    }

    std::cout << "Texture sampler created\n";
    return 0;
}

int Renderer::CreateDescriptorSetLayout() {
    // tells the pipeline to expect an image + sampler at binding 0
    VkDescriptorSetLayoutBinding samplerBinding{};
    samplerBinding.binding = 0; // matches layout(binding = 0) in the fragment shader
    samplerBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; // an image and its sampler bundled together
    samplerBinding.descriptorCount = 1;
    samplerBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT; // only the fragment shader reads it

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = 1;
    layoutInfo.pBindings = &samplerBinding;

    if (vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &descriptorSetLayout) != VK_SUCCESS) {
        std::cerr << "Failed to create descriptor set layout\n";
        return -1;
    }

    std::cout << "Descriptor set layout created\n";
    return 0;
}

int Renderer::CreateDescriptorPoolAndSet() {
    // the pool needs to know what types of descriptors it will hand out and how many
    VkDescriptorPoolSize poolSize{};
    poolSize.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSize.descriptorCount = 1;

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes = &poolSize;
    poolInfo.maxSets = 1; // one set is enough, every quad shares the same texture

    if (vkCreateDescriptorPool(device, &poolInfo, nullptr, &descriptorPool) != VK_SUCCESS) {
        std::cerr << "Failed to create descriptor pool\n";
        return -1;
    }

    // allocate one set from the pool, using our layout
    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = descriptorPool;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &descriptorSetLayout;

    if (vkAllocateDescriptorSets(device, &allocInfo, &descriptorSet) != VK_SUCCESS) {
        std::cerr << "Failed to allocate descriptor set\n";
        return -1;
    }

    // the set exists but is empty. point binding 0 to the actual image view + sampler
    VkDescriptorImageInfo imageInfo{};
    imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL; // the layout the image is in while the shader reads it (it was transitioned there in CreateTexture)
    imageInfo.imageView = textureImageView;
    imageInfo.sampler = textureSampler;

    VkWriteDescriptorSet descriptorWrite{};
    descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptorWrite.dstSet = descriptorSet;
    descriptorWrite.dstBinding = 0; // matches the layout binding
    descriptorWrite.dstArrayElement = 0;
    descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    descriptorWrite.descriptorCount = 1;
    descriptorWrite.pImageInfo = &imageInfo;

    vkUpdateDescriptorSets(device, 1, &descriptorWrite, 0, nullptr);

    std::cout << "Descriptor pool and set created\n";
    return 0;
}


void Renderer::CreateIndexBuffer() { // basically the same as for the vertex buffer
    VkDeviceSize bufferSize = sizeof(indices[0]) * indices.size();

    CreateBuffer(bufferSize, VK_BUFFER_USAGE_INDEX_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, indexBuffer, indexBufferMemory);

    void* data;
    vkMapMemory(device, indexBufferMemory, 0, bufferSize, 0, &data);
    memcpy(data, indices.data(), (size_t)bufferSize);
    vkUnmapMemory(device, indexBufferMemory);

    std::cout << "Index buffer created\n";
}

void Renderer::CleanupSwapchain() {
    // destroy everything that depends on the swapchain/window size
    for (auto framebuffer : swapchainFramebuffers) {
        vkDestroyFramebuffer(device, framebuffer, nullptr);
    }
    for (auto imageView : swapchainImageViews) {
        vkDestroyImageView(device, imageView, nullptr);
    }
    vkDestroySwapchainKHR(device, swapchain, nullptr);
}

int Renderer::Init(const std::vector<uint8_t>& texturePixels, uint32_t texWidth, uint32_t texHeight) {
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

    if (CreateTexture(texturePixels, texWidth, texHeight) != 0) return -1;
    if (CreateTextureSampler() != 0) return -1;
    if (CreateDescriptorPoolAndSet() != 0) return -1;

    return 0;
}


void Renderer::GetCursorPos(double& x, double& y) {
    glfwGetCursorPos(window, &x, &y);
}

void Renderer::GetFramebufferSize(int& width, int& height) {
    glfwGetFramebufferSize(window, &width, &height);
}

bool Renderer::IsMouseButtonPressed(int button) {
    return glfwGetMouseButton(window, button) == GLFW_PRESS;
}

bool Renderer::IsKeyPressed(int key) {
    return glfwGetKey(window, key) == GLFW_PRESS;
}

void Renderer::RequestClose() {
    glfwSetWindowShouldClose(window, GLFW_TRUE);
}

double Renderer::GetTime() {
    return glfwGetTime(); // seconds counting up from when glfwInit() was called
}

void Renderer::DrawFrame(const std::vector<QuadInstance>& quads) {
    // will block the main thread until vkWaitForFence returns
    // VK_TRUE means wait until ALL are signaled
    // UINT64_MAX is timeout in nanoseconds. in this case we are waiting 600 years :)
    vkWaitForFences(device, 1, &inFlightFence, VK_TRUE, UINT64_MAX);



    // grab the next available image from the swapchain
    // so we know which framebuffer (swapchainFramebuffers[imageIndex]) is safe to draw into
    uint32_t imageIndex;
    VkResult acquireResult = vkAcquireNextImageKHR(device, swapchain, UINT64_MAX, imageAvailableSemaphore, VK_NULL_HANDLE, &imageIndex);

    if (acquireResult == VK_ERROR_OUT_OF_DATE_KHR) {
        // the window is a fixed size so this should not happen (maybe while minimised)
        // there is nothing to rebuild, so just skip this frame
        return;
    }

    vkResetFences(device, 1, &inFlightFence); // change back to unsignaled state

    // reset command buffer to initial state so it can be re-recorded
    vkResetCommandBuffer(commandBuffer, 0);
    RecordCommandBuffer(commandBuffer, imageIndex, quads); // record the next frame

    // tells the gpu to not start executing this submission until imageAvailableSemaphore is signaled
    VkSemaphore waitSemaphores[] = { imageAvailableSemaphore };
    VkPipelineStageFlags waitStages[] = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT }; // wait when it reaches color output stage. can overlap vertex work with waiting for image
    VkSemaphore signalSemaphores[] = { renderFinishedSemaphores[imageIndex] }; // what semaphore to signal when all commands in this specific submission finish execution. use the one tied to THIS image


    // submits the work to the gpu queue
    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitSemaphores = waitSemaphores; // presentation waits on this before displaying
    submitInfo.pWaitDstStageMask = waitStages;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffer;
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = signalSemaphores; // signal when drawing is done

    // submit the work to the GPU queue
    vkQueueSubmit(graphicsQueue, 1, &submitInfo, inFlightFence);

    VkPresentInfoKHR presentInfo{};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = signalSemaphores; // wait for drawing to finish before presenting
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = &swapchain; // present this swapchain
    presentInfo.pImageIndices = &imageIndex; // the acquired image

    // request the presentation engine to display this image
    vkQueuePresentKHR(graphicsQueue, &presentInfo);
}

bool Renderer::WindowShouldClose() {
    return glfwWindowShouldClose(window);
}

void Renderer::PollEvents() {
    glfwPollEvents();
}

void Renderer::WaitIdle() {
    // wait for the gpu to finish everything before we start destroying stuff in cleanup
    vkDeviceWaitIdle(device);
}

void Renderer::Cleanup() {
    // we need to destroy the vulkan stuff in the reverse order they were created
    // this is because they depend on each other
    vkDestroySemaphore(device, imageAvailableSemaphore, nullptr);
    vkDestroyFence(device, inFlightFence, nullptr);
    for (auto semaphore : renderFinishedSemaphores) {
        vkDestroySemaphore(device, semaphore, nullptr);
    }
    vkDestroyCommandPool(device, commandPool, nullptr); // also frees the command buffer allocated from it

    vkDestroyDescriptorPool(device, descriptorPool, nullptr); // also frees the descriptor set allocated from it

    vkDestroySampler(device, textureSampler, nullptr);
    vkDestroyImageView(device, textureImageView, nullptr);
    vkDestroyImage(device, textureImage, nullptr);
    vkFreeMemory(device, textureImageMemory, nullptr);

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
    vkDestroyDescriptorSetLayout(device, descriptorSetLayout, nullptr);
    vkDestroyRenderPass(device, renderPass, nullptr);
    /*
    for (auto imageView : swapchainImageViews) {
        vkDestroyImageView(device, imageView, nullptr);
    }
    */
    CleanupSwapchain();
    vkDestroySurfaceKHR(instance, surface, nullptr);
    vkDestroyDevice(device, nullptr);
    vkDestroyInstance(instance, nullptr);
    glfwDestroyWindow(window);
    glfwTerminate();
}