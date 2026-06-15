use vk_mem::Alloc;
use winit::window::Window;
use ash::vk;

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
  fallback_image: vk::Image,
  fallback_image_view: vk::ImageView,
  fallback_image_memory: vk::DeviceMemory
}

impl Renderer
{
  pub fn new(window: &Window) -> Self
  {
    let context = VulkanContext::new(window);
    let (timeline_semaphore, in_flight_fences) = Self::create_sync_objects(&context);

    Self {
      context,
      timeline_semaphore,
      in_flight_fences
    }
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

  fn create_buffer(allocator: &vk_mem::Allocator, buffer_size: vk::DeviceSize, usage_flags: vk::BufferUsageFlags, memory_flags: vk::MemoryPropertyFlags) -> Result<(vk::Buffer, vk_mem::Allocation), String>
  {
    let buffer_info = vk::BufferCreateInfo::default()
      .size(buffer_size).usage(usage_flags).sharing_mode(vk::SharingMode::EXCLUSIVE);

    let buffer_alloc_info = vk_mem::AllocationCreateInfo {
      usage: vk_mem::MemoryUsage::Auto, required_flags: memory_flags,
      ..Default::default()
    };

    let (buffer, buffer_alloc) = unsafe { allocator.create_buffer(&buffer_info, &buffer_alloc_info)}.map_err(|_| "failed to allocate buffer!")?;

    Ok((buffer, buffer_alloc))
  }
}