use std::{ffi::{CStr, c_void}};

use ash::{Device, Entry, Instance, ext::debug_utils, khr::{surface, swapchain}, prelude::VkResult, vk};
use winit::{raw_window_handle::{HasDisplayHandle, HasWindowHandle}, window::Window};



pub struct VulkanContext {
  entry:              Entry,
  pub instance:           Instance,
  pub surface:            surface::Instance,
  pub surface_khr:        vk::SurfaceKHR,
  pub physical_device:    vk::PhysicalDevice,
  pub device:             Device,
  pub swapchain:          swapchain::Device,
  pub swapchain_khr:      vk::SwapchainKHR,
  pub presentation_queue: (vk::Queue, vk::CommandPool),
  pub graphics_queue:     (vk::Queue, vk::CommandPool),
  pub compute_queue:      (vk::Queue, vk::CommandPool),
}

unsafe extern "system" fn debug_callback(
  msg_severity: vk::DebugUtilsMessageSeverityFlagsEXT, 
  _msg_type: vk::DebugUtilsMessageTypeFlagsEXT, 
  p_callback_data: *const vk::DebugUtilsMessengerCallbackDataEXT<'_>, 
  _: *mut c_void
) -> vk::Bool32
{
  // If msg_severity is error, print error else print warning
  let severity = 
    if msg_severity & vk::DebugUtilsMessageSeverityFlagsEXT::ERROR == msg_severity { "error" } 
    else if msg_severity & vk::DebugUtilsMessageSeverityFlagsEXT::WARNING == msg_severity { "warning" } 
    else if msg_severity & vk::DebugUtilsMessageSeverityFlagsEXT::INFO == msg_severity { "info" }
    else { "verbose" };

  println!("validation layer: type {} msg: {}", severity,
    // The message is passed as a pointer to a CStr. Reconstruct the message and convert it to a UTF-8 str slice
    unsafe { CStr::from_ptr((*p_callback_data).p_message) }.to_string_lossy()
  );

  // Result is unused
  return vk::FALSE;
}

impl VulkanContext
{
  pub fn new(window: &Window) -> Self 
  {
    let entry = unsafe { Entry::load().expect("failed to load Vulkan!") };
    let instance = Self::create_instance(&entry, window).expect("failed to create Vulkan instance");
    
    #[cfg(debug_assertions)]
    Self::setup_debug_messenger(&entry, &instance);

    let surface = surface::Instance::new(&entry, &instance);
    let surface_khr = unsafe { 
      ash_window::create_surface(
        &entry, &instance, 
        window.display_handle().expect("failed to get window display handle").as_raw(), 
        window.window_handle().expect("failed to get window handle").as_raw(), 
        None
    )}.expect("failed to create surface from winit window!");

    let required_device_extensions = [
      vk::KHR_SWAPCHAIN_NAME, vk::KHR_SPIRV_1_4_NAME, vk::KHR_SYNCHRONIZATION2_NAME, vk::KHR_CREATE_RENDERPASS2_NAME
    ];

    let physical_device = Self::pick_physical_device(&instance, &surface, surface_khr, &required_device_extensions);

    let req_ext_ptr: Vec<*const i8> = required_device_extensions.iter().map(|ext| ext.as_ptr()).collect();

    let (device, qfis) = Self::create_logical_device(&instance, &surface, surface_khr, physical_device, &req_ext_ptr);

    let presentation_queue = unsafe { device.get_device_queue(qfis[0], 0)};
    let graphics_queue = if qfis[1] == qfis[0] { presentation_queue } 
      else { unsafe { device.get_device_queue(qfis[1], 0)}};
    let compute_queue = if qfis[2] == qfis[0] { presentation_queue } 
      else if qfis[2] == qfis[1] { graphics_queue } 
      else { unsafe { device.get_device_queue(qfis[2], 0)}};

    let swapchain = swapchain::Device::new(&instance, &device);

    let swapchain_khr = Self::create_swapchain(window, &surface, surface_khr, physical_device, &swapchain);

    let presentation_command_pool_create_info = vk::CommandPoolCreateInfo::default().queue_family_index(qfis[0]);

    let graphics_command_pool_create_info = vk::CommandPoolCreateInfo::default().queue_family_index(qfis[1]);

    let compute_command_pool_create_info = vk::CommandPoolCreateInfo::default().queue_family_index(qfis[2]);

    let presentation_command_pool = 
      unsafe { device.create_command_pool(&presentation_command_pool_create_info, None) }
        .expect("failed to create command pool!");
    let graphics_command_pool = 
      if qfis[1] == qfis[0] { presentation_command_pool } 
      else { 
        unsafe { device.create_command_pool(&graphics_command_pool_create_info, None) }
        .expect("failed to create command pool!") 
      };

    let compute_command_pool = 
      if qfis[2] == qfis[0] { presentation_command_pool } 
      else if qfis[2] == qfis[1] { graphics_command_pool } 
      else { 
        unsafe { device.create_command_pool(&compute_command_pool_create_info, None) }
        .expect("failed to create command pool!") 
      };

    Self { 
      entry,
      instance, 
      surface, 
      surface_khr, 
      physical_device, 
      device, 
      swapchain, 
      swapchain_khr,
      presentation_queue: (presentation_queue, presentation_command_pool),
      graphics_queue: (graphics_queue, graphics_command_pool),
      compute_queue: (compute_queue, compute_command_pool)
    }
  }

  fn create_instance(entry: &Entry, window: &Window) -> VkResult<Instance>
  {
    let app_info = vk::ApplicationInfo::default()
      .api_version(vk::make_api_version(0, 1, 4, 0))
      .application_name(&c"Beans Editor")
      .engine_name(&c"Beans Engine");

    #[cfg(not(debug_assertions))]
    let required_layers: [&CStr; 0] = [];

    #[cfg(debug_assertions)]
    let required_layers = [c"VK_LAYER_KHRONOS_validation"];

    let layer_properties = 
      unsafe { entry.enumerate_instance_layer_properties().expect("failed to load Vulkan layer properties") };
    
    if !required_layers.iter().all(|&req_layer| { layer_properties.iter().any(|&layer| 
      req_layer == layer.layer_name_as_c_str().expect("failed to get layer property name as cstr!")) 
    }) { Err(vk::Result::ERROR_LAYER_NOT_PRESENT)? };

    let req_layers_ptr: Vec<*const i8> = required_layers.iter().map(|layer| layer.as_ptr()).collect();

    let mut extension_names = vec![vk::KHR_GET_PHYSICAL_DEVICE_PROPERTIES2_NAME.as_ptr()];
    extension_names.extend_from_slice(
      ash_window::enumerate_required_extensions(
        window.display_handle().expect("failed to get window display handle").as_raw())
        .expect("failed to enumerate extensions required by ash-window!")
    );

    #[cfg(debug_assertions)]
    extension_names.push(vk::EXT_DEBUG_UTILS_NAME.as_ptr());

    let create_info = vk::InstanceCreateInfo::default()
      .application_info(&app_info)
      .enabled_extension_names(&extension_names)
      .enabled_layer_names(&req_layers_ptr);

    unsafe { entry.create_instance(&create_info, None) }
  }

  fn setup_debug_messenger(entry: &Entry, instance: &Instance)
  {
    // Determine which message severities to even consider printing
    let severity_flags = vk::DebugUtilsMessageSeverityFlagsEXT::WARNING | vk::DebugUtilsMessageSeverityFlagsEXT::ERROR;
    
    // Determine which message types to even consider printing
    let message_type_flags = 
      vk::DebugUtilsMessageTypeFlagsEXT::GENERAL | vk::DebugUtilsMessageTypeFlagsEXT::PERFORMANCE |
      vk::DebugUtilsMessageTypeFlagsEXT::VALIDATION;

    // The instantiation data for the debugMessenger
    let debug_utils_messenger_create_info = vk::DebugUtilsMessengerCreateInfoEXT::default()
      .message_severity(severity_flags).message_type(message_type_flags).pfn_user_callback(Some(debug_callback)); 
    
    // Associate the debugMessenger with the instance
    let utils_instance = debug_utils::Instance::new(entry, instance);
    unsafe {
      utils_instance.create_debug_utils_messenger(&debug_utils_messenger_create_info, None)
        .expect("failed to create debug messenger!")
    };
  }

  fn pick_physical_device(
    instance: &Instance, surface: &surface::Instance, surface_khr: vk::SurfaceKHR, 
    required_device_extensions: &[&CStr]
  ) -> vk::PhysicalDevice
  {
    let physical_devices = unsafe { 
      instance.enumerate_physical_devices().expect("failed to enumerate physical devices!")
    };
    if physical_devices.is_empty() { panic!("failed to find any physical devices"); }

    let first_suitable_device = physical_devices.iter().find(|&device| {
      let properties = unsafe { instance.get_physical_device_properties(*device) };

      let supports_vulkan_1_4 = properties.api_version >= vk::make_api_version(0, 1, 4, 0);

      let queue_families = unsafe { instance.get_physical_device_queue_family_properties(*device)};

      let supports_presentation = queue_families.iter().enumerate().any(|(idx, &qfp)| unsafe { 
        surface.get_physical_device_surface_support(*device, idx as u32, surface_khr)
        }.expect("failed to query surface support")
      );

      let supports_graphics = queue_families.iter().any(|&qfp| 
        qfp.queue_flags.contains(vk::QueueFlags::GRAPHICS));
      let supports_compute = queue_families.iter().any(|&qfp| 
        qfp.queue_flags.contains(vk::QueueFlags::COMPUTE));

      let available_device_extensions = unsafe { 
        instance.enumerate_device_extension_properties(*device)
          .expect("failed to enumerate physical device extension properties!")
      };

      let supports_all_required_extensions = required_device_extensions.iter().all(|&req_ext| {
        available_device_extensions.iter().any(|&ext| 
          req_ext == ext.extension_name_as_c_str().expect("failed to get extension name as cstr!"))        
      });

      let mut extended_dynamic_state_features = vk::PhysicalDeviceExtendedDynamicStateFeaturesEXT::default();
      let mut vulkan_1_3_features = vk::PhysicalDeviceVulkan13Features::default();
      let mut vulkan_1_2_features = vk::PhysicalDeviceVulkan12Features::default();

      vulkan_1_3_features.p_next = &raw mut extended_dynamic_state_features as *mut c_void;
      vulkan_1_2_features.p_next = &raw mut vulkan_1_3_features as *mut c_void;
      
      let mut features2 = vk::PhysicalDeviceFeatures2::default().push_next(&mut vulkan_1_2_features);
      unsafe { instance.get_physical_device_features2(*device, &mut features2);}

      let supports_required_features = 
        vulkan_1_2_features.timeline_semaphore                  != vk::FALSE &&
        vulkan_1_3_features.synchronization2                    != vk::FALSE &&
        vulkan_1_3_features.dynamic_rendering                   != vk::FALSE &&
        extended_dynamic_state_features.extended_dynamic_state  != vk::FALSE;

      
      return supports_vulkan_1_4 && supports_presentation && supports_graphics && supports_compute && 
        supports_all_required_extensions && supports_required_features;
    }).expect("failed to find a compatible physical device!");

    return *first_suitable_device;
  }

  fn create_logical_device(
    instance: &Instance, surface: &surface::Instance, surface_khr: vk::SurfaceKHR, 
    physical_device: vk::PhysicalDevice, req_ext_ptr: &Vec<*const i8>
  ) -> (Device, [u32; 3])
  {
    let qfp = unsafe { instance.get_physical_device_queue_family_properties(physical_device)};

    let present_qfi = qfp.iter().enumerate().position(|(idx, &qfp)| 
        unsafe { surface.get_physical_device_surface_support(physical_device, idx as u32, surface_khr)}
          .expect("failed to find the present queue family!")
      )
      .expect("failed to find present-capable queue family, despite one existing!");

    let graphics_qfi = qfp.iter().position(|&qfp| qfp.queue_flags.contains(vk::QueueFlags::GRAPHICS))
      .expect("failed to find graphics-capable queue family, despite one existing!");

    // Prefer a separate queue for compute
    let compute_qfi = match qfp.iter().enumerate().position(|(idx, &qfp)| 
      qfp.queue_flags.contains(vk::QueueFlags::COMPUTE) && idx != graphics_qfi)
    {
      Some(qfi) => qfi,
      None => qfp.iter().position(|&qfp| qfp.queue_flags.contains(vk::QueueFlags::COMPUTE))
        .expect("failed to find compute-capable queue family, despite one existing!")
    };

    let qfis = [present_qfi as u32, graphics_qfi as u32, compute_qfi as u32];

    let mut vulkan_1_2_features = vk::PhysicalDeviceVulkan12Features::default()
      .timeline_semaphore(true);
    let mut vulkan_1_3_features = vk::PhysicalDeviceVulkan13Features::default()
      .synchronization2(true).dynamic_rendering(true);
    let mut extended_dynamic_state_features = vk::PhysicalDeviceExtendedDynamicStateFeaturesEXT::default()
      .extended_dynamic_state(true);

    vulkan_1_3_features.p_next = &raw mut extended_dynamic_state_features as *mut c_void;
    vulkan_1_2_features.p_next = &raw mut vulkan_1_3_features as *mut c_void;

    let mut features2 = vk::PhysicalDeviceFeatures2::default().push_next(&mut vulkan_1_2_features);

    let present_graphics_queue_priority = [1.0];
    let compute_queue_priority = [0.0];

    let mut device_queue_create_info = vec![
      vk::DeviceQueueCreateInfo::default()
        .queue_family_index(present_qfi as u32)
        .queue_priorities(&present_graphics_queue_priority)
    ];

    if present_qfi != graphics_qfi { 
      device_queue_create_info.push(
        vk::DeviceQueueCreateInfo::default()
          .queue_family_index(graphics_qfi as u32)
          .queue_priorities(&present_graphics_queue_priority));
    }

    if compute_qfi != graphics_qfi { 
      device_queue_create_info.push(
        vk::DeviceQueueCreateInfo::default()
          .queue_family_index(compute_qfi as u32)
          .queue_priorities(&compute_queue_priority));
    }

    let device_create_info = vk::DeviceCreateInfo::default()
      .push_next(&mut features2)
      .queue_create_infos(&device_queue_create_info)
      .enabled_extension_names(req_ext_ptr);

    let device = unsafe {
      instance.create_device(physical_device, &device_create_info, None).expect("failed to create logical device!")
    };

    return (device, qfis);
  }

  fn get_swapchain_present_mode(surface: &surface::Instance, surface_khr: vk::SurfaceKHR, physical_device: vk::PhysicalDevice) -> vk::PresentModeKHR
  {
    let available_present_modes = unsafe { 
      surface.get_physical_device_surface_present_modes(physical_device, surface_khr)
    }.expect("failed to query surface present modes!");
    
    match available_present_modes.iter().find(|&mode| *mode == vk::PresentModeKHR::MAILBOX) {
      Some(mode) => *mode,
      None => vk::PresentModeKHR::FIFO
    }
  }

  fn get_swapchain_surface_format(surface: &surface::Instance, surface_khr: vk::SurfaceKHR, physical_device: vk::PhysicalDevice) -> vk::SurfaceFormatKHR
  {
    let available_surface_formats = unsafe { 
      surface.get_physical_device_surface_formats(physical_device, surface_khr)
    }.expect("failed to query surface formats!"); 
    
    match available_surface_formats.iter().find(|&fmt| 
      fmt.format == vk::Format::B8G8R8A8_SRGB && fmt.color_space == vk::ColorSpaceKHR::SRGB_NONLINEAR) {
      Some(fmt) => *fmt,
      None => available_surface_formats[0]
    }
  }

  fn create_swapchain(
    window: &Window, surface: &surface::Instance, surface_khr: vk::SurfaceKHR, 
    physical_device: vk::PhysicalDevice, swapchain: &swapchain::Device
  ) -> vk::SwapchainKHR
  {
    let swapchain_present_mode = Self::get_swapchain_present_mode(surface, surface_khr, physical_device);

    let swapchain_surface_format = Self::get_swapchain_surface_format(surface, surface_khr, physical_device);

    let capabilities = unsafe {
      surface.get_physical_device_surface_capabilities(physical_device, surface_khr)
    }.expect("failed to query surface capabilities!");
    
    let swapchain_extent = if capabilities.current_extent.width != u32::MAX {
      capabilities.current_extent
    } else {
      vk::Extent2D {
        width: window.inner_size().width.clamp(capabilities.min_image_extent.width, capabilities.max_image_extent.width),
        height: window.inner_size().height.clamp(capabilities.min_image_extent.height, capabilities.max_image_extent.height)
      }
    };

    let swapchain_min_image_count = 
      if capabilities.max_image_count > 0 && capabilities.min_image_count.max(3) > capabilities.max_image_count 
        { capabilities.max_image_count } else { capabilities.min_image_count.max(3) };

    let swapchain_create_info = vk::SwapchainCreateInfoKHR::default()
      .surface(surface_khr)
      .min_image_count(swapchain_min_image_count)
      .image_format(swapchain_surface_format.format)
      .image_color_space(swapchain_surface_format.color_space)
      .image_extent(swapchain_extent)
      .image_array_layers(1)
      .image_usage(vk::ImageUsageFlags::COLOR_ATTACHMENT)
      .image_sharing_mode(vk::SharingMode::EXCLUSIVE)
      .pre_transform(capabilities.current_transform)
      .composite_alpha(vk::CompositeAlphaFlagsKHR::OPAQUE)
      .present_mode(swapchain_present_mode)
      .clipped(true);

    unsafe { swapchain.create_swapchain(&swapchain_create_info, None) }.expect("failed to create swapchain!")
  }

  pub fn find_depth_format(&self) -> vk::Format
  {
    let candidates = [vk::Format::D32_SFLOAT, vk::Format::D32_SFLOAT_S8_UINT, vk::Format::D24_UNORM_S8_UINT];

    *candidates.iter().find(|&fmt| {
      let properties = unsafe { self.instance.get_physical_device_format_properties(self.physical_device, *fmt)};
      properties.optimal_tiling_features.contains(vk::FormatFeatureFlags::DEPTH_STENCIL_ATTACHMENT)
    }).expect("failed to find supported format!")
  }

  pub fn get_surface_format(&self) -> vk::Format
  {
    Self::get_swapchain_surface_format(&self.surface, self.surface_khr, self.physical_device).format
  }
}