use std::{ffi::c_void, path::Path};

use ash::{util::Align, vk};
use image::{EncodableLayout, ImageReader};
use vk_mem::Alloc;

use crate::renderer::{Renderer, context::VulkanContext};

impl Renderer
{
    fn create_image_from_png(image_path: &Path, context: &VulkanContext) -> Result<(), String>
    {
        let texture = ImageReader::open(image_path).map_err(|e| e.to_string())?.decode().map_err(|e| e.to_string())?;

        let binding = texture.into_rgba8();
        let raw_texture = binding.as_bytes();
        let raw_texture_size = raw_texture.len() as u64;

        let mip_levels = 1; let format = vk::Format::R8G8B8A8_SRGB;

        let allocator = &context.allocator;

        let (staging_buffer, staging_buffer_memory) = Self::create_buffer(
            allocator, raw_texture_size, vk::BufferUsageFlags::TRANSFER_SRC, 
            vk::MemoryPropertyFlags::HOST_COHERENT | vk::MemoryPropertyFlags::HOST_VISIBLE
        )?;

        let device = &context.device;

        unsafe {
            let data = allocator.map_memory(&mut staging_buffer_memory).map_err(|_| "failed to map buffer memory!")?;
            let mut align = Align::new(data as *mut c_void, size_of::<u8>() as u64, raw_texture_size);
            align.copy_from_slice(&raw_texture);
            allocator.unmap_memory(&mut staging_buffer_memory);
        }

        let texture_extent = vk::Extent3D::default().width(texture.width()).height(texture.height()).depth(1);
        let texture_image_info = vk::ImageCreateInfo::default()
            .extent(texture_extent)
            .usage(vk::ImageUsageFlags::SAMPLED | vk::ImageUsageFlags::TRANSFER_DST)
            .tiling(vk::ImageTiling::OPTIMAL)
            .initial_layout(vk::ImageLayout::TRANSFER_DST_OPTIMAL);
            
        let texture_create_info = vk_mem::AllocationCreateInfo::

        let texture_image = unsafe { allocator.create_image(&texture_image_info, create_info)};

        Self::copy_buffer_to_image();
        Self::transition_image_layout();

        Ok(())
    }

    fn create_image(context: &VulkanContext)
    {

    }

    fn copy_buffer_to_image(context: &VulkanContext)
    {

    }

    fn transition_image_layout(context: &VulkanContext)
    {

    }
}