use std::time::Instant;

use winit::{application::ApplicationHandler, event::{ElementState, WindowEvent}, event_loop::ActiveEventLoop, keyboard::{Key, KeyCode, ModifiersState, NamedKey, PhysicalKey}, window::{Window, WindowId}};

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
      let modifiers = self.modifiers_state;

      match event {
        WindowEvent::CloseRequested => {
          event_loop.exit();
        },
        WindowEvent::RedrawRequested => {
          let frame_start = Instant::now();
          match renderer.present_frame() {
            Err(e) => println!("{}", e),
            _ => ()
          };
          renderer.frame_delta = Instant::now().duration_since(frame_start).as_secs_f32();
          if let Some(window) = self.window.as_ref() { window.request_redraw(); }
        },
        WindowEvent::KeyboardInput { device_id, event, is_synthetic } => {
          match &event.physical_key {
            PhysicalKey::Code(KeyCode::Escape) => event_loop.exit(),
            _ => ()
          }
          // if any modifiers are pressed (except shift), return
          if !modifiers.is_empty() && !modifiers.shift_key() { return; }
          match event.state {
            ElementState::Pressed => {
              // println!("Pressed: {:?}", key.as_ref());
              match event.logical_key.as_ref()
              {
                Key::Character("a") | Key::Character("A") => renderer.camera_velocity.x = -1.0,
                Key::Character("d") | Key::Character("D") => renderer.camera_velocity.x =  1.0,
                Key::Character("q") | Key::Character("Q") => renderer.camera_velocity.y = -1.0,
                Key::Character("e") | Key::Character("E") => renderer.camera_velocity.y =  1.0,
                Key::Character("s") | Key::Character("S") => renderer.camera_velocity.z =  1.0,
                Key::Character("w") | Key::Character("W") => renderer.camera_velocity.z = -1.0,
                Key::Named(NamedKey::ArrowDown)           => renderer.camera_look.x     = -1.0,
                Key::Named(NamedKey::ArrowUp)             => renderer.camera_look.x     =  1.0,
                Key::Named(NamedKey::ArrowLeft)           => renderer.camera_look.y     =  1.0,
                Key::Named(NamedKey::ArrowRight)          => renderer.camera_look.y     = -1.0,
                Key::Character("-") | Key::Character("_") => renderer.delta_fov  = -1.0,
                Key::Character("=") | Key::Character("+") => renderer.delta_fov  =  1.0,
                Key::Named(NamedKey::Shift)               => renderer.shift_mod  = true,
                _ => ()
              }
            },
            ElementState::Released => {
              match event.logical_key.as_ref() {
              Key::Character("w") | Key::Character("W") | Key::Character("s") | Key::Character("S") => renderer.camera_velocity.z =   0.0,
              Key::Character("a") | Key::Character("A") | Key::Character("d") | Key::Character("D") => renderer.camera_velocity.x =   0.0,
              Key::Character("q") | Key::Character("Q") | Key::Character("e") | Key::Character("E") => renderer.camera_velocity.y =   0.0,
              Key::Named(NamedKey::ArrowUp)   | Key::Named(NamedKey::ArrowDown)                     => renderer.camera_look.x     =   0.0,
              Key::Named(NamedKey::ArrowLeft) | Key::Named(NamedKey::ArrowRight)                    => renderer.camera_look.y     =   0.0,
              Key::Character("-") | Key::Character("_") | Key::Character("=") | Key::Character("+") => renderer.delta_fov         =   0.0,
              Key::Named(NamedKey::Shift)                                                           => renderer.shift_mod         = false,
                _ => ()
              }
            }
          }
        },
        WindowEvent::DroppedFile(path) => {
          // println!("File dropped: {:?}", path);
          match renderer.load_gltf_from_path(&path)
          {
            Err(e) => println!("{}", e),
            _ => ()
          };
        },
        _ => ()
      }
    }
}