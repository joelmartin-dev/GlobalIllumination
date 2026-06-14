use winit::{application::ApplicationHandler, event::WindowEvent, event_loop::ActiveEventLoop, keyboard::ModifiersState, window::{Window, WindowId}};

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
      self.window = Some(event_loop.create_window(Window::default_attributes()).expect("failed to create winit window!"));
      self.renderer = Some(Renderer::new(self.window.as_ref().unwrap()));
    }

    fn window_event(&mut self, event_loop: &ActiveEventLoop, window_id: WindowId, event: WindowEvent) 
    {
      match event {
        WindowEvent::CloseRequested => {
          event_loop.exit();
        },
        WindowEvent::RedrawRequested => {


          if let Some(window) = self.window.as_ref() { window.request_redraw(); }
        },
        _ => ()
      }
    }
}