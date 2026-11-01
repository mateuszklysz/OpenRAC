// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 the OpenRAC contributors
//
// HDR output (renderer/hdr.h, docs/port/HDR.md): the swapchain made in an HDR colour space when the
// frontend asks for it and the surface offers it, and the one compute pass that writes the default
// framebuffer's picture into it at the player's paper white (shaders/hdr_encode.comp). With HDR
// not asked for, nothing here runs: the swapchain is chosen and the picture copied as before.

#include <array>
#include <cstring>

#include "renderer/vulkan/surface_format.h"
#include "renderer/vulkan/vkgl_internal.h"
#include "vkgl_shaders.h"

namespace openrac::vkgl {

namespace detail {

namespace {

using renderer::hdr::Output;

// The encode pass's push constants (hdr_encode.comp's Encode).
struct EncodeConstants {
    float paper_white;
    float sdr_gamma;
    std::int32_t same_size;
};

struct StorageQuery {
    VkImageUsageFlags usage;
};

// Whether the encode pass can write a swapchain image of `format`: the surface lets its images be
// storage images, and the device stores to that format.
bool can_store(void* user, VkFormat format) {
    const auto* query = static_cast<const StorageQuery*>(user);
    if ((query->usage & VK_IMAGE_USAGE_STORAGE_BIT) == 0) {
        return false;
    }
    VkFormatProperties props{};
    vkGetPhysicalDeviceFormatProperties(g->physical, format, &props);
    return (props.optimalTilingFeatures & VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT) != 0;
}

void release_pipeline() {
    auto& hdr = g->hdr;
    vkDestroyPipeline(g->device, hdr.pipeline, nullptr);
    vkDestroyPipelineLayout(g->device, hdr.layout, nullptr);
    vkDestroyDescriptorSetLayout(g->device, hdr.descriptors, nullptr);
    hdr.pipeline = VK_NULL_HANDLE;
    hdr.layout = VK_NULL_HANDLE;
    hdr.descriptors = VK_NULL_HANDLE;
    hdr.pipeline_for = Output::Sdr;
}

// The encode pipeline for `output`, made the first time it is needed; one at a time.
bool make_pipeline(Output output) {
    auto& hdr = g->hdr;
    if (hdr.pipeline != VK_NULL_HANDLE && hdr.pipeline_for == output) {
        return true;
    }
    if (hdr.failed) {
        return false;
    }
    release_pipeline();
    const auto failed = [] {
        release_pipeline();
        g->hdr.failed = true;
        return false;
    };
    NativeComputeShader shader;
    std::string error;
    const std::string_view definitions =
        output == Output::Hdr10 ? "#define OPENRAC_HDR_PQ 1\n" : "";
    if (!compile_native_compute(shaders::hdr_encode_comp, definitions, {}, shader, error)) {
        log::warn("vulkan: no HDR encode pass ({}); the picture goes out in SDR", error);
        return failed();
    }
    ++g->submission.shader_compiles;
    const std::array<VkDescriptorSetLayoutBinding, 2> bindings{
        {{0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
         {1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr}}
    };
    VkDescriptorSetLayoutCreateInfo descriptors{
        VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO
    };
    descriptors.bindingCount = static_cast<unsigned>(bindings.size());
    descriptors.pBindings = bindings.data();
    if (!check(
            vkCreateDescriptorSetLayout(g->device, &descriptors, nullptr, &hdr.descriptors),
            "HDR encode descriptors"
        )) {
        return failed();
    }
    const VkPushConstantRange push{VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(EncodeConstants)};
    VkPipelineLayoutCreateInfo layout{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    layout.setLayoutCount = 1;
    layout.pSetLayouts = &hdr.descriptors;
    layout.pushConstantRangeCount = 1;
    layout.pPushConstantRanges = &push;
    if (!check(
            vkCreatePipelineLayout(g->device, &layout, nullptr, &hdr.layout), "HDR encode layout"
        )) {
        return failed();
    }
    VkShaderModule module = VK_NULL_HANDLE;
    VkShaderModuleCreateInfo code{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    code.codeSize = shader.code.size() * sizeof(std::uint32_t);
    code.pCode = shader.code.data();
    if (!check(vkCreateShaderModule(g->device, &code, nullptr, &module), "HDR encode module")) {
        return failed();
    }
    VkComputePipelineCreateInfo pipeline{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
    pipeline.stage = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
    pipeline.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    pipeline.stage.module = module;
    pipeline.stage.pName = "main";
    pipeline.layout = hdr.layout;
    const bool made = check(
        vkCreateComputePipelines(
            g->device, g->pipeline_cache, 1, &pipeline, nullptr, &hdr.pipeline
        ),
        "HDR encode pipeline"
    );
    vkDestroyShaderModule(g->device, module, nullptr);
    if (!made) {
        return failed();
    }
    hdr.pipeline_for = output;
    count_pipeline();
    return true;
}

// A layout change of a screen (or its stand-in) the encode pass writes, against every stage:
// only HDR frames take it, so the transfer-only form of swapchain_barrier is not needed here.
void screen_barrier(VkCommandBuffer cmd, VkImage image, VkImageLayout from, VkImageLayout to) {
    VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    b.srcAccessMask = from == VK_IMAGE_LAYOUT_UNDEFINED ? 0 : VK_ACCESS_SHADER_WRITE_BIT;
    b.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
    b.oldLayout = from;
    b.newLayout = to;
    b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.image = image;
    b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    vkCmdPipelineBarrier(
        cmd,
        VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
        VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
        0,
        0,
        nullptr,
        0,
        nullptr,
        1,
        &b
    );
    count_barrier(VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT);
}

// Records the encode of the default framebuffer into `screen` (seen through `view`, `extent`,
// in GENERAL) for `output` at `levels`.
bool encode(
    VkCommandBuffer cmd,
    VkImageView view,
    VkExtent2D extent,
    Output output,
    const renderer::hdr::Levels& levels
) {
    if (!g->default_colour || !make_pipeline(output)) {
        return false;
    }
    const VkDescriptorSet set = allocate_set(g->hdr.descriptors);
    if (!set) {
        return false;
    }
    Image& colour = *g->default_colour;
    barrier(cmd, colour, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    SamplerParams parameters;
    parameters.min = parameters.mag = gl::GL_LINEAR;
    parameters.wrap_s = parameters.wrap_t = gl::GL_CLAMP_TO_EDGE;
    const std::array<VkDescriptorImageInfo, 2> images{
        {{sampler_for(parameters, false),
          sample_view(colour, 0, 0),
          VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
         {VK_NULL_HANDLE, view, VK_IMAGE_LAYOUT_GENERAL}}
    };
    std::array<VkWriteDescriptorSet, 2> writes{};
    for (unsigned i = 0; i < writes.size(); ++i) {
        writes[i] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        writes[i].dstSet = set;
        writes[i].dstBinding = i;
        writes[i].descriptorCount = 1;
        writes[i].descriptorType =
            i == 0 ? VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER : VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        writes[i].pImageInfo = &images[i];
    }
    vkUpdateDescriptorSets(
        g->device, static_cast<unsigned>(writes.size()), writes.data(), 0, nullptr
    );
    g->submission.descriptor_writes += writes.size();
    const EncodeConstants constants{
        levels.paper_white,
        renderer::hdr::kSdrGamma,
        colour.width == static_cast<int>(extent.width)
                && colour.height == static_cast<int>(extent.height)
            ? 1
            : 0,
    };
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, g->hdr.pipeline);
    vkCmdBindDescriptorSets(
        cmd, VK_PIPELINE_BIND_POINT_COMPUTE, g->hdr.layout, 0, 1, &set, 0, nullptr
    );
    vkCmdPushConstants(
        cmd, g->hdr.layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(constants), &constants
    );
    vkCmdDispatch(cmd, (extent.width + 7) / 8, (extent.height + 7) / 8, 1);
    count_command(Command::BindPipeline);
    count_command(Command::BindDescriptors);
    count_command(Command::Other);
    count_command(Command::Dispatch);
    count_copy(extent.width, extent.height);
    return true;
}

// The screen's metadata: BT.2020 primaries, the display's peak, and content that never comes
// above paper white (MaxCLL), so that a television need not tone-map it down.
void set_metadata() {
    auto& hdr = g->hdr;
    if (!hdr.metadata || vkSetHdrMetadataEXT == nullptr || hdr.shown == Output::Sdr) {
        return;
    }
    VkHdrMetadataEXT m{VK_STRUCTURE_TYPE_HDR_METADATA_EXT};
    m.displayPrimaryRed = {0.708f, 0.292f};
    m.displayPrimaryGreen = {0.170f, 0.797f};
    m.displayPrimaryBlue = {0.131f, 0.046f};
    m.whitePoint = {0.3127f, 0.3290f};
    m.maxLuminance = hdr.asked.levels.peak;
    m.minLuminance = 0.0f;
    m.maxContentLightLevel = hdr.asked.levels.paper_white;
    m.maxFrameAverageLightLevel = hdr.asked.levels.paper_white;
    vkSetHdrMetadataEXT(g->device, 1, &g->swapchain, &m);
}

}  // namespace

VkSurfaceFormatKHR choose_hdr_format(
    std::span<const VkSurfaceFormatKHR> formats, const VkSurfaceCapabilitiesKHR& caps
) {
    auto& hdr = g->hdr;
    hdr.offered = hdr.colour_spaces && offers_hdr(formats);
    const Output wanted = hdr.colour_spaces ? hdr.asked.wanted : Output::Sdr;
    StorageQuery query{caps.supportedUsageFlags};
    const SurfaceChoice choice = choose_surface_format(formats, wanted, can_store, &query);
    hdr.made_for = hdr.asked.wanted;
    hdr.shown = choice.output;
    if (wanted != Output::Sdr && choice.output == Output::Sdr) {
        log::warn(
            "vulkan: HDR asked for, but the screen offers no HDR format the game can write; SDR"
        );
    }
    return choice.format;
}

void release_hdr_views() {
    for (VkImageView v : g->hdr.views) {
        vkDestroyImageView(g->device, v, nullptr);
    }
    g->hdr.views.clear();
}

void hdr_swapchain_made(const VkSurfaceFormatKHR& format) {
    release_hdr_views();
    auto& hdr = g->hdr;
    if (hdr.shown == Output::Sdr) {
        return;
    }
    for (VkImage image : g->swapchain_images) {
        VkImageViewCreateInfo info{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        info.image = image;
        info.viewType = VK_IMAGE_VIEW_TYPE_2D;
        info.format = format.format;
        info.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        VkImageView view = VK_NULL_HANDLE;
        if (!check(vkCreateImageView(g->device, &info, nullptr, &view), "an HDR screen view")) {
            release_hdr_views();
            hdr.shown = Output::Sdr;
            return;
        }
        hdr.views.push_back(view);
    }
    set_metadata();
    log::info(
        "vulkan: HDR output {} at {:.0f} nits paper white (peak {:.0f})",
        renderer::hdr::name(hdr.shown),
        hdr.asked.levels.paper_white,
        hdr.asked.levels.peak
    );
}

void release_hdr() {
    release_hdr_views();
    release_pipeline();
    g->hdr.failed = false;
}

bool hdr_encode_into(VkCommandBuffer cmd, std::uint32_t index) {
    auto& hdr = g->hdr;
    const VkImage screen = g->swapchain_images[index];
    // No view of it (none made): left ready for the SDR copy the caller makes instead.
    if (hdr.shown == Output::Sdr || index >= hdr.views.size()) {
        screen_barrier(
            cmd, screen, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL
        );
        return false;
    }
    screen_barrier(cmd, screen, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);
    if (!encode(cmd, hdr.views[index], g->swapchain_extent, hdr.shown, hdr.asked.levels)) {
        // Nothing written: back to TRANSFER_DST for the SDR copy the caller makes instead.
        screen_barrier(cmd, screen, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
        return false;
    }
    screen_barrier(cmd, screen, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
    return true;
}

}  // namespace detail

void set_hdr(const HdrRequest& request) {
    using namespace detail;
    if (!active()) {
        return;
    }
    auto& hdr = g->hdr;
    const bool same = hdr.asked == request;
    hdr.asked = request;
    if (same || !hdr.colour_spaces) {
        return;
    }
    // A change of kind needs another swapchain; of levels only, new constants and metadata.
    if (request.wanted != hdr.made_for) {
        g->swapchain_stale = true;
    } else {
        set_metadata();
    }
}

renderer::hdr::Output hdr_output() {
    using namespace detail;
    return active() ? g->hdr.shown : renderer::hdr::Output::Sdr;
}

bool hdr_offered() {
    using namespace detail;
    return active() && g->hdr.offered;
}

bool test_hdr_encode(
    renderer::hdr::Output output,
    const renderer::hdr::Levels& levels,
    int width,
    int height,
    std::vector<std::uint8_t>& out
) {
    using namespace detail;
    using renderer::hdr::Output;
    if (!active() || output == Output::Sdr || !g->default_colour) {
        return false;
    }
    end_rendering();
    const bool pq = output == Output::Hdr10;
    const Format format{
        pq ? VK_FORMAT_A2B10G10R10_UNORM_PACK32 : VK_FORMAT_R16G16B16A16_SFLOAT,
        VK_IMAGE_ASPECT_COLOR_BIT,
        pq ? 4 : 8
    };
    const ImagePtr screen =
        make_image(format, width, height, 1, VK_SAMPLE_COUNT_1_BIT, VK_IMAGE_USAGE_STORAGE_BIT);
    if (!screen) {
        return false;
    }
    const auto bytes = static_cast<VkDeviceSize>(width) * height * format.bytes;
    VkBufferCreateInfo info{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    info.size = bytes;
    info.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    VmaAllocationCreateInfo alloc{};
    alloc.usage = VMA_MEMORY_USAGE_AUTO;
    alloc.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
    VkBuffer buffer = VK_NULL_HANDLE;
    VmaAllocation allocation = nullptr;
    VmaAllocationInfo got{};
    if (!check(
            vmaCreateBuffer(g->allocator, &info, &alloc, &buffer, &allocation, &got), "a read-back"
        )) {
        retire(screen);
        return false;
    }
    VkCommandBuffer cmd = cmd_for(screen.get(), true);
    const VkExtent2D extent{static_cast<std::uint32_t>(width), static_cast<std::uint32_t>(height)};
    screen_barrier(cmd, screen->image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);
    const bool encoded = encode(cmd, sample_view(*screen, 0, 0), extent, output, levels);
    if (encoded) {
        screen_barrier(
            cmd, screen->image, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL
        );
        screen->layout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        VkBufferImageCopy copy{};
        copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        copy.imageExtent = {extent.width, extent.height, 1};
        vkCmdCopyImageToBuffer(
            cmd, screen->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, buffer, 1, &copy
        );
        count_command(Command::Copy);
    } else {
        screen->layout = VK_IMAGE_LAYOUT_GENERAL;
    }
    flush_and_wait();
    if (encoded) {
        vmaInvalidateAllocation(g->allocator, allocation, 0, VK_WHOLE_SIZE);
        const auto* from = static_cast<const std::uint8_t*>(got.pMappedData);
        out.assign(from, from + bytes);
    }
    vmaDestroyBuffer(g->allocator, buffer, allocation);
    retire(screen);
    return encoded;
}

}  // namespace openrac::vkgl
