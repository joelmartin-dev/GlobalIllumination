use winit::{application::ApplicationHandler, event::WindowEvent, event_loop::ActiveEventLoop, keyboard::{KeyCode, ModifiersState, PhysicalKey}, window::{Window, WindowId}};

#[cfg(target_os = "windows")]
use winit::platform::windows::WindowAttributesExtWindows;

use crate::renderer::Renderer;


#[derive(Default)]
pub struct App {
  window: Option<Window>,
  modifiers_state: ModifiersState,
  renderer: Option<Renderer>
}

impl ApplicationHandler for App {
    fn resumed(&mut self, event_loop: &ActiveEventLoop) 
    {
      self.window = Some(event_loop.create_window(Window::default_attributes().with_title("Beans Engine")).expect("failed to create winit window!"));
      self.renderer = match Renderer::new(self.window.as_ref().unwrap()) {
        Ok(v) => Some(v),
        Err(e) => { println!("{}", e); None }
      };
    }

    fn window_event(&mut self, event_loop: &ActiveEventLoop, window_id: WindowId, event: WindowEvent) 
    {
      let renderer = self.renderer.as_mut().unwrap();
      let window = self.window.as_ref().unwrap();

      match event {
        WindowEvent::CloseRequested => {
          event_loop.exit();
        },
        WindowEvent::RedrawRequested => {
          match renderer.present_frame() {
            Err(e) => println!("{}", e),
            _ => ()
          };

          if let Some(window) = self.window.as_ref() { window.request_redraw(); }
        },
        WindowEvent::KeyboardInput { device_id, event, is_synthetic } => {
          match &event.physical_key {
            PhysicalKey::Code(KeyCode::Escape) => event_loop.exit(),
            _ => ()
          }
        }
        _ => ()
      }
    }
}