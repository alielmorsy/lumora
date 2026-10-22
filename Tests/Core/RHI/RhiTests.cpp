#include <gtest/gtest.h>
#include <lumora/log/Log.h>
#include <lumora/rhi/RHI.h>

#include <atomic>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <span>
#include <vector>

namespace {

std::vector<std::byte> LoadSpirv(const std::filesystem::path& path)
{
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open())
        return {};
    const auto size = file.tellg();
    file.seekg(0, std::ios::beg);
    std::vector<std::byte> buffer(size);
    file.read(reinterpret_cast<char*>(buffer.data()), size);
    return buffer;
}

class RhiTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        if (lumora::rhi::EnumerateAdapters().empty()) {
            GTEST_SKIP() << "No Vulkan adapters available";
        }
        validationError = false;
        sinkHandle = lumora::log::AddSink([this](lumora::log::Level level, const char*, const char*) {
            if (level >= lumora::log::Level::Error) {
                validationError = true;
            }
        });
    }

    void TearDown() override
    {
        if (sinkHandle) {
            lumora::log::RemoveSink(sinkHandle);
            sinkHandle = 0;
        }
        EXPECT_FALSE(validationError.load()) << "Vulkan validation or RHI error logged during test";
    }

    std::atomic<bool> validationError{false};
    lumora::log::SinkHandle sinkHandle = 0;
};

} // namespace

TEST_F(RhiTest, InitShutdown)
{
    lumora::rhi::RHI rhi;
    lumora::rhi::RHIInitDescription init{};
    init.enableValidation = true;
    rhi.Init(init);
    EXPECT_TRUE(rhi.IsInitialised());
    EXPECT_GT(rhi.GetFramesInFlight(), 0u);
    rhi.Shutdown();
    EXPECT_FALSE(rhi.IsInitialised());
}

TEST_F(RhiTest, BufferCreateMappedWriteDestroy)
{
    lumora::rhi::RHI rhi;
    lumora::rhi::RHIInitDescription init{};
    init.enableValidation = true;
    rhi.Init(init);

    lumora::rhi::BufferDescription desc{};
    desc.size      = 1024;
    desc.usage     = lumora::rhi::BufferUsage::Uniform | lumora::rhi::BufferUsage::TransferDst;
    desc.memory    = lumora::rhi::MemoryPattern::CpuToGpuMapped;
    desc.debugName = "TestBuffer";

    auto buf = rhi.Resources().CreateBuffer(desc);
    EXPECT_TRUE(buf.IsValid());

    void* ptr = rhi.Resources().GetMappedPointer(buf);
    ASSERT_NE(ptr, nullptr);
    const uint32_t testData = 0x12345678;
    std::memcpy(ptr, &testData, sizeof(testData));
    EXPECT_EQ(*static_cast<const uint32_t*>(ptr), testData);

    rhi.Resources().Destroy(buf);
    EXPECT_EQ(rhi.Resources().GetMappedPointer(buf), nullptr);
    rhi.Shutdown();
}

TEST_F(RhiTest, TextureCreateUploadUpdate)
{
    lumora::rhi::RHI rhi;
    lumora::rhi::RHIInitDescription init{};
    init.enableValidation = true;
    rhi.Init(init);

    lumora::rhi::TextureDescription desc{};
    desc.type      = lumora::rhi::TextureType::e2D;
    desc.extent    = {4, 4, 1};
    desc.format    = lumora::rhi::Format::R8G8B8A8_Unorm;
    desc.usage     = lumora::rhi::TextureUsage::Sampled | lumora::rhi::TextureUsage::TransferDst;
    desc.debugName = "TestTexture";

    auto tex = rhi.Resources().CreateTexture(desc);
    EXPECT_TRUE(tex.IsValid());
    EXPECT_TRUE(rhi.Resources().GetDefaultView(tex).IsValid());

    const auto storedDesc = rhi.Resources().GetTextureDescription(tex);
    EXPECT_EQ(storedDesc.extent.width, 4u);
    EXPECT_EQ(storedDesc.extent.height, 4u);

    uint32_t pixels[16];
    std::fill_n(pixels, 16, 0xFF0000FF);

    lumora::rhi::UploadRequest uploadReq{};
    uploadReq.dstTexture = tex;
    uploadReq.data       = pixels;
    uploadReq.size       = sizeof(pixels);
    rhi.Uploads().UploadBlocking(std::span(&uploadReq, 1));

    std::fill_n(pixels, 16, 0x00FF00FF);
    lumora::rhi::UploadRequest updateReq{};
    updateReq.dstTexture = tex;
    updateReq.data       = pixels;
    updateReq.size       = sizeof(pixels);
    rhi.Uploads().UpdateBlocking(std::span(&updateReq, 1));

    rhi.Resources().Destroy(tex);
    rhi.Shutdown();
}

TEST_F(RhiTest, SamplerCreateDestroy)
{
    lumora::rhi::RHI rhi;
    lumora::rhi::RHIInitDescription init{};
    init.enableValidation = true;
    rhi.Init(init);

    lumora::rhi::SamplerDescription desc{};
    desc.magFilter = lumora::rhi::Filter::Linear;
    desc.minFilter = lumora::rhi::Filter::Nearest;
    desc.addressU  = lumora::rhi::AddressMode::ClampToEdge;
    desc.addressV  = lumora::rhi::AddressMode::ClampToEdge;
    desc.debugName = "TestSampler";

    auto sampler = rhi.Resources().CreateSampler(desc);
    EXPECT_TRUE(sampler.IsValid());

    rhi.Resources().Destroy(sampler);
    rhi.Shutdown();
}

TEST_F(RhiTest, DescriptorLayoutPushAndNonPush)
{
    lumora::rhi::RHI rhi;
    lumora::rhi::RHIInitDescription init{};
    init.enableValidation = true;
    rhi.Init(init);

    lumora::rhi::DescriptorBinding binding{
        .binding = 0,
        .type    = lumora::rhi::DescriptorType::UniformBuffer,
        .count   = 1,
        .stages  = lumora::rhi::ShaderStageFlags::All,
    };

    lumora::rhi::DescriptorLayoutDescription desc1{
        .bindings       = std::span(&binding, 1),
        .pushDescriptor = false,
        .debugName      = "NormalLayout",
    };
    auto h1 = rhi.Descriptors().CreateDescriptorLayout(desc1);
    EXPECT_TRUE(h1.IsValid());
    rhi.Descriptors().Destroy(h1);

    lumora::rhi::DescriptorLayoutDescription desc2{
        .bindings       = std::span(&binding, 1),
        .pushDescriptor = true,
        .debugName      = "PushLayout",
    };
    auto h2 = rhi.Descriptors().CreateDescriptorLayout(desc2);
    EXPECT_TRUE(h2.IsValid());
    rhi.Descriptors().Destroy(h2);

    rhi.Shutdown();
}

TEST_F(RhiTest, PipelineCreateDestroy)
{
    lumora::rhi::RHI rhi;
    lumora::rhi::RHIInitDescription init{};
    init.enableValidation = true;
    rhi.Init(init);

    const auto vertSpv = LoadSpirv(std::filesystem::path(TEST_SPIRV_DIR) / "TestShader.vert.spv");
    const auto fragSpv = LoadSpirv(std::filesystem::path(TEST_SPIRV_DIR) / "TestShader.frag.spv");
    ASSERT_FALSE(vertSpv.empty());
    ASSERT_FALSE(fragSpv.empty());

    lumora::rhi::ShaderStageDesc stages[2] = {
        {lumora::rhi::ShaderStageFlags::Vertex, vertSpv, "VSMain"},
        {lumora::rhi::ShaderStageFlags::Fragment, fragSpv, "PSMain"},
    };

    lumora::rhi::DescriptorBinding binding{
        .binding = 0,
        .type    = lumora::rhi::DescriptorType::UniformBuffer,
        .count   = 1,
        .stages  = lumora::rhi::ShaderStageFlags::Vertex,
    };
    lumora::rhi::DescriptorLayoutDescription dlDesc{
        .bindings       = std::span(&binding, 1),
        .pushDescriptor = false,
    };
    auto dl = rhi.Descriptors().CreateDescriptorLayout(dlDesc);
    ASSERT_TRUE(dl.IsValid());

    lumora::rhi::Format colorFormat = lumora::rhi::Format::R8G8B8A8_Unorm;
    lumora::rhi::BlendAttachment blend{};
    blend.writeMask = lumora::rhi::ColorComponentFlags::RGBA;

    lumora::rhi::PipelineDescription pDesc{};
    pDesc.type              = lumora::rhi::PipelineType::Graphics;
    pDesc.stages            = stages;
    pDesc.blend.attachments = std::span(&blend, 1);
    pDesc.setLayouts        = std::span(&dl, 1);
    pDesc.colorFormats      = std::span(&colorFormat, 1);
    pDesc.debugName         = "TestPipeline";

    auto pipe = rhi.Pipelines().CreatePipeline(pDesc);
    EXPECT_TRUE(pipe.IsValid());

    rhi.Pipelines().Destroy(pipe);
    rhi.Descriptors().Destroy(dl);

    rhi.Shutdown();
}

TEST_F(RhiTest, PipelineCreateWithVertexInput)
{
    lumora::rhi::RHI rhi;
    lumora::rhi::RHIInitDescription init{};
    init.enableValidation = true;
    rhi.Init(init);

    const auto vertSpv = LoadSpirv(std::filesystem::path(TEST_SPIRV_DIR) / "TestShaderAttr.vert.spv");
    const auto fragSpv = LoadSpirv(std::filesystem::path(TEST_SPIRV_DIR) / "TestShader.frag.spv");
    ASSERT_FALSE(vertSpv.empty());
    ASSERT_FALSE(fragSpv.empty());

    lumora::rhi::ShaderStageDesc stages[2] = {
        {lumora::rhi::ShaderStageFlags::Vertex, vertSpv, "VSMainWithAttr"},
        {lumora::rhi::ShaderStageFlags::Fragment, fragSpv, "PSMain"},
    };

    lumora::rhi::VertexBinding vb{
        .binding     = 0,
        .stride      = sizeof(float) * 2,
        .perInstance = false,
    };
    lumora::rhi::VertexAttribute va{
        .location = 0,
        .binding  = 0,
        .format   = lumora::rhi::Format::R32G32_Sfloat,
        .offset   = 0,
    };
    lumora::rhi::VertexLayout vertexLayout{
        .bindings   = std::span(&vb, 1),
        .attributes = std::span(&va, 1),
    };

    lumora::rhi::Format colorFormat = lumora::rhi::Format::R8G8B8A8_Unorm;
    lumora::rhi::BlendAttachment blend{};
    blend.writeMask = lumora::rhi::ColorComponentFlags::RGBA;

    lumora::rhi::PipelineDescription pDesc{};
    pDesc.type              = lumora::rhi::PipelineType::Graphics;
    pDesc.stages            = stages;
    pDesc.vertex            = vertexLayout;
    pDesc.blend.attachments = std::span(&blend, 1);
    pDesc.colorFormats      = std::span(&colorFormat, 1);
    pDesc.debugName         = "TestPipelineWithAttr";

    auto pipe = rhi.Pipelines().CreatePipeline(pDesc);
    EXPECT_TRUE(pipe.IsValid());

    rhi.Pipelines().Destroy(pipe);
    rhi.Shutdown();
}

TEST_F(RhiTest, DeferredDestroyAcrossFramesNoSwapchain)
{
    lumora::rhi::RHI rhi;
    lumora::rhi::RHIInitDescription init{};
    init.enableValidation = true;
    init.framesInFlight   = 2;
    rhi.Init(init);

    lumora::rhi::BufferDescription desc{};
    desc.size   = 256;
    desc.usage  = lumora::rhi::BufferUsage::Uniform;
    desc.memory = lumora::rhi::MemoryPattern::CpuToGpuMapped;
    auto buf = rhi.Resources().CreateBuffer(desc);
    EXPECT_TRUE(buf.IsValid());

    auto ctx = rhi.BeginFrame({});
    EXPECT_EQ(ctx.cmd->FrameSlot(), 0u);
    EXPECT_FALSE(ctx.swapchainImage.IsValid());

    rhi.Resources().Destroy(buf);
    EXPECT_EQ(rhi.Resources().GetMappedPointer(buf), nullptr);

    rhi.EndFrame(ctx);

    for (uint32_t i = 1; i <= rhi.GetFramesInFlight() + 1; ++i) {
        auto frame = rhi.BeginFrame({});
        rhi.EndFrame(frame);
    }

    rhi.Shutdown();
}
