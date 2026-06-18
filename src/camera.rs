use nalgebra_glm as glm;

pub const WORLD_UP:       glm::Vec3 = glm::Vec3::new(0.0, 1.0, 0.0);
pub const WORLD_RIGHT:    glm::Vec3 = glm::Vec3::new(1.0, 0.0, 0.0);
pub const WORLD_FORWARD:  glm::Vec3 = glm::Vec3::new(0.0, 0.0, 1.0);

#[derive(Default)]
pub struct Camera
{
  pub pos: glm::Vec3,
  pub rot: glm::Quat,

  pub move_speed: f32,
  pub rot_speed: f32,
  pub shift_speed: f32,

  pub fov: f32,
  pub fov_speed: f32,

  pub aspect: f32,
  pub near_plane: f32,
  pub far_plane: f32
}

impl Camera
{
  pub fn new(width: u32, height: u32, position: glm::Vec3, euler_angles: glm::Vec3) -> Self
  {
    Self {
      pos: position,
      rot:  glm::quat_angle_axis(euler_angles.y, &WORLD_UP) * 
            glm::quat_angle_axis(euler_angles.x, &WORLD_RIGHT) * 
            glm::quat_angle_axis(euler_angles.z, &WORLD_FORWARD).normalize(),
      move_speed: 1.0,
      rot_speed: 0.5,
      shift_speed: 2.0,
      fov: 45.0,
      fov_speed: 50.0,
      aspect: width as f32 / height as f32,
      near_plane: 0.03,
      far_plane: 1000.0
    }
  }

  pub fn get_view_matrix(&self) -> glm::Mat4
  {
    return glm::look_at_lh(&self.pos, &(self.pos + glm::quat_rotate_vec3(&self.rot, &WORLD_FORWARD)), &WORLD_UP);
  }

  pub fn get_proj_matrix(&self) -> glm::Mat4
  {
    let mut proj: glm::Mat4 = glm::perspective(
      self.aspect, self.fov, self.near_plane, self.far_plane);
    proj.m22 *= -1.0;
    return proj;
  }

  pub fn update(&mut self, velocity: glm::Vec3, look: glm::Quat, delta_fov: f32, shift_mod: bool)
  {
    let modifier: f32 = if shift_mod { self.shift_speed } else { 1.0 };
    let rotated_velocity: glm::Vec3 = 
      glm::quat_rotate_vec3(&self.rot, &WORLD_RIGHT) * velocity.x +
      &WORLD_UP * velocity.y +
      glm::quat_rotate_vec3(&self.rot, &WORLD_FORWARD) * velocity.z
    ;
    self.pos += rotated_velocity * modifier * self.move_speed;
    self.rot = (self.rot * look).normalize();
    self.fov += delta_fov * self.fov_speed;
  }
}