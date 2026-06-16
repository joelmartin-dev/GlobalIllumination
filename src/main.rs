mod application;
mod renderer;
mod camera;

use winit::{event_loop::{ControlFlow, EventLoop}};

use crate::application::App;

// Arch Linux: unset envvar WAYLAND_DISPLAY to force x11
fn main()
{
  let event_loop = EventLoop::new().unwrap();
  event_loop.set_control_flow(ControlFlow::Poll);

  let mut app = App::default();

  event_loop.run_app(&mut app).unwrap();

  return;
}
