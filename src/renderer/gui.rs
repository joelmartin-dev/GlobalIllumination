use imgui::Condition;
use imgui_rs_vulkan_renderer::{DynamicRendering, Options, Renderer};
use imgui_winit_support::{HiDpiMode, WinitPlatform};
use winit::window::Window;

use crate::{camera::Camera, renderer::{FRAMES_IN_FLIGHT, context::VulkanContext}};


pub struct GuiContext
{
  pub imgui: imgui::Context,
  pub platform: WinitPlatform,
  pub renderer: Renderer
}

impl GuiContext
{
  pub fn new(window: &Window, context: &VulkanContext) -> Result<Self, String>
  {
    let mut imgui = imgui::Context::create();
    let mut platform = WinitPlatform::new(&mut imgui);
    platform.attach_window(imgui.io_mut(), window, HiDpiMode::Default);

    let dynamic_rendering = DynamicRendering {
      color_attachment_format: VulkanContext::get_surface_format(&context.surface, context.surface_khr, context.physical_device).format, 
      depth_attachment_format: Some(VulkanContext::get_depth_format(&context.instance, context.physical_device))
    };
    let imgui_options = Options { in_flight_frames: FRAMES_IN_FLIGHT, ..Default::default() };
    let renderer = Renderer::with_default_allocator(
      &context.instance, context.physical_device, context.device.clone(), 
      context.graphics_queue.0, context.graphics_queue.1, dynamic_rendering, &mut imgui, Some(imgui_options)
    ).map_err(|_| "failed to create ImGui renderer!")?;
    Ok(Self { imgui, platform, renderer })
  }

  pub fn setup_imgui_frame(
      &mut self, camera: &mut Camera, window: &Window//, gltf_replace_mode: &mut bool
  ) -> Result<(), String>
  {
      let imgui = &mut self.imgui;
      let platform = &mut self.platform;

      let _frame_io = platform.prepare_frame(imgui.io_mut(), window).map_err(|e| e.to_string())?;
      let ui = imgui.new_frame();

      if let Some(_) = ui
          .window("Camera Controls")
          .title_bar(true)
          .resizable(true)
          .always_auto_resize(true)
          .movable(true)
          .collapsible(true)
          .position([20.0, 20.0], Condition::FirstUseEver)
          .begin()
      {
          // ui.text_wrapped(format!("{:.2}ms", (debug_gui_context.delta as f64) / 1000.0));
          ui.slider("Move Speed", 0.01, 10.0, &mut camera.move_speed);
          let upper = 30.0; let lower = -upper;
          ui.slider("X", lower, upper, &mut camera.pos.x);
          ui.slider("Y", lower, upper, &mut camera.pos.y);
          ui.slider("Z", lower, upper, &mut camera.pos.z);

          ui.spacing();

          ui.text("Rotation");
          ui.slider("i", -1.0, 1.0, &mut camera.rot.i);
          ui.slider("j", -1.0, 1.0, &mut camera.rot.j);
          ui.slider("k", -1.0, 1.0, &mut camera.rot.k);
          ui.slider("w", -1.0, 1.0, &mut camera.rot.w);
          
          ui.spacing();

          ui.slider("FOV", 20.0, 170.0, &mut camera.fov);
          ui.slider("FOV Speed", 0.01, 1000.0, &mut camera.fov_speed);

          ui.spacing();

          ui.slider("Near Plane", 0.01, 100.0, &mut camera.near_plane);
          ui.slider("Far Plane", 10.0, 1000.0, &mut camera.far_plane);

          ui.spacing();

          ui.slider("Speed Mod", 0.01, 4.0, &mut camera.shift_speed);
          // ImGui::SliderInt("Delta Mult", &deltaExp, 0, 32);
      };

      // if let Some(_) = ui
      // .window("Shaders")
      // .title_bar(true)
      // .resizable(true)
      // // .always_auto_resize(true)
      // .movable(true)
      // .collapsible(true)
      // .position([1110.0, 20.0], Condition::FirstUseEver)
      // .begin()
      // {
      //     ui.checkbox("Replace Geometry", gltf_replace_mode);
      //     ui.input_text("Slang Path", &mut debug_gui_context.slang_path).build();
      //     ui.input_text("SPIR-V Path", &mut debug_gui_context.spirv_path).build();
      //     ui.input_text_multiline("Slang Content", &mut debug_gui_context.slang_content, ui.content_region_avail()).build();
          
      //     if ui.is_item_deactivated_after_edit() {
      //     match fs::write(&debug_gui_context.slang_path, &debug_gui_context.slang_content) {
      //         Err(e) => println!("{}", e.to_string()),
      //         _ => ()//println!("Wrote shader to file!")
      //     }
      //     }
      // };

      platform.prepare_render(ui, window);
      Ok(())
  }
}