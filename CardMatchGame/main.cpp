#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <iostream>
#include <vector>

int main() {
    // chek glfw initilizes
    if (!glfwInit()) {
        std::cerr << "Failed to init GLFW\n";
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
    createInfo.enabledLayerCount = 0;

    VkInstance instance;
    VkResult result = vkCreateInstance(&createInfo, nullptr, &instance);
    if (result != VK_SUCCESS) {
        std::cerr << "Failed to create Vulkan instance " << result << "\n";
        glfwDestroyWindow(window);
        glfwTerminate();
        return -1;
    }


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
    uint32_t graphicsQueieFamilyIndex = 0;

    // a gpu can have multiple queue families, there are groups of commands it supports.
    // in this case we need one that supports graphics commands
    for (const auto& device : devices) {
        uint32_t queueFamilyCount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, nullptr);

        std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
        vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, queueFamilies.data());
        for (uint32_t i = 0; i < queueFamilyCount; i++) {
            // queueFlags is a bitmask (a sinlge int repersenting differnt capabilitys)
            // thats why we need to check with bitwise and operator insead of ==
            if (queueFamilies[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) { // VK_QUEUE_GRAPHICS_BIT means it supports graphics commands
                physicalDevice = device;
                graphicsQueieFamilyIndex = i; // save what index the queue family for graphics is
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
    std::cout << deviceProps.deviceName;
    return 0;

    // keep window open
    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();
    }

    // cleanup
    vkDestroyInstance(instance, nullptr);
    glfwDestroyWindow(window);
    glfwTerminate();

    std::cout << "done\n";
    return 0;
}