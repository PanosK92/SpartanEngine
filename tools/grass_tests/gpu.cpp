// Copyright(c) 2015-2026 Panos Karabelas
// Executes the production SPIR-V compute shader and checks the resulting GPU history.
#include <vulkan/vulkan.h>
#include <array>
#include <vector>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <cmath>
#include <cstring>

void check(VkResult result) { if (result != VK_SUCCESS) throw std::runtime_error("Vulkan error " + std::to_string(result)); }
void require(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); }
struct Pixel { float x, y, pressure, height; };
struct Contact { std::array<float, 4> start, end, direction, normal; };
struct Push { uint32_t header[4]{}; float v[20]{}; };
constexpr uint32_t size = 512;
constexpr float cell = 32.0f / size;

struct Gpu
{
    VkInstance instance{};
    VkPhysicalDevice physical{};
    VkDevice device{};
    VkQueue queue{};
    VkCommandPool pool{};
    VkCommandBuffer cmd{};
    VkDescriptorPool descriptors{};
    VkDescriptorSetLayout layout{};
    VkDescriptorSet set{};
    VkPipelineLayout pipeline_layout{};
    VkPipeline pipeline{};
    VkShaderModule shader{};
    VkImage images[2]{};
    VkImageView views[2]{};
    VkDeviceMemory image_memory[2]{};
    VkBuffer contacts{}, readback{};
    VkDeviceMemory contact_memory{}, readback_memory{};
    void* mapped_contacts{};
    void* mapped_readback{};
    uint32_t current = 0;

    uint32_t memory_type(uint32_t bits, VkMemoryPropertyFlags flags)
    {
        VkPhysicalDeviceMemoryProperties properties{};
        vkGetPhysicalDeviceMemoryProperties(physical, &properties);
        for (uint32_t i = 0; i < properties.memoryTypeCount; ++i)
            if ((bits & (1u << i)) && (properties.memoryTypes[i].propertyFlags & flags) == flags) return i;
        throw std::runtime_error("No suitable Vulkan memory type");
    }
    void buffer(VkDeviceSize bytes, VkBufferUsageFlags usage, VkBuffer& result, VkDeviceMemory& memory, void*& mapped)
    {
        VkBufferCreateInfo info{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO}; info.size = bytes; info.usage = usage;
        check(vkCreateBuffer(device, &info, nullptr, &result));
        VkMemoryRequirements requirements{}; vkGetBufferMemoryRequirements(device, result, &requirements);
        VkMemoryAllocateInfo allocation{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO}; allocation.allocationSize = requirements.size;
        allocation.memoryTypeIndex = memory_type(requirements.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        check(vkAllocateMemory(device, &allocation, nullptr, &memory));
        check(vkBindBufferMemory(device, result, memory, 0));
        check(vkMapMemory(device, memory, 0, bytes, 0, &mapped));
    }
    void begin()
    {
        check(vkResetCommandPool(device, pool, 0));
        VkCommandBufferBeginInfo begin_info{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        check(vkBeginCommandBuffer(cmd, &begin_info));
    }
    void submit()
    {
        check(vkEndCommandBuffer(cmd));
        VkSubmitInfo info{VK_STRUCTURE_TYPE_SUBMIT_INFO}; info.commandBufferCount = 1; info.pCommandBuffers = &cmd;
        check(vkQueueSubmit(queue, 1, &info, VK_NULL_HANDLE)); check(vkQueueWaitIdle(queue));
    }
    void barrier(VkImage image, VkImageLayout before, VkImageLayout after)
    {
        VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
        b.srcAccessMask = before == VK_IMAGE_LAYOUT_UNDEFINED ? 0 : VK_ACCESS_MEMORY_WRITE_BIT | VK_ACCESS_MEMORY_READ_BIT;
        b.dstAccessMask = VK_ACCESS_MEMORY_WRITE_BIT | VK_ACCESS_MEMORY_READ_BIT;
        b.oldLayout = before; b.newLayout = after; b.image = image;
        b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, nullptr, 0, nullptr, 1, &b);
    }
    Gpu(const char* shader_path = "binaries/grass_tests/grass_interaction_cs_SP_SHADER_STAGE_COMPUTE_vulkan.bin")
    {
        VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO}; app.apiVersion = VK_API_VERSION_1_3;
        VkInstanceCreateInfo info{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO}; info.pApplicationInfo = &app;
        check(vkCreateInstance(&info, nullptr, &instance));
        uint32_t count = 0; check(vkEnumeratePhysicalDevices(instance, &count, nullptr)); require(count > 0, "No Vulkan GPU");
        std::vector<VkPhysicalDevice> devices(count); check(vkEnumeratePhysicalDevices(instance, &count, devices.data())); physical = devices[0];
        VkPhysicalDeviceProperties properties{}; vkGetPhysicalDeviceProperties(physical, &properties);
        std::cout << "GPU: " << properties.deviceName << '\n';
        vkGetPhysicalDeviceQueueFamilyProperties(physical, &count, nullptr);
        std::vector<VkQueueFamilyProperties> families(count); vkGetPhysicalDeviceQueueFamilyProperties(physical, &count, families.data());
        uint32_t family = 0; while (family < count && !(families[family].queueFlags & VK_QUEUE_COMPUTE_BIT)) ++family;
        require(family < count, "No compute queue");
        float priority = 1;
        VkDeviceQueueCreateInfo qi{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO}; qi.queueFamilyIndex = family; qi.queueCount = 1; qi.pQueuePriorities = &priority;
        VkPhysicalDeviceFeatures features{}; features.shaderStorageImageWriteWithoutFormat = VK_TRUE;
        VkPhysicalDeviceVulkan12Features features12{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES}; features12.runtimeDescriptorArray = VK_TRUE;
        VkDeviceCreateInfo di{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO}; di.pNext = &features12; di.queueCreateInfoCount = 1; di.pQueueCreateInfos = &qi; di.pEnabledFeatures = &features;
        check(vkCreateDevice(physical, &di, nullptr, &device)); vkGetDeviceQueue(device, family, 0, &queue);
        VkCommandPoolCreateInfo pi{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO}; pi.queueFamilyIndex = family;
        check(vkCreateCommandPool(device, &pi, nullptr, &pool));
        VkCommandBufferAllocateInfo ai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO}; ai.commandPool = pool; ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY; ai.commandBufferCount = 1;
        check(vkAllocateCommandBuffers(device, &ai, &cmd));
        for (uint32_t i = 0; i < 2; ++i)
        {
            VkImageCreateInfo ii{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO}; ii.imageType = VK_IMAGE_TYPE_2D; ii.format = VK_FORMAT_R32G32B32A32_SFLOAT;
            ii.extent = {size, size, 1}; ii.mipLevels = ii.arrayLayers = 1; ii.samples = VK_SAMPLE_COUNT_1_BIT;
            ii.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
            check(vkCreateImage(device, &ii, nullptr, &images[i]));
            VkMemoryRequirements mr{}; vkGetImageMemoryRequirements(device, images[i], &mr);
            VkMemoryAllocateInfo ma{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO}; ma.allocationSize = mr.size; ma.memoryTypeIndex = memory_type(mr.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
            check(vkAllocateMemory(device, &ma, nullptr, &image_memory[i])); check(vkBindImageMemory(device, images[i], image_memory[i], 0));
            VkImageViewCreateInfo vi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO}; vi.image = images[i]; vi.viewType = VK_IMAGE_VIEW_TYPE_2D; vi.format = ii.format; vi.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
            check(vkCreateImageView(device, &vi, nullptr, &views[i]));
        }
        buffer(16384, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, contacts, contact_memory, mapped_contacts);
        buffer(size * size * sizeof(Pixel), VK_BUFFER_USAGE_TRANSFER_DST_BIT, readback, readback_memory, mapped_readback);
        VkDescriptorSetLayoutBinding bindings[] = {{0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
            {7, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr}, {61, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr}};
        VkDescriptorSetLayoutCreateInfo li{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO}; li.bindingCount = 3; li.pBindings = bindings;
        check(vkCreateDescriptorSetLayout(device, &li, nullptr, &layout));
        VkDescriptorPoolSize sizes[] = {{VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1}, {VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1}, {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1}};
        VkDescriptorPoolCreateInfo dp{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO}; dp.maxSets = 1; dp.poolSizeCount = 3; dp.pPoolSizes = sizes;
        check(vkCreateDescriptorPool(device, &dp, nullptr, &descriptors));
        VkDescriptorSetAllocateInfo ds{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO}; ds.descriptorPool = descriptors; ds.descriptorSetCount = 1; ds.pSetLayouts = &layout;
        check(vkAllocateDescriptorSets(device, &ds, &set));
        VkPushConstantRange range{VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(Push)};
        VkPipelineLayoutCreateInfo pl{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO}; pl.setLayoutCount = 1; pl.pSetLayouts = &layout; pl.pushConstantRangeCount = 1; pl.pPushConstantRanges = &range;
        check(vkCreatePipelineLayout(device, &pl, nullptr, &pipeline_layout));
        std::ifstream file(shader_path, std::ios::binary | std::ios::ate);
        require(file.good(), "Compile the production shaders first");
        size_t bytes = static_cast<size_t>(file.tellg()); std::vector<uint32_t> code(bytes / 4); file.seekg(0); file.read(reinterpret_cast<char*>(code.data()), bytes);
        VkShaderModuleCreateInfo sm{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO}; sm.codeSize = bytes; sm.pCode = code.data(); check(vkCreateShaderModule(device, &sm, nullptr, &shader));
        VkComputePipelineCreateInfo cp{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO}; cp.layout = pipeline_layout;
        cp.stage = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO}; cp.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT; cp.stage.module = shader; cp.stage.pName = "main_cs";
        check(vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &cp, nullptr, &pipeline));
        begin();
        for (VkImage image : images)
        {
            barrier(image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);
            VkClearColorValue zero{}; VkImageSubresourceRange sub{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
            vkCmdClearColorImage(cmd, image, VK_IMAGE_LAYOUT_GENERAL, &zero, 1, &sub);
        }
        submit();
    }
    std::vector<Pixel> run(Push push, const std::vector<Contact>& wheels = {}, const std::vector<std::array<float, 4>>& body_data = {})
    {
        require(wheels.size() <= 4, "Contact capacity");
        if (!wheels.empty()) std::memcpy(mapped_contacts, wheels.data(), wheels.size() * sizeof(Contact));
        require(body_data.size() * sizeof(body_data[0]) <= 16384, "Body buffer capacity");
        if (!body_data.empty()) std::memcpy(mapped_contacts, body_data.data(), body_data.size() * sizeof(body_data[0]));
        push.v[6] = static_cast<float>(wheels.size());
        uint32_t output = 1 - current;
        VkDescriptorImageInfo image_info[] = {{VK_NULL_HANDLE, views[output], VK_IMAGE_LAYOUT_GENERAL}, {VK_NULL_HANDLE, views[current], VK_IMAGE_LAYOUT_GENERAL}};
        VkDescriptorBufferInfo buffer_info{contacts, 0, 16384};
        VkWriteDescriptorSet writes[3]{};
        const uint32_t slots[] = {0, 7, 61};
        const VkDescriptorType types[] = {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER};
        for (uint32_t i = 0; i < 3; ++i) { writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET; writes[i].dstSet = set; writes[i].dstBinding = slots[i]; writes[i].descriptorCount = 1; writes[i].descriptorType = types[i]; }
        writes[0].pImageInfo = &image_info[0]; writes[1].pImageInfo = &image_info[1]; writes[2].pBufferInfo = &buffer_info;
        vkUpdateDescriptorSets(device, 3, writes, 0, nullptr);
        begin();
        for (VkImage image : images) barrier(image, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL);
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline_layout, 0, 1, &set, 0, nullptr);
        vkCmdPushConstants(cmd, pipeline_layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(push), &push);
        vkCmdDispatch(cmd, size / 8, size / 8, 1);
        barrier(images[output], VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
        VkBufferImageCopy copy{}; copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1}; copy.imageExtent = {size, size, 1};
        vkCmdCopyImageToBuffer(cmd, images[output], VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, readback, 1, &copy);
        barrier(images[output], VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL);
        VkMemoryBarrier host{VK_STRUCTURE_TYPE_MEMORY_BARRIER}; host.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT; host.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &host, 0, nullptr, 0, nullptr);
        submit(); current = output;
        auto* data = static_cast<Pixel*>(mapped_readback); return {data, data + size * size};
    }
    ~Gpu()
    {
        vkDeviceWaitIdle(device);
        vkUnmapMemory(device, contact_memory); vkUnmapMemory(device, readback_memory);
        vkDestroyBuffer(device, contacts, nullptr); vkDestroyBuffer(device, readback, nullptr);
        vkFreeMemory(device, contact_memory, nullptr); vkFreeMemory(device, readback_memory, nullptr);
        for (uint32_t i = 0; i < 2; ++i) { vkDestroyImageView(device, views[i], nullptr); vkDestroyImage(device, images[i], nullptr); vkFreeMemory(device, image_memory[i], nullptr); }
        vkDestroyPipeline(device, pipeline, nullptr); vkDestroyShaderModule(device, shader, nullptr); vkDestroyPipelineLayout(device, pipeline_layout, nullptr);
        vkDestroyDescriptorPool(device, descriptors, nullptr); vkDestroyDescriptorSetLayout(device, layout, nullptr); vkDestroyCommandPool(device, pool, nullptr);
        vkDestroyDevice(device, nullptr); vkDestroyInstance(instance, nullptr);
    }
};

int main() try
{
    Gpu gpu;
    Push p{}; p.v[0] = p.v[1] = p.v[4] = p.v[5] = 5984; p.v[2] = cell;
    p.v[7] = 1.0f / 60; p.v[8] = p.v[9] = 6000; p.v[10] = 8; p.v[11] = 1.5f;
    Contact wheel{{6000, 175, 6000, 0.15f}, {6000, 175, 6004, 0.22f}, {0, 1, 1, 0}, {0, 1, 0, 0}};
    auto at = [](const std::vector<Pixel>& pixels, float x, float z, const Push& push) -> Pixel {
        return pixels[static_cast<size_t>((z - push.v[1]) / cell) * size + static_cast<size_t>((x - push.v[0]) / cell)];
    };
    auto first = gpu.run(p, {wheel}); p.v[3] = 1;
    for (float z = 6000; z < 6004; z += 0.125f)
        require(at(first, 6000, z, p).pressure > 0.95f, "High-speed sweep has a gap");
    require(at(first, 6000.5f, 6002, p).pressure == 0, "Track is wider than the tire");
    Pixel mark = at(first, 6000, 6002, p);
    require(mark.y > 0.95f && std::abs(mark.height / mark.pressure - 175) < 0.001f, "Direction or world height incorrect");
    auto held = gpu.run(p);
    require(std::memcmp(&held[0], &first[0], held.size() * sizeof(Pixel)) == 0, "Nearby track changed without contact");
    p.v[7] = 60; held = gpu.run(p);
    require(at(held, 6000, 6002, p).pressure == mark.pressure, "Nearby track expired with time");
    p.v[7] = 1.0f / 60; p.v[0] += 1.25f;
    auto scrolled = gpu.run(p); p.v[4] = p.v[0];
    require(at(scrolled, 6000, 6002, p).pressure == mark.pressure, "Grid scrolling diffused or moved the track");
    require(at(scrolled, 6016, 6002, p).pressure == 0, "Exposed grid cells wrapped stale marks");
    wheel.start = {6000, 175, 6002, 0.15f}; wheel.end = {6000, 175, 6002, 0.22f}; wheel.direction = {0, -1, 1, 0};
    auto reverse = gpu.run(p, {wheel});
    Pixel turned = at(reverse, 6000, 6002, p);
    require(turned.pressure > 0.95f && std::abs(turned.x) > 0.3f, "Reversal cancelled pressure or snapped direction");
    p.v[8] = 6010; p.v[7] = 1;
    auto faded = gpu.run(p);
    require(at(faded, 6000, 6002, p).pressure < 0.5f, "Grass did not recover away from the car");
    p.v[3] = 0; p.v[8] = 6000; p.v[7] = 1.0f / 60;
    auto cleared = gpu.run(p);
    for (const Pixel& pixel : cleared) require(pixel.pressure == 0, "Reset retained stale tracks");
    for (int fps : {30, 60, 144})
    {
        p.v[3] = 0; p.v[7] = 1.0f / fps;
        std::vector<Pixel> settled;
        for (int i = 0; i < fps / 2; ++i) { settled = gpu.run(p, {wheel}); p.v[3] = 1; }
        require(at(settled, 6000, 6002, p).pressure > 0.99f, "Resting wheel failed to accumulate pressure");
        p.v[7] = 0;
        auto paused = gpu.run(p);
        require(at(paused, 6000, 6002, p).pressure == at(settled, 6000, 6002, p).pressure, "Paused history changed");
    }
    std::cout << "PASS sweep continuity, tire width, direction/height, persistence, scrolling, reversal, recovery, reset, resting wheels at 30/60/144 Hz, pause\n";
    Gpu body_gpu("binaries/grass_tests/body_cs_SP_SHADER_STAGE_COMPUTE_vulkan.bin");
    for (bool rotated : {false, true})
    {
        // Compound yaw/pitch, plus the island's large world coordinates.
        const float a = rotated ? 0.83f : 0.0f, b = rotated ? 0.25f : 0.0f;
        Contact body{{6250, 18, -2840, 1},
            {std::cos(a), 0, -std::sin(a), 1},
            {std::sin(a) * std::sin(b), std::cos(b), std::cos(a) * std::sin(b), 0.5f},
            {std::sin(a) * std::cos(b), -std::sin(b), std::cos(a) * std::cos(b), 2}};
        for (int mode = 0; mode <= 7; ++mode)
        {
            Push test{}; test.v[0] = static_cast<float>(mode);
            std::vector<std::array<float, 4>> body_data(4 + 6 + 6 * 48);
            body_data[0] = body.start; body_data[1] = body.end; body_data[2] = body.direction; body_data[3] = body.normal;
            body_data[4] = {0, mode == 7 ? 7.0f : 6.0f, 0, 0};
            body_data[10] = {1, 0, 0, -1}; body_data[11] = {-1, 0, 0, -1};
            body_data[12] = {0, 1, 0, -0.5f}; body_data[13] = {0, -1, 0, -0.5f};
            body_data[14] = {0, 0, 1, -2}; body_data[15] = {0, 0, -1, -2};
            body_data[16] = {0.70710678f, 0, 0.70710678f, -1.6f};
            const auto result = body_gpu.run(test, {}, body_data);
            for (uint32_t row = 0; row < size; ++row)
            {
                const float r = static_cast<float>(row) / (size - 1);
                for (uint32_t column = 0; column < size; ++column)
                {
                    const float h = static_cast<float>(column) / (size - 1);
                    const Pixel q = result[row * size + column];
                    require(std::isfinite(q.x) && std::isfinite(q.y) && std::isfinite(q.pressure) && std::abs(q.height - 2) < 0.002f,
                        "Body deformation produced a non-finite position or damaged the shading basis");
                    require(mode == 7 || std::abs(q.x) >= 0.999f || std::abs(q.y) >= 0.499f || std::abs(q.pressure) >= 1.999f,
                        "Blade penetrates the chassis");
                    if (mode == 1) require(q.y <= -0.498f || std::abs(q.x) >= 0.998f || std::abs(q.pressure) >= 1.998f,
                        "Underbody grass exceeds the chassis clearance");
                    if ((mode >= 2 && mode <= 4) || mode == 6)
                    {
                        float x = mode == 2 ? -0.45f + r * 0.9f : 1.08f + r * 1.3f + (mode == 3 ? 5 : 0);
                        float y = mode == 4 ? 1.0f : -0.9f;
                        require(std::abs(q.x - (x - 0.55f * h * h)) < 0.002f &&
                            std::abs(q.y - (y + h * (mode == 2 ? 0.05f : 1.2f))) < 0.002f,
                            "Body changed a blade below, above or far from the car");
                    }
                    if (mode == 7) require(std::abs(q.x - (0.95f + r * 0.1f + 0.05f * h * h)) < 0.002f &&
                        std::abs(q.y - (-0.9f + 1.2f * h)) < 0.002f, "Body cleared grass in the empty corner of its bounding box");
                    float root_x = mode == 1 ? -0.95f + r * 1.9f : mode == 2 ? -0.45f + r * 0.9f :
                        mode == 5 ? 0.3f : mode == 7 ? 0.95f + r * 0.1f : 1.08f + r * 1.3f + (mode == 3 ? 5.0f : 0.0f);
                    float root_y = mode == 4 ? 1.0f : -0.9f;
                    float root_z = mode == 5 ? 2.08f + r * 1.3f : mode == 6 ? 2.3f : mode == 7 ? 1.8f : 0.3f;
                    float ox = (mode == 7 ? 0.05f : -0.55f) * h * h;
                    float oy = h * (mode == 2 ? 0.05f : 1.2f);
                    float oz = mode == 7 ? 0.0f : 0.015f * std::sin(r * 100.0f) * h;
                    if (mode == 5) { oz = ox; ox = 0; }
                    float original_length = std::sqrt(ox * ox + oy * oy + oz * oz);
                    float dx = q.x - root_x, dy = q.y - root_y, dz = q.pressure - root_z;
                    require(std::abs(std::sqrt(dx * dx + dy * dy + dz * dz) - original_length) < 0.003f,
                        "Body contact shortened a blade instead of bending it");
                    if (column == 0) require(std::abs(q.y - (mode == 4 ? 1.0f : -0.9f)) < 0.002f, "Body moved a planted root");
                    if (column > 0)
                    {
                        const Pixel previous = result[row * size + column - 1];
                        if (std::abs(q.x - previous.x) >= 0.035f || std::abs(q.y - previous.y) >= 0.035f || std::abs(q.pressure - previous.pressure) >= 0.035f)
                            std::cerr << "Body discontinuity mode=" << mode << " row=" << row << " column=" << column << " from "
                                << previous.x << ',' << previous.y << ',' << previous.pressure << " to " << q.x << ',' << q.y << ',' << q.pressure << '\n';
                        require(std::abs(q.x - previous.x) < 0.035f && std::abs(q.y - previous.y) < 0.035f &&
                            std::abs(q.pressure - previous.pressure) < 0.035f, "Body bend contains a discontinuity");
                    }
                    if (row > 0 && mode == 1)
                    {
                        const Pixel neighbour = result[(row - 1) * size + column];
                        if (std::abs(q.x - neighbour.x) >= 0.04f || std::abs(q.y - neighbour.y) >= 0.04f)
                            std::cerr << "Body neighbour mode=" << mode << " row=" << row << " column=" << column << " from "
                                << neighbour.x << ',' << neighbour.y << ',' << neighbour.pressure << " to " << q.x << ',' << q.y << ',' << q.pressure << '\n';
                        require(std::abs(q.x - neighbour.x) < 0.04f && std::abs(q.y - neighbour.y) < 0.04f,
                            "Passing the lower body edge snapped the grass between sideways and downward bending");
                    }
                }
            }
            body_data[0][3] = 0;
            const auto released = body_gpu.run(test, {}, body_data);
            if (mode == 0) require(released[size - 1].x < 1.0f, "Body contact persisted after release");
        }
    }
    std::cout << "PASS body clearance, preserved blade length, side/bumper bending, undertray, planted roots, continuity, rotated/sloped chassis, large coordinates, release, unaffected surrounding grass and empty hull corners\n";
    Gpu distribution_gpu("binaries/grass_tests/distribution_cs_SP_SHADER_STAGE_COMPUTE_vulkan.bin");
    for (uint32_t count : {1u, 2u, 3u, 6u, 7u, 13u, 31u, 257u, 2048u})
    {
        for (uint32_t symmetry = 0; symmetry < 8; ++symmetry)
        {
            Push test{}; test.v[0] = static_cast<float>(count); test.v[1] = static_cast<float>(symmetry);
            auto points = distribution_gpu.run(test);
            // The jitter margin guarantees separation within a cell, even for
            // odd population budgets. Test positions produced by the GPU helper.
            float separation = 0.1f / std::sqrt(static_cast<float>(count));
            for (uint32_t i = 0; i < count; ++i)
            {
                require(points[i].x > 0 && points[i].x < 1 && points[i].y > 0 && points[i].y < 1,
                    "Grass placement escaped its cell");
                for (uint32_t j = 0; j < i; ++j)
                {
                    float dx = points[i].x - points[j].x, dy = points[i].y - points[j].y;
                    require(dx * dx + dy * dy > separation * separation, "Grass roots overlap or bunch up within a cell");
                }
            }
            if (count == 257)
            {
                auto repeated = distribution_gpu.run(test);
                require(std::memcmp(points.data(), repeated.data(), count * sizeof(Pixel)) == 0,
                    "Grass placement changed between frames");
                // Every quarter of the ground must receive its share of roots.
                int quarters[4]{};
                for (uint32_t i = 0; i < count; ++i) ++quarters[(points[i].x > 0.5f ? 1 : 0) + (points[i].y > 0.5f ? 2 : 0)];
                for (int population : quarters) require(population >= 50 && population <= 79, "Grass distribution left a sparse ground quadrant");
                test.v[2] = 4096;
                auto narrow_fov = distribution_gpu.run(test);
                test.v[0] = 280;
                auto wide_fov = distribution_gpu.run(test);
                require(std::memcmp(narrow_fov.data(), wide_fov.data(), count * sizeof(Pixel)) == 0,
                    "Camera FOV changed existing grass positions");
            }
        }
    }
    std::cout << "PASS grass placement: separated roots, balanced coverage, arbitrary populations, scrambled cells, stable frames and changing FOV\n";
    return 0;
}
catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
