use std::path::Path;

use vk_mem::Alloc;
use winit::window::Window;
use ash::{vk, Device};

use crate::renderer::context::VulkanContext;

mod context;
mod slang_compiler;
mod gui;
mod image;

pub const FRAMES_IN_FLIGHT: usize = 2;

pub struct Renderer
{
  context: VulkanContext,
  timeline_semaphore: vk::Semaphore,
  in_flight_fences: [vk::Fence; FRAMES_IN_FLIGHT],
  swapchain_images: Vec<vk::Image>,
  swapchain_image_views: Vec<vk::ImageView>,
  fallback_image: vk::Image,
  fallback_image_memory: vk_mem::Allocation,
  fallback_image_view: vk::ImageView,
  current_frame: u32,
}

impl Drop for Renderer
{
  fn drop(&mut self) {
      unsafe { self.context.allocator.destroy_image(self.fallback_image, &mut self.fallback_image_memory) };
  }
}

impl Renderer
{
  pub fn new(window: &Window) -> Result<Self, String>
  {
    let context = VulkanContext::new(window);
    let (timeline_semaphore, in_flight_fences) = Self::create_sync_objects(&context);

    let single_time_command_buffer = VulkanContext::begin_single_time_commands(&context.device, context.graphics_queue.1)?;
    let (fallback_image, fallback_image_memory, staging_buffer, mut staging_buffer_memory, format, mip_levels) = Self::create_image_from_png(&Path::new("assets/fallback.png"), &context.allocator, &context.device, single_time_command_buffer)?;
    VulkanContext::end_single_time_commands(&context.device, context.graphics_queue.0, single_time_command_buffer)?;
    unsafe { context.allocator.destroy_buffer(staging_buffer, &mut staging_buffer_memory) };
    let fallback_image_view = Self::create_image_view(&context.device, fallback_image, format, vk::ImageAspectFlags::COLOR, mip_levels)?;

    let swapchain_images = unsafe { context.swapchain.get_swapchain_images(context.swapchain_khr) }.map_err(|_| "failed to get swapchain images!")?;
    let format = VulkanContext::get_surface_format(&context.surface, context.surface_khr, context.physical_device).format;
    let swapchain_image_views: Vec<vk::ImageView> = swapchain_images.iter().map(|&image| Self::create_image_view(&context.device, image, format, vk::ImageAspectFlags::COLOR, 1).expect("failed to create swapchain image view!")).collect();

    Ok(Self {
      context,
      timeline_semaphore,
      in_flight_fences,
      swapchain_images,
      swapchain_image_views,
      fallback_image,
      fallback_image_view,
      fallback_image_memory,
      current_frame: 0,
    })
  }

  pub fn present_frame(&mut self) -> Result<(), String>
  {
    let swapchain = &self.context.swapchain;
    let swapchain_khr = self.context.swapchain_khr;
    let device = &self.context.device;
    let presentation_queue = self.context.presentation_queue.0;
    let presentation_pool = self.context.presentation_queue.1;
    let fence = self.in_flight_fences[self.current_frame as usize];

    let (image_index, _) = unsafe { swapchain.acquire_next_image(swapchain_khr, u64::MAX, vk::Semaphore::null(), fence)}.map_err(|e| e.to_string())?;

    let image = self.swapchain_images[image_index as usize];
    let view = self.swapchain_image_views[image_index as usize];

    unsafe { device.wait_for_fences(&[fence], true, u64::MAX) }.map_err(|e| e.to_string())?;

    let command_buffer = VulkanContext::begin_single_time_commands(device, presentation_pool)?;
    Self::transition_swapchain_layout(device, command_buffer, image, vk::PipelineStageFlags2::TOP_OF_PIPE, vk::AccessFlags2::default(), vk::PipelineStageFlags2::BOTTOM_OF_PIPE, vk::AccessFlags2::default(), vk::ImageLayout::UNDEFINED, vk::ImageLayout::PRESENT_SRC_KHR);
    VulkanContext::end_single_time_commands(device, presentation_queue, command_buffer)?;

    let swapchains = &[swapchain_khr]; let image_indices = &[image_index];

    let present_info = vk::PresentInfoKHR::default().swapchains(swapchains).image_indices(image_indices);

    let present_result = unsafe { swapchain.queue_present(presentation_queue, &present_info)}.map_err(|e| e.to_string());

    unsafe { device.reset_fences(&[fence]) }.map_err(|e| e.to_string())?;

    self.current_frame = (self.current_frame + 1) % FRAMES_IN_FLIGHT as u32;

    Ok(())
  }

  fn transition_swapchain_layout(
    device: &Device, command_buffer: vk::CommandBuffer, image: vk::Image, 
    src_stage_mask: vk::PipelineStageFlags2, src_access_mask: vk::AccessFlags2, 
    dst_stage_mask: vk::PipelineStageFlags2, dst_access_mask: vk::AccessFlags2, 
    old_layout: vk::ImageLayout, new_layout: vk::ImageLayout
  )
  {
    let barrier = [
      vk::ImageMemoryBarrier2::default()
        .src_stage_mask(src_stage_mask).src_access_mask(src_access_mask)
        .dst_stage_mask(dst_stage_mask).dst_access_mask(dst_access_mask)
        .old_layout(old_layout).new_layout(new_layout)
        .src_queue_family_index(vk::QUEUE_FAMILY_IGNORED)
        .dst_queue_family_index(vk::QUEUE_FAMILY_IGNORED)
        .image(image)
        .subresource_range(
          vk::ImageSubresourceRange::default()
            .aspect_mask(vk::ImageAspectFlags::COLOR)
            .base_mip_level(0).level_count(1)
            .base_array_layer(0).layer_count(1)
        )
    ];

    let dependency_info = vk::DependencyInfo::default().image_memory_barriers(&barrier);

    unsafe { device.cmd_pipeline_barrier2(command_buffer, &dependency_info);}
  }

  fn create_sync_objects(context: &VulkanContext) -> (vk::Semaphore, [vk::Fence; FRAMES_IN_FLIGHT])
  {
    let device = &context.device;

    // timelineSemaphore starting at 0
    let mut semaphore_info = vk::SemaphoreTypeCreateInfo::default()
      .semaphore_type(vk::SemaphoreType::TIMELINE).initial_value(0);
    let timeline_semaphore = unsafe {
      device.create_semaphore(&vk::SemaphoreCreateInfo::default().push_next(&mut semaphore_info), None)
        .expect("failed to create timeline semaphore!")
    };
    
    // Fences for swapping between swap chain images
    let in_flight_fences = [
      unsafe {device.create_fence(&vk::FenceCreateInfo::default(), None)}.expect("failed to create fence!"); FRAMES_IN_FLIGHT
    ];

    (timeline_semaphore, in_flight_fences)
  }

  fn create_buffer(allocator: &vk_mem::Allocator, buffer_size: vk::DeviceSize, usage_flags: vk::BufferUsageFlags, memory_flags: vk::MemoryPropertyFlags, alloc_flags: vk_mem::AllocationCreateFlags) -> Result<(vk::Buffer, vk_mem::Allocation), String>
  {
    let buffer_info = vk::BufferCreateInfo::default()
      .size(buffer_size).usage(usage_flags).sharing_mode(vk::SharingMode::EXCLUSIVE);

    let buffer_alloc_info = vk_mem::AllocationCreateInfo {
      usage: vk_mem::MemoryUsage::Auto, preferred_flags: memory_flags, 
      flags: alloc_flags,
      ..Default::default()
    };

    let (buffer, buffer_alloc) = unsafe { allocator.create_buffer(&buffer_info, &buffer_alloc_info)}.map_err(|_| "failed to allocate buffer!")?;

    Ok((buffer, buffer_alloc))
  }
}