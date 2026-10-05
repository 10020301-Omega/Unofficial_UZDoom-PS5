/*
** ps5_launcher.cpp
**
** The launcher on the console: reads the pad, lets the launcher
** (ps5/launcher/) draw its 80 x 25 text screen, and shows that screen on
** the display with Vulkan. The text screen is a 640 x 400 texture drawn to
** the swapchain at the largest whole-number scale, so every pixel of the
** DOS font stays square and sharp at 3840 x 2160.
**
** It runs before the engine, on a Vulkan device of its own, and releases
** the device before returning; the instance and the display surface are
** kept for the engine (ps5_video.cpp).
**
** Copyright 2026 PS5-UZDOOM port contributors
** SPDX-License-Identifier: GPL-3.0-or-later
*/

#include <zvulkan/vulkanbuilders.h>
#include <zvulkan/vulkandevice.h>
#include <zvulkan/vulkaninstance.h>
#include <zvulkan/vulkanobjects.h>
#include <zvulkan/vulkansurface.h>
#include <zvulkan/vulkanswapchain.h>

#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "launcher.h"
#include "ps5_display.h"
#include "ps5_launcher.h"
#include "ps5_paths.h"
#include "textscreen.h"

extern "C"
{
#include "console/platform.h"
}

namespace
{

const char *const VertexShader = R"(
layout(location = 0) out vec2 uv;
void main()
{
	// One triangle that covers the viewport
	vec2 p = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
	uv = p;
	gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
}
)";

const char *const FragmentShader = R"(
layout(binding = 0) uniform sampler2D screen;
layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 color;
void main()
{
	color = vec4(texture(screen, uv).rgb, 1.0);
}
)";

//==========================================================================
//
// The text screen on the display
//
//==========================================================================

class TextPresenter
{
public:
	TextPresenter()
	{
		surface = PS5_AcquireSurface();

		VulkanDeviceBuilder deviceBuilder;
		deviceBuilder.Surface(surface);
		device = deviceBuilder.Create(surface->Instance);

		int width = 0, height = 0;
		PS5_GetDisplaySize(&width, &height);
		swapchain = VulkanSwapChainBuilder().Create(device.get());
		swapchain->Create(width, height, 3, true, false, false);
		if (swapchain->Lost() || swapchain->ImageCount() == 0)
			VulkanError("The launcher could not create a swapchain");

		const int tw = dosui::TextScreen::PixelWidth, th = dosui::TextScreen::PixelHeight;
		pixels.resize((size_t)tw * th * 4);

		staging = BufferBuilder()
			.Size(pixels.size())
			.Usage(VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_CPU_ONLY)
			.Create(device.get());
		image = ImageBuilder()
			.Size(tw, th)
			.Format(VK_FORMAT_R8G8B8A8_UNORM)
			.Usage(VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT)
			.Create(device.get());
		view = ImageViewBuilder()
			.Image(image.get(), VK_FORMAT_R8G8B8A8_UNORM)
			.Create(device.get());
		sampler = SamplerBuilder()
			.MinFilter(VK_FILTER_NEAREST)
			.MagFilter(VK_FILTER_NEAREST)
			.MipmapMode(VK_SAMPLER_MIPMAP_MODE_NEAREST)
			.AddressMode(VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE)
			.Create(device.get());

		setLayout = DescriptorSetLayoutBuilder()
			.AddBinding(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT)
			.Create(device.get());
		descriptorPool = DescriptorPoolBuilder()
			.AddPoolSize(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1)
			.MaxSets(1)
			.Create(device.get());
		descriptorSet = descriptorPool->allocate(setLayout.get());
		WriteDescriptors()
			.AddCombinedImageSampler(descriptorSet.get(), 0, view.get(), sampler.get(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
			.Execute(device.get());

		renderPass = RenderPassBuilder()
			.AddAttachment(swapchain->Format().format, VK_SAMPLE_COUNT_1_BIT, VK_ATTACHMENT_LOAD_OP_CLEAR,
				VK_ATTACHMENT_STORE_OP_STORE, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR)
			.AddSubpass()
			.AddSubpassColorAttachmentRef(0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL)
			.AddExternalSubpassDependency(
				VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
				0, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT)
			.Create(device.get());
		for (int i = 0; i < swapchain->ImageCount(); i++)
		{
			framebuffers.push_back(FramebufferBuilder()
				.RenderPass(renderPass.get())
				.AddAttachment(swapchain->GetImageView(i))
				.Size(swapchain->Width(), swapchain->Height())
				.Create(device.get()));
		}

		const std::string version = "#version 450\n";
		vertexShader = ShaderBuilder()
			.Type(ShaderType::Vertex)
			.AddSource("launcher.vert", version + VertexShader)
			.Create("launcher.vert", device.get());
		fragmentShader = ShaderBuilder()
			.Type(ShaderType::Fragment)
			.AddSource("launcher.frag", version + FragmentShader)
			.Create("launcher.frag", device.get());
		pipelineLayout = PipelineLayoutBuilder()
			.AddSetLayout(setLayout.get())
			.Create(device.get());
		pipeline = GraphicsPipelineBuilder()
			.RenderPass(renderPass.get())
			.Layout(pipelineLayout.get())
			.AddVertexShader(vertexShader.get())
			.AddFragmentShader(fragmentShader.get())
			.Topology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST)
			.Cull(VK_CULL_MODE_NONE, VK_FRONT_FACE_COUNTER_CLOCKWISE)
			.AddColorBlendAttachment(ColorBlendAttachmentBuilder().Create())
			.AddDynamicState(VK_DYNAMIC_STATE_VIEWPORT)
			.AddDynamicState(VK_DYNAMIC_STATE_SCISSOR)
			.Create(device.get());

		commandPool = CommandPoolBuilder()
			.QueueFamily(device->GraphicsFamily)
			.Create(device.get());
		imageAvailable = SemaphoreBuilder().Create(device.get());
		renderFinished = SemaphoreBuilder().Create(device.get());
		submitted = FenceBuilder().Create(device.get());

		// The whole-number scale that fits, centred; the rest stays black,
		// as the border of a text mode screen was.
		int scale = std::min(swapchain->Width() / tw, swapchain->Height() / th);
		if (scale < 1)
			scale = 1;
		viewport.width = (float)(tw * scale);
		viewport.height = (float)(th * scale);
		viewport.x = (float)((swapchain->Width() - tw * scale) / 2);
		viewport.y = (float)((swapchain->Height() - th * scale) / 2);
		viewport.minDepth = 0.0f;
		viewport.maxDepth = 1.0f;
		say("launcher: %d x %d display, text screen at %dx (%d x %d)", swapchain->Width(), swapchain->Height(),
			scale, tw * scale, th * scale);
	}

	~TextPresenter()
	{
		if (device)
			vkDeviceWaitIdle(device->device);
		// In the order Vulkan wants: what uses a thing goes before it.
		commandBuffer.reset();
		commandPool.reset();
		submitted.reset();
		renderFinished.reset();
		imageAvailable.reset();
		pipeline.reset();
		pipelineLayout.reset();
		fragmentShader.reset();
		vertexShader.reset();
		framebuffers.clear();
		renderPass.reset();
		descriptorSet.reset();
		descriptorPool.reset();
		setLayout.reset();
		sampler.reset();
		view.reset();
		image.reset();
		staging.reset();
		swapchain.reset();
		device.reset();
	}

	// One frame; blocks until the display has taken it (FIFO).
	void Present(const dosui::TextScreen &screen)
	{
		screen.Rasterize(pixels.data());
		void *mapped = staging->Map(0, pixels.size());
		memcpy(mapped, pixels.data(), pixels.size());
		staging->Unmap();

		const int index = swapchain->AcquireImage(imageAvailable.get());
		if (index < 0)
		{
			// Nothing to draw on this time; try again next frame.
			usleep(16000);
			return;
		}

		commandBuffer = commandPool->createBuffer();
		commandBuffer->begin();

		PipelineBarrier()
			.AddImage(image.get(), imageInitialised ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL : VK_IMAGE_LAYOUT_UNDEFINED,
				VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 0, VK_ACCESS_TRANSFER_WRITE_BIT)
			.Execute(commandBuffer.get(), VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
				VK_PIPELINE_STAGE_TRANSFER_BIT);
		VkBufferImageCopy region = {};
		region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		region.imageSubresource.layerCount = 1;
		region.imageExtent.width = dosui::TextScreen::PixelWidth;
		region.imageExtent.height = dosui::TextScreen::PixelHeight;
		region.imageExtent.depth = 1;
		commandBuffer->copyBufferToImage(staging->buffer, image->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
		PipelineBarrier()
			.AddImage(image.get(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
				VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT)
			.Execute(commandBuffer.get(), VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
		imageInitialised = true;

		RenderPassBegin()
			.RenderPass(renderPass.get())
			.Framebuffer(framebuffers[index].get())
			.RenderArea(0, 0, swapchain->Width(), swapchain->Height())
			.AddClearColor(0.0f, 0.0f, 0.0f, 1.0f)
			.Execute(commandBuffer.get());
		VkRect2D scissor = {};
		scissor.extent.width = (uint32_t)swapchain->Width();
		scissor.extent.height = (uint32_t)swapchain->Height();
		commandBuffer->setViewport(0, 1, &viewport);
		commandBuffer->setScissor(0, 1, &scissor);
		commandBuffer->bindPipeline(VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline.get());
		commandBuffer->bindDescriptorSet(VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout.get(), 0, descriptorSet.get());
		commandBuffer->draw(3, 1, 0, 0);
		commandBuffer->endRenderPass();
		commandBuffer->end();

		vkResetFences(device->device, 1, &submitted->fence);
		QueueSubmit()
			.AddCommandBuffer(commandBuffer.get())
			.AddWait(VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, imageAvailable.get())
			.AddSignal(renderFinished.get())
			.Execute(device.get(), device->GraphicsQueue, submitted.get());
		swapchain->QueuePresent(index, renderFinished.get());
		// The command buffer is recorded again next frame: wait until the
		// GPU is done with this one.
		vkWaitForFences(device->device, 1, &submitted->fence, VK_TRUE, 1'000'000'000);
	}

private:
	std::shared_ptr<VulkanSurface> surface;
	std::shared_ptr<VulkanDevice> device;
	std::shared_ptr<VulkanSwapChain> swapchain;
	std::unique_ptr<VulkanBuffer> staging;
	std::unique_ptr<VulkanImage> image;
	std::unique_ptr<VulkanImageView> view;
	std::unique_ptr<VulkanSampler> sampler;
	std::unique_ptr<VulkanDescriptorSetLayout> setLayout;
	std::unique_ptr<VulkanDescriptorPool> descriptorPool;
	std::unique_ptr<VulkanDescriptorSet> descriptorSet;
	std::unique_ptr<VulkanRenderPass> renderPass;
	std::vector<std::unique_ptr<VulkanFramebuffer>> framebuffers;
	std::unique_ptr<VulkanShader> vertexShader, fragmentShader;
	std::unique_ptr<VulkanPipelineLayout> pipelineLayout;
	std::unique_ptr<VulkanPipeline> pipeline;
	std::unique_ptr<VulkanCommandPool> commandPool;
	std::unique_ptr<VulkanCommandBuffer> commandBuffer;
	std::unique_ptr<VulkanSemaphore> imageAvailable, renderFinished;
	std::unique_ptr<VulkanFence> submitted;
	std::vector<uint8_t> pixels;
	VkViewport viewport = {};
	bool imageInitialised = false;
};

//==========================================================================
//
// The pad as the launcher's buttons. A direction held repeats, as a held
// key did.
//
//==========================================================================

class PadInput
{
public:
	dosui::Input Read()
	{
		struct pad pad;
		memset(&pad, 0, sizeof(pad));
		pad.held = held;
		pad_poll(&pad);
		held = pad.held;

		// The left stick moves as the D-pad does
		uint32_t directions = pad.held & (PAD_UP | PAD_DOWN | PAD_LEFT | PAD_RIGHT);
		if (pad.left_y < -0.6f) directions |= PAD_UP;
		if (pad.left_y > 0.6f) directions |= PAD_DOWN;
		if (pad.left_x < -0.6f) directions |= PAD_LEFT;
		if (pad.left_x > 0.6f) directions |= PAD_RIGHT;

		const double now = now_seconds();
		uint32_t moved = directions & ~heldDirections;
		if (directions != heldDirections)
		{
			nextRepeat = now + 0.40;
		}
		else if (directions != 0 && now >= nextRepeat)
		{
			moved = directions;
			nextRepeat = now + 0.07;
		}
		heldDirections = directions;

		dosui::Input input;
		input.up = moved & PAD_UP;
		input.down = moved & PAD_DOWN;
		input.left = moved & PAD_LEFT;
		input.right = moved & PAD_RIGHT;
		input.accept = pad.pressed & PAD_CROSS;
		input.back = pad.pressed & PAD_CIRCLE;
		input.square = pad.pressed & PAD_SQUARE;
		input.triangle = pad.pressed & PAD_TRIANGLE;
		input.pageUp = pad.pressed & PAD_L1;
		input.pageDown = pad.pressed & PAD_R1;
		input.start = pad.pressed & PAD_OPTIONS;
		return input;
	}

private:
	uint32_t held = 0;
	uint32_t heldDirections = 0;
	double nextRepeat = 0;
};

// The last run's fatal error, shown once.
std::string TakeLastError()
{
	const std::string path = std::string(PS5_UserRoot()) + "/last-error.txt";
	std::ifstream file(path);
	if (!file)
		return "";
	std::stringstream text;
	text << file.rdbuf();
	file.close();
	remove(path.c_str());
	return text.str();
}

} // namespace

bool PS5_RunLauncher(std::vector<std::string> &arguments)
{
	dosui::Launcher launcher(PS5_UserRoot(), PS5_APP_ROOT, PS5_UserRootShown());
	launcher.Load();
	launcher.Rescan();
	const std::string lastError = TakeLastError();
	if (!lastError.empty())
	{
		launcher.ShowMessage("The last run ended with an error",
			lastError + "\nThe full log is in " + PS5_UserRootShown() + "/uzdoom.log");
	}

	dosui::Launcher::Result result = dosui::Launcher::Result::Running;
	try
	{
		TextPresenter presenter;
		PadInput pad;
		dosui::TextScreen screen;
		while (result == dosui::Launcher::Result::Running)
		{
			result = launcher.Update(pad.Read());
			launcher.Draw(screen);
			presenter.Present(screen);
		}
	}
	catch (const std::exception &error)
	{
		// Without a picture there is no choosing: start what was chosen last.
		say("launcher: no display (%s); starting the last selection", error.what());
		result = dosui::Launcher::Result::Launch;
	}

	if (result == dosui::Launcher::Result::Quit)
		return false;
	const std::vector<std::string> chosen = launcher.BuildArguments();
	arguments.insert(arguments.end(), chosen.begin(), chosen.end());
	return true;
}
