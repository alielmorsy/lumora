#include <lumora/rhi/RHI.h>
#include "VulkanRHI.h"
#include "Translate.h"

#include <vector>

namespace lumora::rhi {

DescriptorLayoutHandle RHIDescriptors::CreateDescriptorLayout(const DescriptorLayoutDescription& desc)
{
    if (!_n)
        return {};
    detail::NativeRHI& n = *_n;

    std::vector<VkDescriptorSetLayoutBinding> vkBindings;
    vkBindings.reserve(desc.bindings.size());
    for (const auto& b : desc.bindings) {
        VkDescriptorSetLayoutBinding vb{};
        vb.binding         = b.binding;
        vb.descriptorType  = detail::ToVk(b.type);
        vb.descriptorCount = b.count;
        vb.stageFlags      = detail::ToVk(b.stages);
        vkBindings.push_back(vb);
    }

    const bool usePush = desc.pushDescriptor && n.adapter.pushDescriptor;

    VkDescriptorSetLayoutCreateInfo ci{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    if (usePush)
        ci.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT_KHR;
    ci.bindingCount = static_cast<uint32_t>(vkBindings.size());
    ci.pBindings    = vkBindings.data();

    VkDescriptorSetLayout layout = VK_NULL_HANDLE;
    LUMORA_VK_CHECK(vkCreateDescriptorSetLayout(n.device, &ci, nullptr, &layout));

    if (!desc.debugName.empty())
        n.Name(VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT, reinterpret_cast<uint64_t>(layout), desc.debugName);

    detail::DescriptorLayout dl{};
    dl.layout   = layout;
    dl.push     = usePush;
    dl.bindings.assign(desc.bindings.begin(), desc.bindings.end());

    return n.descriptorLayouts.Add(std::move(dl));
}

void RHIDescriptors::Destroy(DescriptorLayoutHandle h)
{
    if (!_n)
        return;
    detail::NativeRHI& n = *_n;
    detail::DescriptorLayout dl = n.descriptorLayouts.Remove(h);
    if (!dl.layout)
        return;
    n.Defer([device = n.device, layout = dl.layout]() {
        vkDestroyDescriptorSetLayout(device, layout, nullptr);
    });
}

} // namespace lumora::rhi
