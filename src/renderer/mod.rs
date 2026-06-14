use winit::window::Window;

use crate::renderer::context::VulkanContext;

mod context;
mod slang_compiler;
mod gui;

pub struct Renderer
{
  context: VulkanContext 
}

impl Renderer
{
  pub fn new(window: &Window) -> Self
  {
    Self {
      context: VulkanContext::new(window)
    }
  }
}