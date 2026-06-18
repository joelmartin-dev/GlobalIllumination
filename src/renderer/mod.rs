use std::{ffi::c_void, fmt::Debug, fs, path::Path};

use vk_mem::Alloc;
use winit::window::Window;
use ash::{Device, Instance, util::Align, vk};

use crate::{camera::{Camera, WORLD_FORWARD, WORLD_RIGHT, WORLD_UP}, renderer::{buffer_structs::MVP, context::VulkanContext, slang::SlangCompiler, vertex::{TRIANGLE_INDICES, TRIANGLE_VERTICES, Vertex}}};
use nalgebra_glm as glm;

mod context;
mod slang;
mod gui;
mod image;
mod vertex;
mod buffer_structs;
mod gltf_parser;

pub const FRAMES_IN_FLIGHT: usize = 2;

pub struct Renderer
{
  context:                  VulkanContext,
  slang_compiler:           SlangCompiler,
  timeline_semaphore:       vk::Semaphore,
  in_flight_fences:         [vk::Fence; FRAMES_IN_FLIGHT],
  swapchain_images:         Vec<vk::Image>,
  swapchain_image_views:    Vec<vk::ImageView>,
  depth_image:              vk::Image,
  depth_image_memory:       vk_mem::Allocation,
  depth_image_view:         vk::ImageView,
  fallback_image:           vk::Image,
  fallback_image_memory:    vk_mem::Allocation,
  fallback_image_view:      vk::ImageView,
  fallback_sampler:         vk::Sampler,
  camera:                   Camera,
  camera_buffers:           Vec<vk::Buffer>,
  camera_buffers_memory:    Vec<vk_mem::Allocation>,
  descriptor_set_layout:    vk::DescriptorSetLayout,
  graphics_pipeline_layout: vk::PipelineLayout,
  graphics_pipeline:        vk::Pipeline,
  descriptor_pool:          vk::DescriptorPool,
  descriptor_sets:          Vec<vk::DescriptorSet>,
  vertices:                 Vec<Vertex>,
  vertex_buffer:            vk::Buffer,
  vertex_buffer_memory:     vk_mem::Allocation,
  indices:                  Vec<u32>,
  index_buffer:             vk::Buffer,
  index_buffer_memory:      vk_mem::Allocation,
  pub camera_velocity:      glm::Vec3,
  pub camera_look:          glm::Vec3,
  pub delta_fov:            f32,
  pub shift_mod:            bool,
  pub frame_delta:          f32,
  current_frame:            u32,
  timeline_value:           u64,
}

impl Drop for Renderer
{
    fn drop(&mut self) {
        unsafe { self.context.device.device_wait_idle() };
        unsafe { self.context.device.destroy_image_view(self.depth_image_view, None);}
        unsafe { self.context.allocator.destroy_image(self.depth_image, &mut self.depth_image_memory) };
        unsafe { self.context.device.destroy_image_view(self.fallback_image_view, None);}
        unsafe { self.context.allocator.destroy_image(self.fallback_image, &mut self.fallback_image_memory) };

        unsafe { self.context.allocator.destroy_buffer(self.vertex_buffer, &mut self.vertex_buffer_memory);}
        unsafe { self.context.allocator.destroy_buffer(self.index_buffer, &mut self.index_buffer_memory);}

        for i in 0..FRAMES_IN_FLIGHT
        {
            unsafe { self.context.allocator.destroy_buffer(self.camera_buffers[i], &mut self.camera_buffers_memory[i]);}
        }
    }
}

impl Renderer
{
    pub fn new(window: &Window) -> Result<Self, String>
    {
        let context = VulkanContext::new(window);

        let slang_compiler = SlangCompiler::new();

        slang_compiler.compile_shader(Path::new("assets/shaders/raster.slang"), Path::new("assets/shaders/shader.spv"));

        let (timeline_semaphore, in_flight_fences) = Self::create_sync_objects(&context);

        let swapchain_images = unsafe { context.swapchain.get_swapchain_images(context.swapchain_khr) }.map_err(|_| "failed to get swapchain images!")?;
        let swapchain_format = VulkanContext::get_surface_format(&context.surface, context.surface_khr, context.physical_device).format;
        let swapchain_image_views: Vec<vk::ImageView> = swapchain_images.iter().map(|&image| Self::create_image_view(&context.device, image, swapchain_format, vk::ImageAspectFlags::COLOR, 1).expect("failed to create swapchain image view!")).collect();

        let mut temp_buffers: Vec<(vk::Buffer, vk_mem::Allocation)> = Vec::new();

        let single_time_command_buffer = VulkanContext::begin_single_time_commands(&context.device, context.graphics_queue.1)?;
        let (fallback_image, fallback_image_memory, fallback_image_view) = {
            let (image, image_memory, staging_buffer, staging_buffer_memory, format, mip_levels) = Self::create_image_from_png(&Path::new("assets/fallback.png"), &context.allocator, &context.device, single_time_command_buffer)?;
            let view = Self::create_image_view(&context.device, image, format, vk::ImageAspectFlags::COLOR, mip_levels)?;
            temp_buffers.push((staging_buffer, staging_buffer_memory));
            (image, image_memory, view)
        };
        let (vertices, vertex_buffer, vertex_buffer_memory, indices, index_buffer, index_buffer_memory) = {
            let (
                (vertex_buffer, vertex_buffer_memory, vertex_staging_buffer, vertex_staging_buffer_memory),
                vertices,
                (index_buffer, index_buffer_memory, index_staging_buffer, index_staging_buffer_memory),
                indices
            ) = Self::load_gltf(
                &context.device, single_time_command_buffer, context.graphics_queue.0, &context.allocator, 
                &Path::new(
                    "INSERT PATH HERE")
            )?;
            temp_buffers.push((vertex_staging_buffer, vertex_staging_buffer_memory));
            temp_buffers.push((index_staging_buffer, index_staging_buffer_memory));
            (vertices, vertex_buffer, vertex_buffer_memory, indices, index_buffer, index_buffer_memory)
        };
        VulkanContext::end_single_time_commands(&context.device, context.graphics_queue.0, single_time_command_buffer)?;

        temp_buffers.iter().for_each(|&(buffer, mut memory)| unsafe { context.allocator.destroy_buffer(buffer, &mut memory) });
        
        let sampler_create_info = vk::SamplerCreateInfo::default()
            .address_mode_u(vk::SamplerAddressMode::REPEAT).address_mode_v(vk::SamplerAddressMode::REPEAT).address_mode_w(vk::SamplerAddressMode::REPEAT)
            .anisotropy_enable(false).compare_enable(false).min_filter(vk::Filter::LINEAR).mag_filter(vk::Filter::LINEAR);
        let fallback_sampler = unsafe { context.device.create_sampler(&sampler_create_info, None) }.map_err(|_| "failed to create texture sampler")?;

        let depth_format = VulkanContext::get_depth_format(&context.instance, context.physical_device);
        let (depth_image, depth_image_memory, depth_image_view) = Self::create_depth_resources(
            &context.instance, &context.allocator, &context.device, context.physical_device, context.swapchain_extent)?;

        let descriptor_set_layout = Self::create_descriptor_set_layouts(&context.device)?;
        let (graphics_pipeline_layout, graphics_pipeline) = Self::create_pipeline(&context.instance, &context.device, context.physical_device, &Path::new("assets/shaders/shader.spv"), swapchain_format, depth_format, descriptor_set_layout)?;
        
        let descriptor_pool = Self::create_descriptor_pools(&context.device)?;

        let camera = Camera::new(context.swapchain_extent.width, context.swapchain_extent.height, glm::vec3(0.0, 0.0, 10.0), glm::zero());
        let (camera_buffers, camera_buffers_memory) = Self::create_camera_buffers(&context.allocator)?;

        let descriptor_sets = Self::create_descriptor_sets(&context.device, descriptor_pool, descriptor_set_layout, camera_buffers.as_slice(), fallback_image_view, fallback_sampler)?;

        Ok(Self {
            context,
            slang_compiler,
            timeline_semaphore,
            in_flight_fences,
            swapchain_images,
            swapchain_image_views,
            depth_image,
            depth_image_memory,
            depth_image_view,
            fallback_image,
            fallback_image_view,
            fallback_image_memory,
            fallback_sampler,
            camera,
            camera_buffers,
            camera_buffers_memory,
            descriptor_set_layout,
            graphics_pipeline_layout,
            graphics_pipeline,
            descriptor_pool,
            descriptor_sets,
            vertices,
            vertex_buffer,
            vertex_buffer_memory,
            indices,
            index_buffer,
            index_buffer_memory,
            camera_velocity: glm::zero(),
            camera_look: glm::zero(),
            delta_fov: 0.0,
            shift_mod: false,
            frame_delta: 0.0,
            current_frame: 0,
            timeline_value: 0,
        })
    }




    fn create_buffer_from_slice<T>(device: &Device, command_buffer: vk::CommandBuffer, allocator: &vk_mem::Allocator, data: &[T], dst_usage_flags: vk::BufferUsageFlags) 
        -> Result<(vk::Buffer, vk_mem::Allocation, vk::Buffer, vk_mem::Allocation), String> where T: Copy + Debug
    {
        let buffer_size = (size_of::<T>() * data.len()) as vk::DeviceSize;
        let (staging_buffer, mut staging_buffer_memory) = Self::create_buffer(
            allocator, buffer_size, vk::BufferUsageFlags::TRANSFER_SRC, 
            vk::MemoryPropertyFlags::HOST_COHERENT | vk::MemoryPropertyFlags::HOST_VISIBLE,
            vk_mem::AllocationCreateFlags::HOST_ACCESS_SEQUENTIAL_WRITE
        )?;

        unsafe {
            let raw_data = match allocator.map_memory(&mut staging_buffer_memory)
            {
                Ok(v) => v,
                Err(e) => { allocator.destroy_buffer(staging_buffer, &mut staging_buffer_memory); Err("failed to map buffer memory!")? }
            };
            let mut align = Align::new(raw_data as *mut c_void, size_of::<T>() as u64, buffer_size);
            align.copy_from_slice(&data);
            allocator.unmap_memory(&mut staging_buffer_memory);
        }

        let buffer_info = vk::BufferCreateInfo::default().usage(dst_usage_flags | vk::BufferUsageFlags::TRANSFER_DST).size(buffer_size).sharing_mode(vk::SharingMode::EXCLUSIVE);

        let alloc_info = vk_mem::AllocationCreateInfo {
            usage: vk_mem::MemoryUsage::AutoPreferDevice, ..Default::default()
        };

        let (buffer, buffer_memory) = match unsafe { allocator.create_buffer(&buffer_info, &alloc_info)}
        {
            Ok(v) => v,
            Err(e) => { unsafe { allocator.destroy_buffer(staging_buffer, &mut staging_buffer_memory) };  Err(e.to_string())? }
        };

        unsafe { device.cmd_copy_buffer(command_buffer, staging_buffer, buffer, &[vk::BufferCopy::default().size(buffer_size)])};

        Ok((buffer, buffer_memory, staging_buffer, staging_buffer_memory))
    }

    fn create_camera_buffers(
        allocator: &vk_mem::Allocator
    ) -> Result<(Vec<vk::Buffer>, Vec<vk_mem::Allocation>), String>
    {
        let mut buffers: Vec<(vk::Buffer, vk_mem::Allocation)> = Vec::with_capacity(FRAMES_IN_FLIGHT);
        for i in 0..FRAMES_IN_FLIGHT
        {
            buffers.push(Self::create_buffer(allocator, size_of::<MVP>() as u64, vk::BufferUsageFlags::UNIFORM_BUFFER, 
            vk::MemoryPropertyFlags::HOST_VISIBLE | vk::MemoryPropertyFlags::HOST_COHERENT, 
            vk_mem::AllocationCreateFlags::HOST_ACCESS_SEQUENTIAL_WRITE)?);
        }
        let camera_buffers: (Vec<vk::Buffer>, Vec<vk_mem::Allocation>) = buffers.into_iter().unzip();

        Ok(camera_buffers)
    }

    fn create_descriptor_sets(
        device: &Device, descriptor_pool: vk::DescriptorPool, 
        descriptor_set_layout: vk::DescriptorSetLayout, camera_buffers: &[vk::Buffer], 
        fallback_image_view: vk::ImageView, fallback_sampler: vk::Sampler
    ) -> Result<Vec<vk::DescriptorSet>, String>
    {
        let layouts = [descriptor_set_layout; FRAMES_IN_FLIGHT];

        let layouts_alloc_info = vk::DescriptorSetAllocateInfo::default().descriptor_pool(descriptor_pool).set_layouts(&layouts);

        let sets = unsafe { device.allocate_descriptor_sets(&layouts_alloc_info) }.map_err(|_| "failed to allocate descriptor sets")?;

        for i in 0..FRAMES_IN_FLIGHT
        {
            let camera_info = [vk::DescriptorBufferInfo::default().buffer(camera_buffers[i]).offset(0).range(size_of::<MVP>() as u64)];
            let texture_info = [vk::DescriptorImageInfo::default().image_layout(vk::ImageLayout::SHADER_READ_ONLY_OPTIMAL).image_view(fallback_image_view).sampler(fallback_sampler)];
            let writes = [
                vk::WriteDescriptorSet::default().dst_set(sets[i]).dst_binding(0).dst_array_element(0).descriptor_count(1).descriptor_type(vk::DescriptorType::UNIFORM_BUFFER).buffer_info(&camera_info), 
                vk::WriteDescriptorSet::default().dst_set(sets[i]).dst_binding(1).dst_array_element(0).descriptor_count(1).descriptor_type(vk::DescriptorType::COMBINED_IMAGE_SAMPLER).image_info(&texture_info)
            ];

            unsafe { device.update_descriptor_sets(&writes, &[]) };
        }

        Ok(sets)
    }

    fn create_descriptor_pools(device: &Device) -> Result<vk::DescriptorPool, String>
    {
        let descriptor_count = FRAMES_IN_FLIGHT as u32;
        let pool_sizes = [
            vk::DescriptorPoolSize::default().ty(vk::DescriptorType::UNIFORM_BUFFER).descriptor_count(descriptor_count),
            vk::DescriptorPoolSize::default().ty(vk::DescriptorType::COMBINED_IMAGE_SAMPLER).descriptor_count(descriptor_count)
        ];

        let pool_info = vk::DescriptorPoolCreateInfo::default().flags(vk::DescriptorPoolCreateFlags::FREE_DESCRIPTOR_SET).max_sets(2).pool_sizes(&pool_sizes);

        Ok(unsafe { device.create_descriptor_pool(&pool_info, None) }.map_err(|_| "failed to create descriptor pool!")?)
    }

    fn create_descriptor_set_layouts(device: &Device) -> Result<vk::DescriptorSetLayout, String>
    {
        let bindings = [
            vk::DescriptorSetLayoutBinding::default().binding(0)
                .descriptor_type(vk::DescriptorType::UNIFORM_BUFFER).descriptor_count(1)
                .stage_flags(vk::ShaderStageFlags::VERTEX),
            vk::DescriptorSetLayoutBinding::default().binding(1)
                .descriptor_type(vk::DescriptorType::COMBINED_IMAGE_SAMPLER).descriptor_count(1)
                .stage_flags(vk::ShaderStageFlags::FRAGMENT)
        ];

        let layout_info = vk::DescriptorSetLayoutCreateInfo::default()
            .bindings(&bindings);

        Ok(unsafe { device.create_descriptor_set_layout(&layout_info, None) }.map_err(|_| "failed to create descriptor set layout!")?)
    }
    
    fn create_depth_resources(
        instance: &Instance, allocator: &vk_mem::Allocator, device: &Device, 
        physical_device: vk::PhysicalDevice, swapchain_extent: vk::Extent2D
    ) -> Result<(vk::Image, vk_mem::Allocation, vk::ImageView), String>
    {
        let format = VulkanContext::get_depth_format(instance, physical_device);
        let depth_image_create_info = vk::ImageCreateInfo::default()
            .extent(vk::Extent3D::default().width(swapchain_extent.width).height(swapchain_extent.height).depth(1))
            .usage(vk::ImageUsageFlags::DEPTH_STENCIL_ATTACHMENT)
            .tiling(vk::ImageTiling::OPTIMAL)
            .format(format)
            .sharing_mode(vk::SharingMode::EXCLUSIVE)
            .samples(vk::SampleCountFlags::TYPE_1)
            .mip_levels(1)
            .array_layers(1)
            .initial_layout(vk::ImageLayout::UNDEFINED)
            .image_type(vk::ImageType::TYPE_2D);

        let depth_image_alloc_info = vk_mem::AllocationCreateInfo {
            usage: vk_mem::MemoryUsage::AutoPreferDevice, preferred_flags: vk::MemoryPropertyFlags::DEVICE_LOCAL, ..Default::default()
        };

        let (image, image_memory) = unsafe { allocator.create_image(&depth_image_create_info, &depth_image_alloc_info) }.map_err(|_| "failed to create depth image!")?;
        let view = Self::create_image_view(device, image, format, vk::ImageAspectFlags::DEPTH, 1)?;

        Ok((image, image_memory, view))
    }

    fn create_pipeline(
        instance: &Instance, device: &Device, physical_device: vk::PhysicalDevice, shader: &Path,
        color_attachment_format: vk::Format, depth_attachment_format: vk::Format, descriptor_set_layout: vk::DescriptorSetLayout
    ) -> Result<(vk::PipelineLayout, vk::Pipeline), String>
    {
        let shader_code: Vec<u32> = fs::read(shader).map_err(|_| "failed to read from shader file")?.chunks_exact(size_of::<u32>()).map(|chunk| u32::from_le_bytes(chunk.try_into().unwrap())).collect();
        let module = Self::create_shader_module(device, shader_code.as_slice())?;
        let vert_name = c"vertMain";
        let frag_name = c"fragMain";

        let vert_shader_create_info = vk::PipelineShaderStageCreateInfo::default()
            .stage(vk::ShaderStageFlags::VERTEX)
            .module(module).name(&vert_name);
        
        let frag_shader_create_info = vk::PipelineShaderStageCreateInfo::default()
            .stage(vk::ShaderStageFlags::FRAGMENT)
            .module(module).name(&frag_name);

        let shader_stages = [vert_shader_create_info, frag_shader_create_info];

        let binding_description = [Vertex::get_binding_description()];
        let attribute_description = Vertex::get_attribute_descriptions();
        let vert_input_info = vk::PipelineVertexInputStateCreateInfo::default()
            .vertex_binding_descriptions(&binding_description)
            .vertex_attribute_descriptions(&attribute_description);

        let input_assembly_info = vk::PipelineInputAssemblyStateCreateInfo::default()
            .topology(vk::PrimitiveTopology::TRIANGLE_LIST);

        let depth_stencil = vk::PipelineDepthStencilStateCreateInfo::default()
            .depth_test_enable(true).depth_write_enable(true).depth_bounds_test_enable(false)
            .depth_compare_op(vk::CompareOp::LESS).stencil_test_enable(false);

        let dynamic_states = [vk::DynamicState::VIEWPORT, vk::DynamicState::SCISSOR];
        let dynamic_info = vk::PipelineDynamicStateCreateInfo::default().dynamic_states(&dynamic_states);

        let viewport_info = vk::PipelineViewportStateCreateInfo::default().viewport_count(1).scissor_count(1);

        let rasterizer_info = vk::PipelineRasterizationStateCreateInfo::default()
            .depth_clamp_enable(false).rasterizer_discard_enable(false)
            .polygon_mode(vk::PolygonMode::FILL).cull_mode(vk::CullModeFlags::BACK)
            .front_face(vk::FrontFace::COUNTER_CLOCKWISE)
            .depth_bias_enable(false).depth_bias_constant_factor(0.0)
            .depth_bias_clamp(0.0).depth_bias_slope_factor(1.0)
            .line_width(1.0);

        let multisampling_info = vk::PipelineMultisampleStateCreateInfo::default()
            .rasterization_samples(vk::SampleCountFlags::TYPE_1).sample_shading_enable(false);

        let color_blend_attachment = [vk::PipelineColorBlendAttachmentState::default()
            .blend_enable(true)
            .src_color_blend_factor(vk::BlendFactor::SRC_ALPHA).src_alpha_blend_factor(vk::BlendFactor::ONE_MINUS_SRC_ALPHA)
            .dst_alpha_blend_factor(vk::BlendFactor::ONE_MINUS_SRC_ALPHA).dst_alpha_blend_factor(vk::BlendFactor::ZERO)
            .color_blend_op(vk::BlendOp::ADD).color_write_mask(vk::ColorComponentFlags::RGBA)];

        let color_blend_info = vk::PipelineColorBlendStateCreateInfo::default()
            .logic_op_enable(false).logic_op(vk::LogicOp::COPY)
            .attachments(&color_blend_attachment);

        let colour_attachment_formats = [color_attachment_format];
        // Which attachments are involved
        let mut rendering_info = vk::PipelineRenderingCreateInfo::default()
            .color_attachment_formats(&colour_attachment_formats)
            .depth_attachment_format(depth_attachment_format);

        let set_layouts = [descriptor_set_layout];
        let pipeline_layout_info = vk::PipelineLayoutCreateInfo::default().set_layouts(&set_layouts);

        let layout = unsafe { device.create_pipeline_layout(&pipeline_layout_info, None) }
            .map_err(|_| "failed to create pipeline layout!")?;

        // Collate all the info
        let pipeline_info = vk::GraphicsPipelineCreateInfo::default()
            .push_next(&mut rendering_info)
            .stages(&shader_stages)
            .vertex_input_state(&vert_input_info)
            .input_assembly_state(&input_assembly_info)
            .viewport_state(&viewport_info)
            .rasterization_state(&rasterizer_info)
            .depth_stencil_state(&depth_stencil)
            .multisample_state(&multisampling_info)
            .color_blend_state(&color_blend_info)
            .dynamic_state(&dynamic_info)
            .layout(layout);

        let pipeline = unsafe {
            device.create_graphics_pipelines(vk::PipelineCache::null(), &[pipeline_info], None)
        }.map_err(|_| "failed to create graphics pipelines!")?[0];

        Ok((layout, pipeline))
    }

    fn create_shader_module(device: &Device, shader_bytes: &[u32]) -> Result<vk::ShaderModule, String>
    {
        let create_info = vk::ShaderModuleCreateInfo::default().code(shader_bytes);
        Ok(unsafe { device.create_shader_module(&create_info, None)}.map_err(|_| "failed to create shader module!")?)
    }

    fn update_camera_buffer(device: &Device, allocator: &vk_mem::Allocator, camera: &Camera, mut camera_buffer_memory: vk_mem::Allocation) -> Result<(), String>
    {
        let model = glm::identity(); let view = camera.get_view_matrix(); let proj = camera.get_proj_matrix();
        let mut mvp = MVP {
            model: model,
            view: view,
            proj: proj,
            inv_view: glm::inverse(&view.clone()),
            inv_proj: glm::inverse(&proj.clone())
        };

        let mvps = [mvp];

        let buffer_size = size_of::<MVP>() as vk::DeviceSize;
        unsafe {
            let data = allocator.map_memory(&mut camera_buffer_memory).map_err(|e| e.to_string())?;
            let mut align = Align::new(data as *mut c_void, align_of::<f32>() as u64, buffer_size);
            align.copy_from_slice(&mvps);
            allocator.unmap_memory(&mut camera_buffer_memory);
        }

        Ok(())
    }

    pub fn present_frame(&mut self) -> Result<(), String>
    {
        let swapchain = &self.context.swapchain;
        let swapchain_khr = self.context.swapchain_khr;
        let swapchain_extent = self.context.swapchain_extent;
        let device = &self.context.device;
        let allocator = &self.context.allocator;
        let graphics_queue = self.context.graphics_queue.0;
        let graphics_pool = self.context.graphics_queue.1;
        let presentation_queue = self.context.presentation_queue.0;
        let presentation_pool = self.context.presentation_queue.1;
        let fence = self.in_flight_fences[self.current_frame as usize];
        let graphics_pipeline = self.graphics_pipeline;
        let graphics_pipeline_layout = self.graphics_pipeline_layout;
        let descriptor_set = self.descriptor_sets[self.current_frame as usize];
        
        let semaphore = [self.timeline_semaphore];
        let graphics_wait_value = [self.timeline_value]; self.timeline_value += 1; let graphics_signal_value = [self.timeline_value];

        let (image_index, _) = unsafe { swapchain.acquire_next_image(swapchain_khr, u64::MAX, vk::Semaphore::null(), fence)}.map_err(|e| e.to_string())?;

        let image = self.swapchain_images[image_index as usize];
        let view = self.swapchain_image_views[image_index as usize];

        let depth_image = self.depth_image;
        let depth_image_view = self.depth_image_view;

        let camera_look_quat = 
            glm::quat_angle_axis(self.camera_look.y * self.frame_delta, &WORLD_UP) * 
            glm::quat_angle_axis(self.camera_look.x * self.frame_delta, &WORLD_RIGHT) * 
            glm::quat_angle_axis(self.camera_look.z * self.frame_delta, &WORLD_FORWARD).normalize();
        self.camera.update(
            self.camera_velocity * self.frame_delta, camera_look_quat,
            self.delta_fov * self.frame_delta, self.shift_mod
        );
        Self::update_camera_buffer(device, allocator, &self.camera, self.camera_buffers_memory[self.current_frame as usize])?;

        unsafe { device.wait_for_fences(&[fence], true, u64::MAX) }.map_err(|e| e.to_string())?;

        let graphics_command_buffer = VulkanContext::begin_single_time_commands(device, graphics_pool)?;
        Self::begin_render(device, graphics_command_buffer, image, view, swapchain_extent, depth_image, depth_image_view);

        unsafe {
            device.cmd_bind_pipeline(graphics_command_buffer, vk::PipelineBindPoint::GRAPHICS, graphics_pipeline);
            device.cmd_bind_vertex_buffers(graphics_command_buffer, 0, &[self.vertex_buffer], &[0]);
            device.cmd_bind_index_buffer(graphics_command_buffer, self.index_buffer, 0, vk::IndexType::UINT32);
            device.cmd_bind_descriptor_sets(graphics_command_buffer, vk::PipelineBindPoint::GRAPHICS, graphics_pipeline_layout, 0, &[descriptor_set], &[]);
            device.cmd_draw_indexed(graphics_command_buffer, self.indices.len() as u32, 1, self.indices[0], 0, 0);
        };

        Self::end_render(device, graphics_command_buffer, image);
        unsafe { device.end_command_buffer(graphics_command_buffer) }.map_err(|e| e.to_string())?;

        let mut graphics_timeline_info = vk::TimelineSemaphoreSubmitInfo::default()
            .wait_semaphore_values(&graphics_wait_value).signal_semaphore_values(&graphics_signal_value);

        let wait_stage = [vk::PipelineStageFlags::COLOR_ATTACHMENT_OUTPUT];
        
        let command_buffers = [graphics_command_buffer];

        let graphics_submit_info = vk::SubmitInfo::default()
            .push_next(&mut graphics_timeline_info)
            .wait_semaphores(&semaphore).wait_dst_stage_mask(&wait_stage)
            .signal_semaphores(&semaphore)
            .command_buffers(&command_buffers);

        unsafe { device.queue_submit(graphics_queue, &[graphics_submit_info], vk::Fence::null()) }
        .map_err(|_| "failed to submit single time commands to queue!")?;

        let swapchains = &[swapchain_khr]; let image_indices = &[image_index];
        
        let present_info = vk::PresentInfoKHR::default().swapchains(swapchains).image_indices(image_indices);
        
        let wait_info = vk::SemaphoreWaitInfo::default()
            .semaphores(&semaphore).values(&graphics_signal_value);

        unsafe { device.wait_semaphores(&wait_info, u64::MAX) }.map_err(|_| "failed to wait for semaphores!")?;

        let present_result = unsafe { swapchain.queue_present(presentation_queue, &present_info)}.map_err(|e| e.to_string());

        unsafe { device.reset_fences(&[fence]) }.map_err(|e| e.to_string())?;

        self.current_frame = (self.current_frame + 1) % FRAMES_IN_FLIGHT as u32;

        Ok(())
    }

    fn begin_render(
        device: &Device, command_buffer: vk::CommandBuffer, 
        swapchain_image: vk::Image, swapchain_image_view: vk::ImageView, swapchain_extent: vk::Extent2D,
        depth_image: vk::Image, depth_image_view: vk::ImageView
    )
    {
        Self::transition_swapchain_layout(
            device, command_buffer, swapchain_image, 
            vk::PipelineStageFlags2::TOP_OF_PIPE, vk::AccessFlags2::default(), 
            vk::PipelineStageFlags2::COLOR_ATTACHMENT_OUTPUT, vk::AccessFlags2::COLOR_ATTACHMENT_WRITE, 
            vk::ImageLayout::UNDEFINED, vk::ImageLayout::COLOR_ATTACHMENT_OPTIMAL
        );

        let depth_barrier = [vk::ImageMemoryBarrier2::default()
            .src_stage_mask(vk::PipelineStageFlags2::TOP_OF_PIPE)
            .src_access_mask(vk::AccessFlags2::default())
            .dst_stage_mask(vk::PipelineStageFlags2::EARLY_FRAGMENT_TESTS | vk::PipelineStageFlags2::LATE_FRAGMENT_TESTS)
            .dst_access_mask(vk::AccessFlags2::DEPTH_STENCIL_ATTACHMENT_READ | vk::AccessFlags2::DEPTH_STENCIL_ATTACHMENT_WRITE)
            .old_layout(vk::ImageLayout::DEPTH_STENCIL_ATTACHMENT_OPTIMAL)
            .new_layout(vk::ImageLayout::DEPTH_STENCIL_ATTACHMENT_OPTIMAL)
            .src_queue_family_index(vk::QUEUE_FAMILY_IGNORED)
            .dst_queue_family_index(vk::QUEUE_FAMILY_IGNORED)
            .image(depth_image)
            .subresource_range(vk::ImageSubresourceRange::default()
                .aspect_mask(vk::ImageAspectFlags::DEPTH)
                .base_mip_level(0).level_count(1)
                .base_array_layer(0).layer_count(1)
            )
        ];

        let depth_dependency_info = vk::DependencyInfo::default().image_memory_barriers(&depth_barrier);
        unsafe { device.cmd_pipeline_barrier2(command_buffer, &depth_dependency_info); }

        let clear_depth = vk::ClearValue {
        depth_stencil: vk::ClearDepthStencilValue{depth: 1.0, stencil: 0}};

        let depth_attachment_info = vk::RenderingAttachmentInfo::default()
            .image_view(depth_image_view)
            .image_layout(vk::ImageLayout::DEPTH_STENCIL_ATTACHMENT_OPTIMAL)
            .load_op(vk::AttachmentLoadOp::CLEAR).store_op(vk::AttachmentStoreOp::DONT_CARE)
            .clear_value(clear_depth);

        let clear_colour = vk::ClearValue {
        color: vk::ClearColorValue{float32: [0.0, 0.0, 0.0, 1.0]}};

        let colour_attachment_info = [vk::RenderingAttachmentInfo::default()
            .image_view(swapchain_image_view)
            .image_layout(vk::ImageLayout::ATTACHMENT_OPTIMAL)
            .load_op(vk::AttachmentLoadOp::CLEAR).store_op(vk::AttachmentStoreOp::STORE)
            .clear_value(clear_colour)];

        let rendering_info = vk::RenderingInfo::default()
        .render_area(vk::Rect2D{offset: vk::Offset2D{x:0,y:0}, extent: swapchain_extent})
        .layer_count(1)
        .color_attachments(&colour_attachment_info)
        .depth_attachment(&depth_attachment_info);

        unsafe { 
        device.cmd_begin_rendering(command_buffer, &rendering_info);

        device.cmd_set_viewport(command_buffer, 0, 
            &[vk::Viewport::default()
            .x(0.0).y(0.0)
            .width(swapchain_extent.width as f32)
            .height(swapchain_extent.height as f32)
            .min_depth(0.0).max_depth(1.0)]
        );
        device.cmd_set_scissor(command_buffer, 0, 
            &[vk::Rect2D{offset: vk::Offset2D{x:0,y:0}, extent: swapchain_extent}]);
        };
    }

    fn end_render(device: &Device, command_buffer: vk::CommandBuffer, swapchain_image: vk::Image)
    {
        unsafe { device.cmd_end_rendering(command_buffer); }

        Self::transition_swapchain_layout(
            device, command_buffer, swapchain_image, 
            vk::PipelineStageFlags2::COLOR_ATTACHMENT_OUTPUT, vk::AccessFlags2::COLOR_ATTACHMENT_WRITE, 
            vk::PipelineStageFlags2::BOTTOM_OF_PIPE, vk::AccessFlags2::default(), 
            vk::ImageLayout::COLOR_ATTACHMENT_OPTIMAL, vk::ImageLayout::PRESENT_SRC_KHR
        );
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

    pub fn create_buffer(
        allocator: &vk_mem::Allocator, buffer_size: vk::DeviceSize, 
        usage_flags: vk::BufferUsageFlags, memory_flags: vk::MemoryPropertyFlags, 
        alloc_flags: vk_mem::AllocationCreateFlags
    ) -> Result<(vk::Buffer, vk_mem::Allocation), String>
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