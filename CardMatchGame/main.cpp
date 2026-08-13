#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <iostream>
#include <vector>
#include <cstring>
#include <fstream>

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

int main() {

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
    GLFWwindow* window = glfwCreateWindow(640, 480, "Test", nullptr, nullptr);
    if (!window) {
        std::cerr << "Failed to create GLFW window\n";
        glfwTerminate();
        return -1;
    }

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

    VkInstance instance;
    VkResult result = vkCreateInstance(&createInfo, nullptr, &instance);
    if (result != VK_SUCCESS) {
        std::cerr << "Failed to create Vulkan instance " << result << "\n";
        glfwDestroyWindow(window);
        glfwTerminate();
        return -1;
    }


    // to draw stuff to the screen we need a bridge between vulkan and the glfw window
    VkSurfaceKHR surface;
    VkResult surfaceResult = glfwCreateWindowSurface(instance, window, nullptr, &surface); // glfw can do this automaticly based on operating system & graphics api
    if (surfaceResult != VK_SUCCESS) {
        std::cerr << "Failed to create window surface\n";
        return -1;

    }
    std::cout << "Window surface created\n";


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
    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
    uint32_t graphicsQueueFamilyIndex = 0;

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

    VkDevice device; // the logical device handel
    VkResult deviceResult = vkCreateDevice(physicalDevice, &deviceCreateInfo, nullptr, &device); // create a device for our physical device

    if (deviceResult != VK_SUCCESS) {
        std::cerr << "Failed to create logical device\n";
        return -1;
    }

    // create the swapchain (the images we render into and present)
    VkSurfaceCapabilitiesKHR capabilities;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDevice, surface, &capabilities); // get info about the window

    // check what pixel formats the surface supports
    // surface format tells vulkan how the images in the swapchain should store their pixles
    // some windows/gpus cant necessarily display every pixel format
    uint32_t formatCount = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface, &formatCount, nullptr); // ask how many formats exist
    std::vector<VkSurfaceFormatKHR> formats(formatCount); // allocate enough space
    vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface, &formatCount, formats.data()); // fill that allocated array

    // just get the first available format, good enough for now
    VkSurfaceFormatKHR surfaceFormat = formats[0];

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

    VkSwapchainKHR swapchain;
    VkResult swapchainResult = vkCreateSwapchainKHR(device, &swapchainCreateInfo, nullptr, &swapchain);
    if (swapchainResult != VK_SUCCESS) {
        std::cerr << "Failed to create swapchain\n";
        return -1;
    }

    std::cout << "Swapchain created with " << imageCount << " images\n";

    // Fetch handles to the actual images the swapchain created for us
    vkGetSwapchainImagesKHR(device, swapchain, &imageCount, nullptr);
    std::vector<VkImage> swapchainImages(imageCount);
    // We only specify a minimum count and it may have created more
    vkGetSwapchainImagesKHR(device, swapchain, &imageCount, swapchainImages.data());

    // create image view for each swapchain image
    // image views descrie how to access an image. stuff like format and dimensions
    // This is beacuse vulkan wont let you render into raw images
    std::vector<VkImageView> swapchainImageViews(swapchainImages.size());
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

    VkRenderPass renderPass;
    VkResult renderPassResult = vkCreateRenderPass(device, &renderPassCreateInfo, nullptr, &renderPass);
    if (renderPassResult != VK_SUCCESS) {
        std::cerr << "Failed to create render pass\n";
        return -1;
    }

    std::cout << "Render pass created\n";












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


    // vertex input describes the format of vertex data
    // right now its hard coded in the shader for a triangle
    VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
    vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInputInfo.vertexBindingDescriptionCount = 0;
    vertexInputInfo.vertexAttributeDescriptionCount = 0;

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

    // pipline layout is for passing extra data like uniform buffer objects
    // TODO: create UBO struct 
    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 0;
    pipelineLayoutInfo.pushConstantRangeCount = 0;

    VkPipelineLayout pipelineLayout;
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


    VkPipeline graphicsPipeline;
    if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &graphicsPipeline) != VK_SUCCESS) {
        std::cerr << "Failed to create graphics pipeline\n";
        return -1;
    }

    // shader modules are only needed to build the pipeline
    vkDestroyShaderModule(device, vertShaderModule, nullptr);
    vkDestroyShaderModule(device, fragShaderModule, nullptr);
    std::cout << "Graphics pipeline created\n";






    // get a handle to the graphics queue so we can submit commands to it
    // queueIndex 0 is the first (and only) queue we requested
    VkQueue graphicsQueue;
    vkGetDeviceQueue(device, graphicsQueueFamilyIndex, 0, &graphicsQueue);
    std::cout << "Device and graphics queue created\n";

    // keep window open
    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();
    }

    // cleanup

    // we need to destroy the vulkan stuff in the reverse order they were created
    // this is beacuse they depend on each other
    vkDestroyRenderPass(device, renderPass, nullptr);

    for (auto imageView : swapchainImageViews) {
        vkDestroyImageView(device, imageView, nullptr);
    }

    vkDestroySwapchainKHR(device, swapchain, nullptr);
    vkDestroySurfaceKHR(instance, surface, nullptr);
    vkDestroyDevice(device, nullptr);
    vkDestroyInstance(instance, nullptr);

    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}