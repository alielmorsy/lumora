#include <lumora/rhi/RHI.h>
#include "VulkanRHI.h"
#include "Translate.h"

#include <algorithm>

namespace lumora::rhi {

BufferHandle RHIResources::CreateBuffer(const BufferDescription& desc)
{
    if (!_n)
        return {};
    detail::NativeRHI& n = *_n;

    VkBufferCreateInfo bci{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bci.size        = desc.size;
    bci.usage       = detail::ToVk(desc.usage);
    bci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo aci{};
    switch (desc.memory) {
    case MemoryPattern::CpuToGpu:
        aci.usage = desc.preferHost ? VMA_MEMORY_USAGE_AUTO_PREFER_HOST : VMA_MEMORY_USAGE_AUTO;
        aci.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;
        break;
    case MemoryPattern::CpuToGpuMapped:
        aci.usage = desc.preferHost ? VMA_MEMORY_USAGE_AUTO_PREFER_HOST : VMA_MEMORY_USAGE_AUTO;
        aci.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
        break;
    case MemoryPattern::GpuToCpu:
        aci.usage = VMA_MEMORY_USAGE_AUTO_PREFER_HOST;
        aci.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
        break;
    case MemoryPattern::GpuOnly:
    default:
        aci.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
        break;
    }

    VkBuffer buf = VK_NULL_HANDLE;
    VmaAllocation alloc = VK_NULL_HANDLE;
    VmaAllocationInfo allocInfo{};
    LUMORA_VK_CHECK(vmaCreateBuffer(n.allocator, &bci, &aci, &buf, &alloc, &allocInfo));

    if (!desc.debugName.empty())
        n.Name(VK_OBJECT_TYPE_BUFFER, reinterpret_cast<uint64_t>(buf), desc.debugName);

    detail::Buffer b{};
    b.buffer = buf;
    b.alloc  = alloc;
    b.mapped = allocInfo.pMappedData;
    if ((aci.flags & VMA_ALLOCATION_CREATE_MAPPED_BIT) && !b.mapped)
        vmaMapMemory(n.allocator, alloc, &b.mapped);
    b.desc   = desc;
    b.desc.debugName = {};

    return n.buffers.Add(std::move(b));
}

TextureHandle RHIResources::CreateTexture(const TextureDescription& desc)
{
    if (!_n)
        return {};
    detail::NativeRHI& n = *_n;

    VkImageCreateInfo ici{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    ici.imageType     = VK_IMAGE_TYPE_2D;
    ici.format        = detail::ToVk(desc.format);
    ici.extent        = {desc.extent.width, desc.extent.height, std::max(1u, desc.extent.depth)};
    ici.mipLevels     = std::max(1u, desc.mipLevels);
    ici.arrayLayers   = std::max(1u, desc.arrayLayers);
    ici.samples       = detail::ToVk(desc.samples);
    ici.tiling        = VK_IMAGE_TILING_OPTIMAL;
    ici.usage         = detail::ToVk(desc.usage);
    ici.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;
    ici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    if (desc.type == TextureType::eCube)
        ici.flags |= VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;

    VmaAllocationCreateInfo aci{};
    aci.usage = VMA_MEMORY_USAGE_AUTO;

    VkImage img = VK_NULL_HANDLE;
    VmaAllocation alloc = VK_NULL_HANDLE;
    LUMORA_VK_CHECK(vmaCreateImage(n.allocator, &ici, &aci, &img, &alloc, nullptr));

    VkImageViewType viewType = VK_IMAGE_VIEW_TYPE_2D;
    switch (desc.type) {
    case TextureType::e2D: viewType = VK_IMAGE_VIEW_TYPE_2D; break;
    case TextureType::e2DArray: viewType = VK_IMAGE_VIEW_TYPE_2D_ARRAY; break;
    case TextureType::eCube: viewType = VK_IMAGE_VIEW_TYPE_CUBE; break;
    }

    VkImageViewCreateInfo vci{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    vci.image                           = img;
    vci.viewType                        = viewType;
    vci.format                          = ici.format;
    vci.subresourceRange.aspectMask     = detail::AspectOf(desc.format);
    vci.subresourceRange.baseMipLevel   = 0;
    vci.subresourceRange.levelCount     = ici.mipLevels;
    vci.subresourceRange.baseArrayLayer = 0;
    vci.subresourceRange.layerCount     = ici.arrayLayers;

    VkImageView view = VK_NULL_HANDLE;
    LUMORA_VK_CHECK(vkCreateImageView(n.device, &vci, nullptr, &view));

    if (!desc.debugName.empty()) {
        n.Name(VK_OBJECT_TYPE_IMAGE, reinterpret_cast<uint64_t>(img), desc.debugName);
        n.Name(VK_OBJECT_TYPE_IMAGE_VIEW, reinterpret_cast<uint64_t>(view), desc.debugName);
    }

    detail::Texture t{};
    t.image          = img;
    t.alloc          = alloc;
    t.format         = ici.format;
    t.desc           = desc;
    t.desc.debugName = {};

    const TextureHandle th = n.textures.Add(std::move(t));
    const TextureViewHandle vh = n.views.Add(detail::TextureView{view, th});
    n.textures.Get(th)->defaultView = vh;
    return th;
}

SamplerHandle RHIResources::CreateSampler(const SamplerDescription& desc)
{
    if (!_n)
        return {};
    detail::NativeRHI& n = *_n;

    VkSamplerCreateInfo sci{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    sci.magFilter               = detail::ToVk(desc.magFilter);
    sci.minFilter               = detail::ToVk(desc.minFilter);
    sci.mipmapMode              = detail::ToVk(desc.mipmapMode);
    sci.addressModeU            = detail::ToVk(desc.addressU);
    sci.addressModeV            = detail::ToVk(desc.addressV);
    sci.addressModeW            = detail::ToVk(desc.addressW);
    sci.mipLodBias              = desc.mipLodBias;
    sci.anisotropyEnable        = desc.anisotropyEnable ? VK_TRUE : VK_FALSE;
    sci.maxAnisotropy           = desc.maxAnisotropy;
    sci.compareEnable           = VK_FALSE;
    sci.compareOp               = VK_COMPARE_OP_ALWAYS;
    sci.minLod                  = desc.minLod;
    sci.maxLod                  = desc.maxLod;
    sci.borderColor             = detail::ToVk(desc.borderColor);
    sci.unnormalizedCoordinates = VK_FALSE;

    VkSampler sampler = VK_NULL_HANDLE;
    LUMORA_VK_CHECK(vkCreateSampler(n.device, &sci, nullptr, &sampler));

    if (!desc.debugName.empty())
        n.Name(VK_OBJECT_TYPE_SAMPLER, reinterpret_cast<uint64_t>(sampler), desc.debugName);

    detail::Sampler s{};
    s.sampler = sampler;
    return n.samplers.Add(std::move(s));
}

TextureViewHandle RHIResources::GetDefaultView(TextureHandle texture)
{
    if (!_n)
        return {};
    if (const detail::Texture* t = _n->textures.Get(texture))
        return t->defaultView;
    return {};
}

void* RHIResources::GetMappedPointer(BufferHandle buffer) const
{
    if (!_n)
        return nullptr;
    if (const detail::Buffer* b = _n->buffers.Get(buffer))
        return b->mapped;
    return nullptr;
}

void RHIResources::FlushMapped(BufferHandle buffer, uint64_t offset, uint64_t size)
{
    if (!_n)
        return;
    if (const detail::Buffer* b = _n->buffers.Get(buffer); b && b->mapped)
        vmaFlushAllocation(_n->allocator, b->alloc, offset, size);
}

TextureDescription RHIResources::GetTextureDescription(TextureHandle texture) const
{
    if (!_n)
        return {};
    if (const detail::Texture* t = _n->textures.Get(texture))
        return t->desc;
    return {};
}

void RHIResources::Destroy(BufferHandle h)
{
    if (!_n)
        return;
    detail::NativeRHI& n = *_n;
    detail::Buffer b = n.buffers.Remove(h);
    if (!b.buffer)
        return;
    n.Defer([allocator = n.allocator, buffer = b.buffer, alloc = b.alloc]() {
        vmaDestroyBuffer(allocator, buffer, alloc);
    });
}

void RHIResources::Destroy(TextureHandle h)
{
    if (!_n)
        return;
    detail::NativeRHI& n = *_n;
    detail::Texture t = n.textures.Remove(h);
    if (!t.image)
        return;
    n.Defer([&n, t = std::move(t)]() mutable {
        n.DestroyTextureNow(t);
    });
}

void RHIResources::Destroy(SamplerHandle h)
{
    if (!_n)
        return;
    detail::NativeRHI& n = *_n;
    detail::Sampler s = n.samplers.Remove(h);
    if (!s.sampler)
        return;
    n.Defer([device = n.device, sampler = s.sampler]() {
        vkDestroySampler(device, sampler, nullptr);
    });
}

} // namespace lumora::rhi
