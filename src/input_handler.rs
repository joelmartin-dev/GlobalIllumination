use nalgebra_glm as glm;
use winit::{event::{ElementState, KeyEvent}, keyboard::{Key, ModifiersState, NamedKey}};

struct Inputs
{
  pub velocity: glm::TVec::<f32, 3>,
  pub look: glm::TVec::<f32, 3>,
  pub delta_fov: f32,
  pub shift_mod: bool
}

impl Inputs
{
  pub fn new() -> Self
  {
    Self { velocity: glm::zero(), look: glm::zero(), delta_fov: 0.0, shift_mod: false }
  }

  pub fn key_handler(&mut self, event: &KeyEvent, modifiers: ModifiersState)
  {
    // if any modifiers are pressed (except shift), return
    if !modifiers.is_empty() && !modifiers.shift_key() { return; }
    match event.state {
      ElementState::Pressed => {
        // println!("Pressed: {:?}", key.as_ref());
        match event.logical_key.as_ref()
        {
          Key::Character("a") | Key::Character("A") => self.velocity.x  =  1.0,
          Key::Character("d") | Key::Character("D") => self.velocity.x  = -1.0,
          Key::Character("q") | Key::Character("Q") => self.velocity.y  = -1.0,
          Key::Character("e") | Key::Character("E") => self.velocity.y  =  1.0,
          Key::Character("s") | Key::Character("S") => self.velocity.z  =  1.0,
          Key::Character("w") | Key::Character("W") => self.velocity.z  = -1.0,
          Key::Named(NamedKey::ArrowDown)           => self.look.x      =  1.0,
          Key::Named(NamedKey::ArrowUp)             => self.look.x      = -1.0,
          Key::Named(NamedKey::ArrowLeft)           => self.look.y      = -1.0,
          Key::Named(NamedKey::ArrowRight)          => self.look.y      =  1.0,
          Key::Character("-") | Key::Character("_") => self.delta_fov   = -1.0,
          Key::Character("=") | Key::Character("+") => self.delta_fov   =  1.0,
          Key::Named(NamedKey::Shift)               => self.shift_mod   = true,
          _ => ()
        }
      },
      ElementState::Released => {
        match event.logical_key.as_ref() {
          Key::Character("w") | Key::Character("W") | Key::Character("s") | Key::Character("S") => self.velocity.z  =   0.0,
          Key::Character("a") | Key::Character("A") | Key::Character("d") | Key::Character("D") => self.velocity.x  =   0.0,
          Key::Character("q") | Key::Character("Q") | Key::Character("e") | Key::Character("E") => self.velocity.y  =   0.0,
          Key::Named(NamedKey::ArrowUp)   | Key::Named(NamedKey::ArrowDown)                     => self.look.x      =   0.0,
          Key::Named(NamedKey::ArrowLeft) | Key::Named(NamedKey::ArrowRight)                    => self.look.y      =   0.0,
          Key::Character("-") | Key::Character("_") | Key::Character("=") | Key::Character("+") => self.delta_fov   =   0.0,
          Key::Named(NamedKey::Shift)                                                           => self.shift_mod   = false,
          _ => ()
        }
      }
    }
  }
}