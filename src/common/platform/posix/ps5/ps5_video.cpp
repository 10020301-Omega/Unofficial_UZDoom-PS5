/*
** ps5_video.cpp
**
** The PS5's display for the Vulkan renderer: one display, one plane surface,
** always 3840 x 2160, always "fullscreen". There is no window system and no
** OpenGL on the console, so this is the Vulkan half of sdlglvideo.cpp only.
**
**---------------------------------------------------------------------------
**
** Copyright 2026 PS5-UZDOOM port contributors
** Based on sdlglvideo.cpp:
** Copyright 2005-2016 Marisa Heit
** Copyright 2005-2016 Christoph Oelckers
** Copyright 2017-2025 GZDoom Maintainers and Contributors
** Copyright 2025-2026 UZDoom Maintainers and Contributors
**
** SPDX-License-Identifier: GPL-3.0-or-later
**
**---------------------------------------------------------------------------
**
*/

#include <zvulkan/vulkanbuilders.h>
#include <zvulkan/vulkandevice.h>
#include <zvulkan/vulkaninstance.h>
#include <zvulkan/vulkansurface.h>

#include "basics.h"
#include "c_dispatch.h"
#include "i_video.h"
#include "m_argv.h"
#include "printf.h"
#include "gl_sysfb.h"
#include "ps5_display.h"
#include "v_video.h"
#include "version.h"
#include "vulkan/system/vk_renderdevice.h"

EXTERN_CVAR(Bool, vk_debug)
EXTERN_CVAR(Int, vid_defwidth)
EXTERN_CVAR(Int, vid_defheight)

// The SDL backend owns these two; menus and the config file name them.
CUSTOM_CVAR(Int, vid_adapter, 0, CVAR_ARCHIVE | CVAR_GLOBALCONFIG | CVAR_NOINITCALL)
{
	if (self != 0) self = 0;
}

namespace
{
	// The display is opened once and kept: the launcher draws on it before the
	// engine starts, and the engine takes the same instance and surface over.
	std::shared_ptr<VulkanSurface> sharedSurface;
	int displayWidth = 3840;
	int displayHeight = 2160;
}

//==========================================================================
//
// The one display: an instance with VK_KHR_display and a plane surface on
// the display's fastest mode.
//
//==========================================================================

std::shared_ptr<VulkanSurface> PS5_AcquireSurface()
{
	if (sharedSurface)
		return sharedSurface;

	VulkanInstanceBuilder builder;
	builder.DebugLayer(false);
	builder.RequireExtension(VK_KHR_SURFACE_EXTENSION_NAME);
	builder.RequireExtension(VK_KHR_DISPLAY_EXTENSION_NAME);
	auto instance = builder.Create();

	if (instance->PhysicalDevices.empty())
		VulkanError("No Vulkan device");
	VkPhysicalDevice gpu = instance->PhysicalDevices[0].Device;

	uint32_t count = 1;
	VkDisplayPropertiesKHR display = {};
	VkResult result = vkGetPhysicalDeviceDisplayPropertiesKHR(gpu, &count, &display);
	if ((result != VK_SUCCESS && result != VK_INCOMPLETE) || count == 0)
		VulkanError("No display");

	uint32_t modeCount = 0;
	vkGetDisplayModePropertiesKHR(gpu, display.display, &modeCount, nullptr);
	if (modeCount == 0)
		VulkanError("The display has no modes");
	std::vector<VkDisplayModePropertiesKHR> modes(modeCount);
	vkGetDisplayModePropertiesKHR(gpu, display.display, &modeCount, modes.data());

	// The fastest mode. The driver's presentation falls back to 59.94 Hz by
	// itself when the television stays at 60.
	const VkDisplayModePropertiesKHR *best = &modes[0];
	for (const auto &mode : modes)
	{
		if (mode.parameters.refreshRate > best->parameters.refreshRate)
			best = &mode;
	}

	VkDisplaySurfaceCreateInfoKHR info = { VK_STRUCTURE_TYPE_DISPLAY_SURFACE_CREATE_INFO_KHR };
	info.displayMode = best->displayMode;
	info.planeIndex = 0;
	info.planeStackIndex = 0;
	info.transform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
	info.globalAlpha = 1.0f;
	info.alphaMode = VK_DISPLAY_PLANE_ALPHA_OPAQUE_BIT_KHR;
	info.imageExtent = best->parameters.visibleRegion;

	VkSurfaceKHR handle = VK_NULL_HANDLE;
	result = vkCreateDisplayPlaneSurfaceKHR(instance->Instance, &info, nullptr, &handle);
	if (result != VK_SUCCESS)
		VulkanError("vkCreateDisplayPlaneSurfaceKHR failed");

	displayWidth = (int)info.imageExtent.width;
	displayHeight = (int)info.imageExtent.height;
	Printf("Display: %d x %d at %.2f Hz\n", displayWidth, displayHeight, best->parameters.refreshRate / 1000.0);

	sharedSurface = std::make_shared<VulkanSurface>(instance, handle);
	return sharedSurface;
}

void PS5_ReleaseSurface()
{
	sharedSurface.reset();
}

void PS5_GetDisplaySize(int *width, int *height)
{
	if (width) *width = displayWidth;
	if (height) *height = displayHeight;
}

void I_GetVulkanDrawableSize(int *width, int *height)
{
	PS5_GetDisplaySize(width, height);
}

//==========================================================================
//
// IVideo
//
//==========================================================================

class PS5Video : public IVideo
{
public:
	DFrameBuffer *CreateFrameBuffer() override
	{
		// No fallback: a failure here is fatal and reaches klog through
		// I_FatalError, which is the only place it can be read.
		try
		{
			auto surface = PS5_AcquireSurface();
			return new VulkanRenderDevice(nullptr, true, surface);
		}
		catch (CVulkanError const &error)
		{
			I_FatalError("Initialization of Vulkan failed: %s", error.what());
		}
		return nullptr;
	}

	void DumpAdapters() override
	{
		Printf("1. [%dx%d]\n", displayWidth, displayHeight);
	}
};

IVideo *gl_CreateVideo()
{
	return new PS5Video();
}

//==========================================================================
//
// SystemBaseFrameBuffer
//
//==========================================================================

SystemBaseFrameBuffer::SystemBaseFrameBuffer(void *, bool)
	: DFrameBuffer(vid_defwidth, vid_defheight)
{
}

int SystemBaseFrameBuffer::GetClientWidth()
{
	return displayWidth;
}

int SystemBaseFrameBuffer::GetClientHeight()
{
	return displayHeight;
}

bool SystemBaseFrameBuffer::IsFullscreen()
{
	return true;
}

void SystemBaseFrameBuffer::ToggleFullscreen(bool)
{
}

void SystemBaseFrameBuffer::SetWindowSize(int, int)
{
}

void I_SetWindowTitle(const char *)
{
}
