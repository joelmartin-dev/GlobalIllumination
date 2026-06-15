use imgui_rs_vulkan_renderer::{DynamicRendering, Renderer};
use imgui_winit_support::{HiDpiMode, WinitPlatform};
use winit::window::Window;

use crate::renderer::context::VulkanContext;


struct GuiContext
{
  imgui: imgui::Context,
  platform: WinitPlatform,
  renderer: Renderer
}

impl GuiContext
{
  pub fn new(window: &Window, context: &VulkanContext) -> Self
  {
    let mut imgui = imgui::Context::create();
    let mut platform = WinitPlatform::new(&mut imgui);
    platform.attach_window(imgui.io_mut(), window, HiDpiMode::Default);

    let dynamic_rendering = DynamicRendering {
      color_attachment_format: VulkanContext::get_surface_format(&context.surface, context.surface_khr, context.physical_device).format, 
      depth_attachment_format: Some(VulkanContext::get_depth_format(&context.instance, context.physical_device))
    };
    let renderer = Renderer::with_default_allocator(
      &context.instance, context.physical_device, context.device.clone(), 
      context.graphics_queue.0, context.graphics_queue.1, dynamic_rendering, &mut imgui, None
    ).expect("failed to create ImGui renderer!");
    Self { imgui, platform, renderer }
  }
}