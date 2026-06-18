use ash::vk as vk;
use nalgebra_glm as glm;
use memoffset::offset_of;

// Attributes
#[repr(C)]
#[derive(Debug, PartialEq, Clone, Copy)]
pub struct Vertex {
  pub pos:        glm::Vec3,
  pub tex_coord:  glm::Vec2,
  pub colour:     glm::Vec3,
  pub norm:       glm::Vec3,
  pub tang:       glm::Vec4
}

pub const TRIANGLE_VERTICES: [Vertex; 3] = [
  Vertex {
    pos: glm::Vec3::new(-1.0, -1.0, -0.0), tex_coord: glm::Vec2::new(0.0, 0.0), 
    colour: glm::Vec3::new(1.0, 0.0, 0.0), norm: glm::Vec3::new(0.0, 0.0, 0.0), 
    tang: glm::Vec4::new(0.0, 0.0, 0.0, 1.0)
  },
  Vertex {
    pos: glm::Vec3::new(-1.0, 3.0, -0.0), tex_coord: glm::Vec2::new(0.0, 2.0), 
    colour: glm::Vec3::new(1.0, 1.0, 1.0), norm: glm::Vec3::new(0.0, 0.0, 0.0), 
    tang: glm::Vec4::new(0.0, 0.0, 0.0, 1.0)
  },
  Vertex {
    pos: glm::Vec3::new(3.0, -1.0, -0.0), tex_coord: glm::Vec2::new(2.0, 0.0), 
    colour: glm::Vec3::new(0.0, 1.0, 0.0), norm: glm::Vec3::new(0.0, 0.0, 0.0), 
    tang: glm::Vec4::new(0.0, 0.0, 0.0, 1.0)
  }
];

pub const TRIANGLE_INDICES: [u32; 3] = [ 0, 1, 2 ];

impl Vertex {
  // How the struct is passed
  pub const fn get_binding_description() -> vk::VertexInputBindingDescription
  {
    return vk::VertexInputBindingDescription { 
      binding: 0, stride: size_of::<Vertex>() as u32, input_rate: vk::VertexInputRate::VERTEX };
  }

  // How the struct's data is laid out
  pub const fn get_attribute_descriptions() -> [vk::VertexInputAttributeDescription; 5]
  {
    return [
      // location, binding, format, offset
      // Binding is 0, as we decided in getBindingDescription
      // Formats are aliases for in-shader data types, e.g. R32Sfloat is float, R64Sfloat is double
      vk::VertexInputAttributeDescription { 
        location: 0, binding: 0, format: vk::Format::R32G32B32_SFLOAT,    offset: offset_of!(Self, pos) as u32 },
      vk::VertexInputAttributeDescription { 
        location: 1, binding: 0, format: vk::Format::R32G32_SFLOAT,       offset: offset_of!(Self, tex_coord) as u32 },
      vk::VertexInputAttributeDescription { 
        location: 2, binding: 0, format: vk::Format::R32G32B32_SFLOAT,    offset: offset_of!(Self, colour) as u32 },
      vk::VertexInputAttributeDescription { 
        location: 3, binding: 0, format: vk::Format::R32G32B32_SFLOAT,    offset: offset_of!(Self, norm) as u32 },
      vk::VertexInputAttributeDescription { 
        location: 4, binding: 0, format: vk::Format::R32G32B32A32_SFLOAT, offset: offset_of!(Self, tang) as u32 }
    ];
  }
}