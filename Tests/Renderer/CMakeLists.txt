set(_renderer_source "${LUMORA_ROOT_DIR}/Lumora/Renderer/Source")

# BatchBuilder compiled in directly: CPU only, no GPU needed.
lumora_add_test(lumora_batch_tests SOURCES BatchTests.cpp PaintGeometryTests.cpp ${_renderer_source}/BatchBuilder.cpp)
target_include_directories(lumora_batch_tests PRIVATE ${_renderer_source})

lumora_add_test(lumora_shared_image_tests SOURCES SharedImageTests.cpp LINK lumora::renderer lumora_rhi)

lumora_add_test(lumora_frame_packet_tests SOURCES FramePacketTests.cpp LINK lumora::renderer lumora_rhi lumora::platform)
