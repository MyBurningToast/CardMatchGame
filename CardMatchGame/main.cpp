#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <iostream>
#include <vector>

int main() {
    // chek glfw initilizes
    if (!glfwInit()) {
        std::cerr << "Failed to init GLFW\n";
        return -1; // in c++ a non zero return value is considered an error
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
    createInfo.enabledLayerCount = 0;

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
        std::cerr << "failed to create window surface \n";
        return -1;

    }
    std::cout << "window surface created \n";


    // Pick a physical device. Somone might have multiple GPUs
    uint32_t deviceCount = 0; // Vulkan uses explicit, fixed width integer types. A normal ints size is compiler dependant
    vkEnumeratePhysicalDevices(instance, &deviceCount, nullptr); // vulkan will count all the gpus that support it
    if (deviceCount == 0) { // couldnt find a compatable gpu
        std::cerr << "no gpu with vulkan support/n";
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
        std::cerr << "no GPU with graphics support found\n";
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

    VkDeviceCreateInfo deviceCreateInfo{};
    deviceCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    deviceCreateInfo.pQueueCreateInfos = &queueCreateInfo;
    deviceCreateInfo.queueCreateInfoCount = 1;
    deviceCreateInfo.pEnabledFeatures = &deviceFeatures;
    deviceCreateInfo.enabledExtensionCount = 0; // TODO: add device extentions for swapchain
    deviceCreateInfo.enabledLayerCount = 0; // TODO: add validation layers

    VkDevice device; // the logical device handel
    VkResult deviceResult = vkCreateDevice(physicalDevice, &deviceCreateInfo, nullptr, &device); // create a device for our physical device

    if (deviceResult != VK_SUCCESS) {
        std::cerr << "failed to create logical device\n";
        return -1;
    }



    // get a handle to the graphics queue so we can submit commands to it
    // queueIndex 0 is the first (and only) queue we requested above
    VkQueue graphicsQueue;
    vkGetDeviceQueue(device, graphicsQueueFamilyIndex, 0, &graphicsQueue);
    std::cout << "device and graphics queue created\n";

    // keep window open
    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();
    }

    // cleanup
    glfwDestroyWindow(window);
    glfwTerminate();
    // we need to destroy the vulkan stuff in the reverse order they were created
    // this is beacuse they depend on each other
    vkDestroySurfaceKHR(instance, surface, nullptr);
    vkDestroyDevice(device, nullptr);
    vkDestroyInstance(instance, nullptr);
    return 0;
}