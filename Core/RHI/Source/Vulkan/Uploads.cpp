#include <lumora/rhi/RHI.h>
#include "VulkanRHI.h"
#include "Translate.h"

#include <algorithm>
#include <cstring>
#include <vector>

namespace lumora::rhi {

namespace {

void ExecuteBlockingUploads(detail::NativeRHI& n, std::span<const UploadRequest> requests, bool isUpdate)
{
    if (requests.empty())
        return;

    struct StagingCopy {
        uint64_t offset = 0;
        uint64_t size   = 0;
    };
    std::vector<StagingCopy> staging(requests.size());
    uint64_t currentOffset = 0;
    for (size_t i = 0; i < requests.size(); ++i) {
        const auto& req = requests[i];
        if (req.size == 0 || !req.data)
            continue;
        currentOffset = (currentOffset + 15) & ~15ull;
        staging[i].offset = currentOffset;
        staging[i].size   = req.size;
        currentOffset += req.size;
    }
    if (currentOffset == 0)
        return;

    VkBufferCreateInfo bci{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bci.size        = currentOffset;
    bci.usage       = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    bci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo aci{};
    aci.usage = VMA_MEMORY_USAGE_AUTO;
    aci.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;

    VkBuffer stagingBuffer = VK_NULL_HANDLE;
    VmaAllocation stagingAlloc = VK_NULL_HANDLE;
    VmaAllocationInfo allocInfo{};
    LUMORA_VK_CHECK(vmaCreateBuffer(n.allocator, &bci, &aci, &stagingBuffer, &stagingAlloc, &allocInfo));

    void* mapped = allocInfo.pMappedData;
    if (!mapped)
        LUMORA_VK_CHECK(vmaMapMemory(n.allocator, stagingAlloc, &mapped));

    for (size_t i = 0; i < requests.size(); ++i) {
        if (staging[i].size > 0 && requests[i].data)
            std::memcpy(static_cast<char*>(mapped) + staging[i].offset, requests[i].data, staging[i].size);
    }
    vmaFlushAllocation(n.allocator, stagingAlloc, 0, currentOffset);

    VkCommandBufferAllocateInfo cbai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    cbai.commandPool        = n.immediatePool;
    cbai.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cbai.commandBufferCount = 1;
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    LUMORA_VK_CHECK(vkAllocateCommandBuffers(n.device, &cbai, &cmd));

    VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    LUMORA_VK_CHECK(vkBeginCommandBuffer(cmd, &bi));

    for (size_t i = 0; i < requests.size(); ++i) {
        const auto& req = requests[i];
        if (staging[i].size == 0)
            continue;

        if (req.dstBuffer.IsValid()) {
            detail::Buffer* dstBuf = n.buffers.Get(req.dstBuffer);
            if (!dstBuf)
                continue;

            VkBufferCopy region{};
            region.srcOffset = staging[i].offset;
            region.dstOffset = req.dstOffset;
            region.size      = staging[i].size;
            vkCmdCopyBuffer(cmd, stagingBuffer, dstBuf->buffer, 1, &region);

            VkBufferMemoryBarrier bmb{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};
            bmb.srcAccessMask       = VK_ACCESS_TRANSFER_WRITE_BIT;
            bmb.dstAccessMask       = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_UNIFORM_READ_BIT |
                                      VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT | VK_ACCESS_INDEX_READ_BIT;
            bmb.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            bmb.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            bmb.buffer              = dstBuf->buffer;
            bmb.offset              = req.dstOffset;
            bmb.size                = staging[i].size;
            vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                                 0, 0, nullptr, 1, &bmb, 0, nullptr);
        } else if (req.dstTexture.IsValid()) {
            detail::Texture* dstTex = n.textures.Get(req.dstTexture);
            if (!dstTex)
                continue;

            const auto& reg = req.textureRegion;
            const uint32_t mipWidth  = std::max(1u, dstTex->desc.extent.width >> reg.mipLevel);
            const uint32_t mipHeight = std::max(1u, dstTex->desc.extent.height >> reg.mipLevel);
            const uint32_t mipDepth  = std::max(1u, dstTex->desc.extent.depth >> reg.mipLevel);

            Extent3D copyExtent = reg.extent;
            if (copyExtent.width == 0 || copyExtent.height == 0)
                copyExtent = {mipWidth, mipHeight, mipDepth};
            else
                copyExtent.depth = std::max(1u, copyExtent.depth);

            const VkImageAspectFlags aspect = detail::AspectOf(dstTex->desc.format);

            VkImageMemoryBarrier imbPre{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
            imbPre.oldLayout                       = isUpdate ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
                                                              : VK_IMAGE_LAYOUT_UNDEFINED;
            imbPre.newLayout                       = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            imbPre.srcAccessMask                   = isUpdate ? VK_ACCESS_SHADER_READ_BIT : 0;
            imbPre.dstAccessMask                   = VK_ACCESS_TRANSFER_WRITE_BIT;
            imbPre.srcQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
            imbPre.dstQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
            imbPre.image                           = dstTex->image;
            imbPre.subresourceRange.aspectMask     = aspect;
            imbPre.subresourceRange.baseMipLevel   = reg.mipLevel;
            imbPre.subresourceRange.levelCount     = 1;
            imbPre.subresourceRange.baseArrayLayer = reg.baseLayer;
            imbPre.subresourceRange.layerCount     = std::max(1u, reg.layerCount);

            const VkPipelineStageFlags srcStage = isUpdate ? (VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT)
                                                           : VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
            vkCmdPipelineBarrier(cmd, srcStage, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &imbPre);

            VkBufferImageCopy region{};
            region.bufferOffset                    = staging[i].offset;
            region.bufferRowLength                 = 0;
            region.bufferImageHeight               = 0;
            region.imageSubresource.aspectMask     = aspect;
            region.imageSubresource.mipLevel       = reg.mipLevel;
            region.imageSubresource.baseArrayLayer = reg.baseLayer;
            region.imageSubresource.layerCount     = std::max(1u, reg.layerCount);
            region.imageOffset                     = {reg.offset.x, reg.offset.y, reg.offset.z};
            region.imageExtent                     = {copyExtent.width, copyExtent.height, copyExtent.depth};
            vkCmdCopyBufferToImage(cmd, stagingBuffer, dstTex->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

            VkImageMemoryBarrier imbPost{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
            imbPost.oldLayout                       = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            imbPost.newLayout                       = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            imbPost.srcAccessMask                   = VK_ACCESS_TRANSFER_WRITE_BIT;
            imbPost.dstAccessMask                   = VK_ACCESS_SHADER_READ_BIT;
            imbPost.srcQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
            imbPost.dstQueueFamilyIndex             = VK_QUEUE_FAMILY_IGNORED;
            imbPost.image                           = dstTex->image;
            imbPost.subresourceRange.aspectMask     = aspect;
            imbPost.subresourceRange.baseMipLevel   = reg.mipLevel;
            imbPost.subresourceRange.levelCount     = 1;
            imbPost.subresourceRange.baseArrayLayer = reg.baseLayer;
            imbPost.subresourceRange.layerCount     = std::max(1u, reg.layerCount);

            vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
                                 VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                                 0, 0, nullptr, 0, nullptr, 1, &imbPost);
        }
    }

    LUMORA_VK_CHECK(vkEndCommandBuffer(cmd));

    VkFenceCreateInfo fci{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    VkFence fence = VK_NULL_HANDLE;
    LUMORA_VK_CHECK(vkCreateFence(n.device, &fci, nullptr, &fence));

    VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    si.commandBufferCount = 1;
    si.pCommandBuffers    = &cmd;
    LUMORA_VK_CHECK(vkQueueSubmit(n.queue, 1, &si, fence));
    LUMORA_VK_CHECK(vkWaitForFences(n.device, 1, &fence, VK_TRUE, UINT64_MAX));

    vkDestroyFence(n.device, fence, nullptr);
    vkFreeCommandBuffers(n.device, n.immediatePool, 1, &cmd);
    vmaDestroyBuffer(n.allocator, stagingBuffer, stagingAlloc);
}

} // namespace

void RHIUploads::UploadBlocking(std::span<const UploadRequest> requests)
{
    if (!_n)
        return;
    ExecuteBlockingUploads(*_n, requests, false);
}

void RHIUploads::UpdateBlocking(std::span<const UploadRequest> requests)
{
    if (!_n)
        return;
    ExecuteBlockingUploads(*_n, requests, true);
}

} // namespace lumora::rhi
