use std::{ffi::c_void, path::Path};

use ash::{util::Align, vk, Device};
use image::{EncodableLayout, ImageReader};
use vk_mem::Alloc;

use crate::renderer::Renderer;

impl Renderer
{
    pub fn create_image_view(device: &Device, image: vk::Image, format: vk::Format, aspect_flags: vk::ImageAspectFlags, mip_levels: u32) -> Result<vk::ImageView, String>
    {
        let view_info = vk::ImageViewCreateInfo::default()
            .image(image)
            .view_type(vk::ImageViewType::TYPE_2D)
            .format(format)
            .subresource_range(
                vk::ImageSubresourceRange::default()
                    .aspect_mask(aspect_flags)
                    .base_mip_level(0)
                    .level_count(mip_levels)
                    .base_array_layer(0)
                    .layer_count(1)
            );

        let view = unsafe { device.create_image_view(&view_info, None) }.map_err(|_| "failed to create image view!")?;
        Ok(view)       
    }

    pub fn create_image_from_png(image_path: &Path, allocator: &vk_mem::Allocator, device: &Device, command_buffer: vk::CommandBuffer) 
        -> Result<(vk::Image, vk_mem::Allocation, vk::Buffer, vk_mem::Allocation, vk::Format, u32), String>
    {
        let texture = ImageReader::open(image_path).map_err(|e| e.to_string())?.decode().map_err(|e| e.to_string())?;

        let width = texture.width(); let height = texture.height();
        let mip_levels = 1; let format = vk::Format::R8G8B8A8_SRGB;

        let binding = texture.into_rgba8();
        let raw_texture = binding.as_bytes();
        let raw_texture_size = raw_texture.len() as u64;


        let (staging_buffer, mut staging_buffer_memory) = Self::create_buffer(
            allocator, raw_texture_size, vk::BufferUsageFlags::TRANSFER_SRC, 
            vk::MemoryPropertyFlags::HOST_COHERENT | vk::MemoryPropertyFlags::HOST_VISIBLE,
            vk_mem::AllocationCreateFlags::HOST_ACCESS_SEQUENTIAL_WRITE
        )?;

        unsafe {
            let data = match allocator.map_memory(&mut staging_buffer_memory)
            {
                Ok(v) => v,
                Err(e) => { allocator.destroy_buffer(staging_buffer, &mut staging_buffer_memory); Err("failed to map buffer memory!")? }
            };
            let mut align = Align::new(data as *mut c_void, align_of::<u8>() as u64, raw_texture_size);
            align.copy_from_slice(&raw_texture);
            allocator.unmap_memory(&mut staging_buffer_memory);
        }

        let texture_extent = vk::Extent3D::default().width(width).height(height).depth(1);
        let texture_image_info = vk::ImageCreateInfo::default()
            .extent(texture_extent)
            .usage(vk::ImageUsageFlags::SAMPLED | vk::ImageUsageFlags::TRANSFER_DST)
            .tiling(vk::ImageTiling::OPTIMAL)
            .format(format)
            .sharing_mode(vk::SharingMode::EXCLUSIVE)
            .samples(vk::SampleCountFlags::TYPE_1)
            .mip_levels(mip_levels)
            .array_layers(1)
            .initial_layout(vk::ImageLayout::UNDEFINED)
            .image_type(vk::ImageType::TYPE_2D);
            
        let texture_create_info = vk_mem::AllocationCreateInfo { usage: vk_mem::MemoryUsage::AutoPreferDevice, ..Default::default()};

        let (texture_image, mut texture_image_alloc) = match unsafe { allocator.create_image(&texture_image_info, &texture_create_info)}
        {
            Ok(v) => v,
            Err(e) => { unsafe { allocator.destroy_buffer(staging_buffer, &mut staging_buffer_memory) };  Err(e.to_string())? }
        };

        match Self::transition_image_layout(device, command_buffer, texture_image, vk::ImageLayout::UNDEFINED, vk::ImageLayout::TRANSFER_DST_OPTIMAL, 1)
        {
            Err(e) => { unsafe { allocator.destroy_buffer(staging_buffer, &mut staging_buffer_memory) }; unsafe { allocator.destroy_image(texture_image, &mut texture_image_alloc) }; Err(e)? },
            _ => ()
        };
        Self::copy_buffer_to_image(device, command_buffer, staging_buffer, texture_image, width, height, mip_levels, &[0]);
        match Self::transition_image_layout(device, command_buffer, texture_image, vk::ImageLayout::TRANSFER_DST_OPTIMAL, vk::ImageLayout::SHADER_READ_ONLY_OPTIMAL, 1)
        {
            Err(e) => { unsafe { allocator.destroy_buffer(staging_buffer, &mut staging_buffer_memory) }; unsafe { allocator.destroy_image(texture_image, &mut texture_image_alloc) }; Err(e)? },
            _ => ()
        };

        Ok((texture_image, texture_image_alloc, staging_buffer, staging_buffer_memory, format, mip_levels))
    }

    fn copy_buffer_to_image(
        device: &Device, command_buffer: vk::CommandBuffer, buffer: vk::Buffer, image: vk::Image,
        initial_width: u32, initial_height: u32, mip_levels: u32, offsets: &[u64]
    )
    {
        let mut regions: Vec<vk::BufferImageCopy> = vec![];
        regions.reserve(mip_levels as usize);

        // Get each mip level as a region of the texture
        for level in 0..mip_levels {
            let offset = offsets[level as usize];

            // Mip levels are always half the size of previous (cascading resolutions)
            // Dividing by 2 is super easy with unsigned integers, a single bit shift towards the endian
            let width = (initial_width >> level).max(1); let height = (initial_height >> level).max(1);

            let region = vk::BufferImageCopy::default()
                .buffer_offset(offset).buffer_row_length(0).buffer_image_height(0)
                .image_subresource(
                vk::ImageSubresourceLayers::default()
                    .aspect_mask(vk::ImageAspectFlags::COLOR)
                    .mip_level(level)
                    .base_array_layer(0).layer_count(1) 
                )
                .image_offset(vk::Offset3D{x:0,y:0,z:0})
                .image_extent(vk::Extent3D{width, height, depth: 1});
            regions.push(region);
        }
        // Copy the collated regions into an image
        unsafe { 
            device.cmd_copy_buffer_to_image(command_buffer, buffer, image, vk::ImageLayout::TRANSFER_DST_OPTIMAL, &regions) 
        };
    }

    fn transition_image_layout(
        device: &Device, command_buffer: vk::CommandBuffer, image: vk::Image, 
        old_layout: vk::ImageLayout, new_layout: vk::ImageLayout, mip_levels: u32
    ) -> Result<(), String>
    {
        // An ImageMemoryBarrier is like a critical section for image memory operations. When we hit the srcStage we check how
        // we were accessing and define the next stage the Image will be used in and how it will be accessed
        let mut barrier = vk::ImageMemoryBarrier::default()
            .old_layout(old_layout).new_layout(new_layout).image(image)
            .subresource_range(vk::ImageSubresourceRange::default()
                .aspect_mask(vk::ImageAspectFlags::COLOR)
                .base_mip_level(0).level_count(mip_levels)
                .base_array_layer(0).layer_count(1)
            );

        if old_layout == vk::ImageLayout::UNDEFINED && 
            new_layout == vk::ImageLayout::TRANSFER_DST_OPTIMAL {
            // How the subresources have/will be accessed at the stages
            barrier.src_access_mask = vk::AccessFlags::default();
            barrier.dst_access_mask = vk::AccessFlags::TRANSFER_WRITE;

            // The stage at which to begin barricading, and when to end. 
            // As in where the last write took place -> where we pick up
            let src_stage = vk::PipelineStageFlags::TOP_OF_PIPE;
            let dst_stage = vk::PipelineStageFlags::TRANSFER;

            // Attach the barrier to the command buffer
            unsafe { 
                device.cmd_pipeline_barrier(
                    command_buffer, src_stage, dst_stage, 
                    vk::DependencyFlags::default(), &[], 
                    &[], &[barrier]) 
            };
        }
        else if old_layout == vk::ImageLayout::TRANSFER_DST_OPTIMAL && 
            new_layout == vk::ImageLayout::SHADER_READ_ONLY_OPTIMAL {
            // Once transitioned, we will only be using this for sampling in the Fragment stage
            barrier.src_access_mask = vk::AccessFlags::TRANSFER_WRITE;
            barrier.dst_access_mask = vk::AccessFlags::SHADER_READ;

            // The stage at which to begin barricading, and when to end. 
            // As in where the last write took place -> where we pick up
            let src_stage = vk::PipelineStageFlags::TRANSFER;
            let dst_stage = vk::PipelineStageFlags::FRAGMENT_SHADER;
            
            // Attach the barrier to the command buffer
            unsafe { 
                device.cmd_pipeline_barrier(
                command_buffer, src_stage, dst_stage, 
                vk::DependencyFlags::default(), &[], 
                &[], &[barrier]) 
            };
        }
        else { Err("unsupported layout transition!")? }
        Ok(())
    }
}