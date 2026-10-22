#pragma once

#include "VulkanRHI.h"

namespace lumora::rhi::detail {

inline VkFormat ToVk(Format f)
{
    switch (f) {
    case Format::R8_Unorm: return VK_FORMAT_R8_UNORM;
    case Format::R8G8_Unorm: return VK_FORMAT_R8G8_UNORM;
    case Format::R8G8B8A8_Unorm: return VK_FORMAT_R8G8B8A8_UNORM;
    case Format::R8G8B8A8_Srgb: return VK_FORMAT_R8G8B8A8_SRGB;
    case Format::B8G8R8A8_Unorm: return VK_FORMAT_B8G8R8A8_UNORM;
    case Format::B8G8R8A8_Srgb: return VK_FORMAT_B8G8R8A8_SRGB;
    case Format::R16_Sfloat: return VK_FORMAT_R16_SFLOAT;
    case Format::R16G16B16A16_Sfloat: return VK_FORMAT_R16G16B16A16_SFLOAT;
    case Format::R32_Uint: return VK_FORMAT_R32_UINT;
    case Format::R32_Sfloat: return VK_FORMAT_R32_SFLOAT;
    case Format::R32G32_Sfloat: return VK_FORMAT_R32G32_SFLOAT;
    case Format::R32G32B32_Sfloat: return VK_FORMAT_R32G32B32_SFLOAT;
    case Format::R32G32B32A32_Sfloat: return VK_FORMAT_R32G32B32A32_SFLOAT;
    case Format::D32_Sfloat: return VK_FORMAT_D32_SFLOAT;
    case Format::D24_Unorm_S8_Uint: return VK_FORMAT_D24_UNORM_S8_UINT;
    default: return VK_FORMAT_UNDEFINED;
    }
}

inline Format FromVk(VkFormat f)
{
    switch (f) {
    case VK_FORMAT_R8G8B8A8_UNORM: return Format::R8G8B8A8_Unorm;
    case VK_FORMAT_R8G8B8A8_SRGB: return Format::R8G8B8A8_Srgb;
    case VK_FORMAT_B8G8R8A8_UNORM: return Format::B8G8R8A8_Unorm;
    case VK_FORMAT_B8G8R8A8_SRGB: return Format::B8G8R8A8_Srgb;
    default: return Format::Undefined;
    }
}

inline bool IsDepth(Format f) { return f == Format::D32_Sfloat || f == Format::D24_Unorm_S8_Uint; }

inline VkImageAspectFlags AspectOf(Format f)
{
    if (f == Format::D24_Unorm_S8_Uint)
        return VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
    return f == Format::D32_Sfloat ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
}

inline VkImageLayout ToVk(ImageLayout l)
{
    switch (l) {
    case ImageLayout::General: return VK_IMAGE_LAYOUT_GENERAL;
    case ImageLayout::ColorAttachment: return VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    case ImageLayout::DepthStencilAttachment: return VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    case ImageLayout::ShaderReadOnly: return VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    case ImageLayout::TransferSrc: return VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    case ImageLayout::TransferDst: return VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    case ImageLayout::PresentSrc: return VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    default: return VK_IMAGE_LAYOUT_UNDEFINED;
    }
}

inline VkPipelineStageFlags ToVk(PipelineStage s)
{
    VkPipelineStageFlags r = 0;
    if (AnyFlag(s, PipelineStage::TopOfPipe)) r |= VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
    if (AnyFlag(s, PipelineStage::VertexInput)) r |= VK_PIPELINE_STAGE_VERTEX_INPUT_BIT;
    if (AnyFlag(s, PipelineStage::VertexShader)) r |= VK_PIPELINE_STAGE_VERTEX_SHADER_BIT;
    if (AnyFlag(s, PipelineStage::FragmentShader)) r |= VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    if (AnyFlag(s, PipelineStage::ColorAttachment)) r |= VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    if (AnyFlag(s, PipelineStage::ComputeShader)) r |= VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
    if (AnyFlag(s, PipelineStage::Transfer)) r |= VK_PIPELINE_STAGE_TRANSFER_BIT;
    if (AnyFlag(s, PipelineStage::BottomOfPipe)) r |= VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
    if (AnyFlag(s, PipelineStage::Host)) r |= VK_PIPELINE_STAGE_HOST_BIT;
    if (AnyFlag(s, PipelineStage::AllGraphics)) r |= VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT;
    if (AnyFlag(s, PipelineStage::AllCommands)) r |= VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
    return r;
}

inline VkAccessFlags ToVk(AccessMask a)
{
    VkAccessFlags r = 0;
    if (AnyFlag(a, AccessMask::IndexRead)) r |= VK_ACCESS_INDEX_READ_BIT;
    if (AnyFlag(a, AccessMask::VertexAttributeRead)) r |= VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT;
    if (AnyFlag(a, AccessMask::UniformRead)) r |= VK_ACCESS_UNIFORM_READ_BIT;
    if (AnyFlag(a, AccessMask::ShaderRead)) r |= VK_ACCESS_SHADER_READ_BIT;
    if (AnyFlag(a, AccessMask::ShaderWrite)) r |= VK_ACCESS_SHADER_WRITE_BIT;
    if (AnyFlag(a, AccessMask::ColorAttachmentRead)) r |= VK_ACCESS_COLOR_ATTACHMENT_READ_BIT;
    if (AnyFlag(a, AccessMask::ColorAttachmentWrite)) r |= VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    if (AnyFlag(a, AccessMask::TransferRead)) r |= VK_ACCESS_TRANSFER_READ_BIT;
    if (AnyFlag(a, AccessMask::TransferWrite)) r |= VK_ACCESS_TRANSFER_WRITE_BIT;
    if (AnyFlag(a, AccessMask::HostRead)) r |= VK_ACCESS_HOST_READ_BIT;
    if (AnyFlag(a, AccessMask::HostWrite)) r |= VK_ACCESS_HOST_WRITE_BIT;
    if (AnyFlag(a, AccessMask::MemoryRead)) r |= VK_ACCESS_MEMORY_READ_BIT;
    if (AnyFlag(a, AccessMask::MemoryWrite)) r |= VK_ACCESS_MEMORY_WRITE_BIT;
    return r;
}

inline VkShaderStageFlags ToVk(ShaderStageFlags s)
{
    VkShaderStageFlags r = 0;
    if (AnyFlag(s, ShaderStageFlags::Vertex)) r |= VK_SHADER_STAGE_VERTEX_BIT;
    if (AnyFlag(s, ShaderStageFlags::Fragment)) r |= VK_SHADER_STAGE_FRAGMENT_BIT;
    if (AnyFlag(s, ShaderStageFlags::Compute)) r |= VK_SHADER_STAGE_COMPUTE_BIT;
    return r;
}

inline VkImageUsageFlags ToVk(TextureUsage u)
{
    VkImageUsageFlags r = 0;
    if (AnyFlag(u, TextureUsage::TransferSrc)) r |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    if (AnyFlag(u, TextureUsage::TransferDst)) r |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    if (AnyFlag(u, TextureUsage::Sampled)) r |= VK_IMAGE_USAGE_SAMPLED_BIT;
    if (AnyFlag(u, TextureUsage::Storage)) r |= VK_IMAGE_USAGE_STORAGE_BIT;
    if (AnyFlag(u, TextureUsage::ColorAttachment)) r |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    if (AnyFlag(u, TextureUsage::DepthAttachment)) r |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    return r;
}

inline VkBufferUsageFlags ToVk(BufferUsage u)
{
    VkBufferUsageFlags r = 0;
    if (AnyFlag(u, BufferUsage::TransferSrc)) r |= VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    if (AnyFlag(u, BufferUsage::TransferDst)) r |= VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    if (AnyFlag(u, BufferUsage::Uniform)) r |= VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
    if (AnyFlag(u, BufferUsage::Storage)) r |= VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    if (AnyFlag(u, BufferUsage::Index)) r |= VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
    if (AnyFlag(u, BufferUsage::Vertex)) r |= VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    if (AnyFlag(u, BufferUsage::Indirect)) r |= VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT;
    return r;
}

inline VkColorComponentFlags ToVk(ColorComponentFlags c) { return static_cast<VkColorComponentFlags>(c); }

inline VkDescriptorType ToVk(DescriptorType t)
{
    switch (t) {
    case DescriptorType::Sampler: return VK_DESCRIPTOR_TYPE_SAMPLER;
    case DescriptorType::CombinedImageSampler: return VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    case DescriptorType::SampledImage: return VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
    case DescriptorType::StorageImage: return VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    case DescriptorType::UniformBuffer: return VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    case DescriptorType::StorageBuffer: return VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    }
    return VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
}

inline VkFilter ToVk(Filter f) { return f == Filter::Nearest ? VK_FILTER_NEAREST : VK_FILTER_LINEAR; }

inline VkSamplerMipmapMode ToVk(MipmapMode m)
{
    return m == MipmapMode::Nearest ? VK_SAMPLER_MIPMAP_MODE_NEAREST : VK_SAMPLER_MIPMAP_MODE_LINEAR;
}

inline VkSamplerAddressMode ToVk(AddressMode m)
{
    switch (m) {
    case AddressMode::MirroredRepeat: return VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
    case AddressMode::ClampToEdge: return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    case AddressMode::ClampToBorder: return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    default: return VK_SAMPLER_ADDRESS_MODE_REPEAT;
    }
}

inline VkBorderColor ToVk(BorderColor c)
{
    switch (c) {
    case BorderColor::TransparentBlack: return VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK;
    case BorderColor::OpaqueWhite: return VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
    default: return VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK;
    }
}

inline VkCompareOp ToVk(CompareOp op) { return static_cast<VkCompareOp>(op); } // same order as Vulkan

inline VkAttachmentLoadOp ToVk(LoadOp op)
{
    switch (op) {
    case LoadOp::Clear: return VK_ATTACHMENT_LOAD_OP_CLEAR;
    case LoadOp::DontCare: return VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    default: return VK_ATTACHMENT_LOAD_OP_LOAD;
    }
}

inline VkAttachmentStoreOp ToVk(StoreOp op)
{
    return op == StoreOp::Store ? VK_ATTACHMENT_STORE_OP_STORE : VK_ATTACHMENT_STORE_OP_DONT_CARE;
}

inline VkIndexType ToVk(IndexType t) { return t == IndexType::Uint16 ? VK_INDEX_TYPE_UINT16 : VK_INDEX_TYPE_UINT32; }

inline VkPrimitiveTopology ToVk(PrimitiveTopology t)
{
    switch (t) {
    case PrimitiveTopology::PointList: return VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
    case PrimitiveTopology::LineList: return VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
    case PrimitiveTopology::LineStrip: return VK_PRIMITIVE_TOPOLOGY_LINE_STRIP;
    case PrimitiveTopology::TriangleStrip: return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
    default: return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    }
}

inline VkPolygonMode ToVk(PolygonMode m) { return m == PolygonMode::Line ? VK_POLYGON_MODE_LINE : VK_POLYGON_MODE_FILL; }

inline VkCullModeFlags ToVk(CullMode m)
{
    switch (m) {
    case CullMode::Front: return VK_CULL_MODE_FRONT_BIT;
    case CullMode::Back: return VK_CULL_MODE_BACK_BIT;
    default: return VK_CULL_MODE_NONE;
    }
}

inline VkFrontFace ToVk(FrontFace f)
{
    return f == FrontFace::Clockwise ? VK_FRONT_FACE_CLOCKWISE : VK_FRONT_FACE_COUNTER_CLOCKWISE;
}

inline VkBlendFactor ToVk(BlendFactor f) { return static_cast<VkBlendFactor>(f); } // same order as Vulkan

inline VkBlendOp ToVk(BlendOp op) { return static_cast<VkBlendOp>(op); } // same order as Vulkan

inline VkPresentModeKHR ToVk(PresentMode m)
{
    switch (m) {
    case PresentMode::Immediate: return VK_PRESENT_MODE_IMMEDIATE_KHR;
    case PresentMode::Mailbox: return VK_PRESENT_MODE_MAILBOX_KHR;
    case PresentMode::FifoRelaxed: return VK_PRESENT_MODE_FIFO_RELAXED_KHR;
    default: return VK_PRESENT_MODE_FIFO_KHR;
    }
}

inline VkSampleCountFlagBits ToVk(SampleCount s) { return static_cast<VkSampleCountFlagBits>(s); }

} // namespace lumora::rhi::detail
