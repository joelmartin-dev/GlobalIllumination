use std::path::{Path};

use crate::{gltf_loader::{Accessor, BufferView, GltfDocument, Mesh, Node, Scene, enums::{AccessorType, ComponentType, MaterialAlphaMode}}, renderer::{Renderer, buffer_structs::SubMesh, vertex::Vertex}};
use ash::{Device, vk};
use nalgebra_glm as glm;

impl Renderer
{
  pub fn component_size(component_type: ComponentType) -> Result<usize, String> 
  {
    match component_type {
      ComponentType::Byte => Ok(size_of::<i8>()),
      ComponentType::UnsignedByte => Ok(size_of::<u8>()),
      ComponentType::Short => Ok(size_of::<i16>()),
      ComponentType::UnsignedShort => Ok(size_of::<u16>()),
      ComponentType::UnsignedInt => Ok(size_of::<u32>()),
      ComponentType::Float => Ok(size_of::<f32>()),
      _ => Err("Unsupported component type!")?
    }
  }

  fn num_components(accessor_type: AccessorType) -> Result<usize, String>
  {
    match accessor_type {
      AccessorType::Scalar => Ok(1),
      AccessorType::Vec2 => Ok(2),
      AccessorType::Vec3 => Ok(3),
      AccessorType::Vec4 => Ok(4),
      AccessorType::Mat2 => Ok(4),
      AccessorType::Mat3 => Ok(9),
      AccessorType::Mat4 => Ok(16),
      _ => Err("Unsupported accessor type!")?
    }
  }

  fn dequantize_value(bytes: &[u8], component_type: ComponentType, normalized: bool) -> Result<f32, String>
  {
    match component_type {
      ComponentType::Byte => {
        let val = i8::from_le_bytes(bytes.try_into().map_err(|_| "failed to convert bytes to i8!")?);
        if normalized { Ok((val as f32 / 127.0).max(-1.0)) } else { Ok(val as f32) }
      },
      ComponentType::UnsignedByte => {
        let val = u8::from_le_bytes(bytes.try_into().map_err(|_| "failed to convert bytes to u8!")?);
        if normalized { Ok(val as f32 / 255.0) } else { Ok(val as f32) }
      },
      ComponentType::Short => {
        let val = i16::from_le_bytes(bytes.try_into().map_err(|_| "failed to convert bytes to i16!")?);
        if normalized { Ok((val as f32 / 32767.0).max(-1.0)) } else { Ok(val as f32) }
      },
      ComponentType::UnsignedShort => {
        let val = u16::from_le_bytes(bytes.try_into().map_err(|_| "failed to convert bytes to u16!")?);
        if normalized { Ok(val as f32 / 65535.0) } else { Ok(val as f32) }
      },
      ComponentType::Float => {
        Ok(f32::from_le_bytes(bytes.try_into().map_err(|_| "failed to convert bytes to f32!")?))
      },
      _ => Err(format!("unsupported component type: {:?}!", component_type))?
    }
  }

  fn parse_accessor(accessor: &Accessor, buffer_view: &BufferView, buffer: &[u8]) -> Result<Vec<f32>, String>
  {
    let accessor_type = accessor.ty;
    let component_type = accessor.component_type;
    let accessor_count = accessor.count;
    let accessor_offset = accessor.byte_offset.unwrap_or(0);
    let num_comps = Self::num_components(accessor_type)?;
    let comp_size = Self::component_size(component_type)?;
    let element_size = num_comps * comp_size;
    let stride = buffer_view.byte_stride.unwrap_or(element_size);
    let buffer_view_offset = buffer_view.byte_offset.unwrap_or(0);
    let normalized = accessor.normalized;
      
    let mut result = Vec::with_capacity(accessor_count * num_comps);
    
    for i in 0..accessor_count {
      let base_offset = buffer_view_offset + accessor_offset + i * stride;
      for j in 0..num_comps {
        let offset = base_offset + j * comp_size;
        let bytes = &buffer[offset..offset + comp_size];
        let value = Self::dequantize_value(bytes, component_type, normalized)?;
        result.push(value);
      }
    }
    
    Ok(result)
  }

  pub fn load_gltf(device: &Device, command_buffer: vk::CommandBuffer, allocator: &vk_mem::Allocator, path: &Path) -> Result<((vk::Buffer, vk_mem::Allocation, vk::Buffer, vk_mem::Allocation), Vec<Vertex>, (vk::Buffer, vk_mem::Allocation, vk::Buffer, vk_mem::Allocation), Vec<u32>), String>
  {
    let (base, bin) = GltfDocument::load(path)?;

    let (loaded_vertices, loaded_indices, loaded_submeshes) = Self::load_geometry(&base, &bin, 0, 0)?;

    Ok((
      Self::create_buffer_from_slice(device, command_buffer, allocator, &loaded_vertices.as_slice(), vk::BufferUsageFlags::VERTEX_BUFFER)?,
      loaded_vertices,
      Self::create_buffer_from_slice(device, command_buffer, allocator, &loaded_indices.as_slice(), vk::BufferUsageFlags::INDEX_BUFFER)?,
      loaded_indices
    ))
  }

  fn load_node(node: &Node, nodes: &Vec<Node>, accessors: &Vec<Accessor>, buffer_views: &Vec<BufferView>, meshes: &Vec<Mesh>, base: &GltfDocument, bin: &Vec<Vec<u8>>, initial_v_offset: u32, initial_index_offset: u32, matrix: &glm::Mat4) 
    -> Result<(Vec<Vertex>, Vec<u32>, Vec<SubMesh>), String>
  {
    let mut vertices: Vec<Vertex> = Vec::new();
    let mut indices: Vec<u32> = Vec::new();
    let mut submeshes: Vec<SubMesh> = Vec::new();

    let translation: glm::Vec3 = glm::make_vec3(&node.translation);
    let rotation = node.rotation;
    let scale: glm::Vec3 = glm::make_vec3(&node.scale);

    let translate_mat = glm::translate(&glm::Mat4::identity(), &translation);
    let rotate_mat = glm::quat_to_mat4(&glm::make_quat(&rotation));
    let scale_mat = glm::scale(&glm::Mat4::identity(), &scale);

    let transform_mat = translate_mat * rotate_mat * scale_mat;

    let world_matrix = if transform_mat != glm::Mat4::identity() 
      { matrix * transform_mat } else { matrix * glm::make_mat4(&node.matrix) };

    if let Some(mesh_idx) = node.mesh {
      let mesh = match meshes.get(mesh_idx) { Some(v) => v, None => Err("Mesh does not exist!")?};
      let _ = mesh.primitives.iter().try_for_each(|prim| -> Result<(), String> {
        let initial_indices_count: u32 = u32::try_from(indices.len()).map_err(|e| e.to_string())?;
        let start_offset: u32 = initial_index_offset + u32::try_from(indices.len()).map_err(|e| e.to_string())?;
        // v_offset will help in evaluating the absolute value of this primitives indices so they match up with the
        // correct vertices in the vertex buffer
        let v_offset: u32 = initial_v_offset + u32::try_from(vertices.len()).map_err(|e| e.to_string())?;
        let pos_accessor = match prim.attributes.get(&String::from("POSITION")) {
          Some(pos_accessor_idx) => match accessors.get(*pos_accessor_idx) { 
            Some(v) => v, None => Err("No positions data found!")?
          },
          None => Err("Primitives must have at least positions defined!")?
        };
        let pos_buffer_view = match pos_accessor.buffer_view { 
          Some(v) => match buffer_views.get(v) { Some(v) => v, None => Err("No buffer view found for positions!")?}, 
          None => Err("Accessor for vertex positions must have a defined buffer view!")?
        };
        let pos_buffer = &bin[pos_buffer_view.buffer];
        let positions: Vec<glm::Vec3> = Self::parse_accessor(&pos_accessor, &pos_buffer_view, &pos_buffer)?.chunks_exact(3)
          .map(|chunk| glm::make_vec3(&chunk)).collect();

        let material = match base.materials.as_ref() {
            Some(materials) => match prim.material {
              Some(mat_idx) => materials.get(mat_idx), None => None
            },
            None => None
          };
        // Get the accessor for where the primitive stores its indices
        if let Some(indices_accessor_index) = prim.indices {
          let accessor: &Accessor = match accessors.get(indices_accessor_index) {
            Some(v) => v, 
            None => Err("No indices data found!")?
          };
          let accessor_byte_offset = accessor.byte_offset.unwrap_or(0);
          let byte_length = accessor.count * match accessor.component_type {
            ComponentType::UnsignedByte => { size_of::<u8>() },
            ComponentType::UnsignedShort => { size_of::<u16>() },
            ComponentType::UnsignedInt => { size_of::<u32>() }
            _ => Err("incompatible component type")?
          };
          let buffer_view: &BufferView = match accessor.buffer_view { 
            Some(idx) => match buffer_views.get(idx) { 
              Some(v) => v,
              None => Err("No buffer view found for indices!")?
            },
            None => Err("Accessor for indices has no buffer view defined!")?
          };
          let byte_offset = buffer_view.byte_offset.unwrap_or(0) + accessor_byte_offset;
          let buffer: &Vec<u8> = match &bin.get(buffer_view.buffer) { 
            Some(v) => v, 
            None => Err("Buffer view contains invalid buffer index!")? 
          };
          let buffer_data = match buffer.get(byte_offset..byte_offset+byte_length) {
            Some(v) => v,
            None => Err("Buffer did not contain required slice!")?
          };

          let prim_indices: Vec<u32> = match accessor.component_type {
            ComponentType::UnsignedByte => {
              buffer_data.iter().map(|&byte| (byte as u32) + v_offset).collect()
            },
            ComponentType::UnsignedShort => {
              buffer_data.chunks_exact(size_of::<u16>()).map(|chunk| 
                (u16::from_le_bytes(chunk.try_into().unwrap()) as u32) + v_offset).collect()
            },
            ComponentType::UnsignedInt => {
              buffer_data.chunks_exact(size_of::<u32>()).map(|chunk| 
                u32::from_le_bytes(chunk.try_into().unwrap()) + v_offset).collect()
            },
            _ => { Err("incompatible indices component type!")? }
          };

          indices.extend(&prim_indices);
          // Insert the indices in reverse order if the material is double-sided (triggers a redraw of the backface as a 
          // frontface, using a reverse iterator and offsets from rbegin (which is the end in the direction of begin)
          if let Some(mat) = material {
            if mat.double_sided {
              indices.extend(prim_indices.iter().rev());
            }
          }
        }
        else {
          indices.extend((0..positions.len() as u32).collect::<Vec<u32>>());
        }
        
        let mat_idx = prim.material.unwrap_or(0);

        // Load UVs
        let accessor: Option<&Accessor> = match prim.attributes.get(&String::from("TEXCOORD_0")) {
          Some(accessor_idx) => match accessors.get(*accessor_idx) { 
            Some(v) => Some(v), None => Err("No texture coordinates data found!")?
          }, None => None
        };
        let buffer_view: Option<&BufferView> = match &accessor {
          Some(accessor) => match accessor.buffer_view { 
            Some(idx) => match buffer_views.get(idx) { Some(v) => Some(v), None => Err("No buffer view found for texture coordinates!")?}, 
            None => Err("Accessor for vertex texture coordinates must have a defined buffer view!")?
          }, None => None
        };
        let buffer: Option<&Vec<u8>> = match &buffer_view {
          Some(buffer_view) => match &bin.get(buffer_view.buffer) { 
            Some(v) => Some(v), 
            None => Err("Buffer view contains invalid buffer index!")? 
          }, None => None
        };
        let uvs: Vec<glm::Vec2> = match buffer {
          Some(buffer) => Self::parse_accessor(accessor.unwrap(), buffer_view.unwrap(), &buffer)?.chunks_exact(2)
            .map(|chunk| glm::make_vec2(&chunk)).collect(),
          None => {
            println!("Loading default texture coordinates...");
            vec![glm::vec2(0.0, 0.0); positions.len()]
          }
        };

        // Load colours
        let accessor: Option<&Accessor> = match prim.attributes.get(&String::from("COLOR_0")) {
          Some(accessor_idx) => match accessors.get(*accessor_idx) { 
            Some(v) => Some(v), None => Err("No vertex colour data found!")?
          }, None => None
        };
        let buffer_view: Option<&BufferView> = match &accessor {
          Some(accessor) => match accessor.buffer_view { 
            Some(idx) => match buffer_views.get(idx) { Some(v) => Some(v), None => Err("No buffer view found for vertex colours!")?}, 
            None => Err("Accessor for vertex colours must have a defined buffer view!")?
          }, None => None
        };
        let buffer: Option<&Vec<u8>> = match &buffer_view {
          Some(buffer_view) => match &bin.get(buffer_view.buffer) { 
            Some(v) => Some(v), 
            None => Err("Buffer view contains invalid buffer index!")? 
          }, None => None
        };
        let cols: Vec<glm::Vec3> = match buffer {
          Some(buffer) => Self::parse_accessor(accessor.unwrap(), buffer_view.unwrap(), &buffer)?.chunks_exact(3)
            .map(|chunk| glm::make_vec3(&chunk)).collect(),
          None => {
            println!("Loading default vertex colours...");
            vec![glm::vec3(1.0, 1.0, 1.0); positions.len()]
          }
        };

        // Load normals
        let accessor: Option<&Accessor> = match prim.attributes.get(&String::from("NORMAL")) {
          Some(accessor_idx) => match accessors.get(*accessor_idx) { 
            Some(v) => Some(v), None => Err("No normals data found!")?
          }, None => None
        };
        let buffer_view: Option<&BufferView> = match &accessor {
          Some(accessor) => match accessor.buffer_view { 
            Some(idx) => match buffer_views.get(idx) { Some(v) => Some(v), None => Err("No buffer view found for vertex normals!")?}, 
            None => Err("Accessor for vertex normals must have a defined buffer view!")?
          }, None => None
        };
        let buffer: Option<&Vec<u8>> = match &buffer_view {
          Some(buffer_view) => match &bin.get(buffer_view.buffer) { 
            Some(v) => Some(v), 
            None => Err("Buffer view contains invalid buffer index!")? 
          }, None => None
        };
        let norms: Vec<glm::Vec3> = match buffer {
          Some(buffer) => Self::parse_accessor(accessor.unwrap(), buffer_view.unwrap(), &buffer)?.chunks_exact(3)
            .map(|chunk| glm::make_vec3(&chunk)).collect(),
          None => {
            println!("Loading default normals...");
            vec![glm::vec3(0.0, 1.0, 0.0); positions.len()]
          }
        };

        // Load tangents
        let accessor: Option<&Accessor> = match prim.attributes.get(&String::from("TANGENT")) {
          Some(accessor_idx) => match accessors.get(*accessor_idx) { 
            Some(v) => Some(v), None => Err("No tangent data found!")?
          }, None => None
        };
        let buffer_view: Option<&BufferView> = match &accessor {
          Some(accessor) => match accessor.buffer_view { 
            Some(idx) => match buffer_views.get(idx) { Some(v) => Some(v), None => Err("No buffer view found for vertex tangents!")?}, 
            None => Err("Accessor for vertex tangents must have a defined buffer view!")?
          }, None => None
        };
        let buffer: Option<&Vec<u8>> = match &buffer_view {
          Some(buffer_view) => match &bin.get(buffer_view.buffer) { 
            Some(v) => Some(v), 
            None => Err("Buffer view contains invalid buffer index!")? 
          }, None => None
        };
        let tangents: Vec<glm::Vec4> = match buffer {
          Some(buffer) => Self::parse_accessor(accessor.unwrap(), buffer_view.unwrap(), &buffer)?.chunks_exact(4)
            .map(|chunk| glm::make_vec4(&chunk)).collect(),
          None => {
            println!("Loading default tangent...");
            vec![glm::vec4(0.0, 0.0, 0.0, 1.0); positions.len()]
          }
        };

        // Instantiate new default vertices
        vertices.reserve(positions.len());
        for i in 0..positions.len() {
          let homogenous_pos = glm::vec4(positions[i].x, positions[i].y, positions[i].z, 1.0);
          let transformed_pos = world_matrix * homogenous_pos;
          vertices.push(Vertex {
            pos: glm::vec3(transformed_pos.x, transformed_pos.y, transformed_pos.z),
            tex_coord: uvs[i],
            colour: cols[i],
            norm: norms[i],
            tang: tangents[i]
          });
        }

        submeshes.push( SubMesh {
          index_offset: start_offset,
          index_count: u32::try_from(indices.len()).map_err(|e| e.to_string())? - initial_indices_count,
          material_id: u32::try_from(mat_idx).map_err(|e| e.to_string())?,
          first_vertex: initial_v_offset,
          max_vertex: initial_v_offset + u32::try_from(vertices.len()).map_err(|e| e.to_string())?,
          alpha_cut: match material {
            Some(mat) => if mat.alpha_mode == MaterialAlphaMode::Opaque { vk::FALSE } else { vk::TRUE },
            None => vk::FALSE
          },
        });

        Ok(())
      });
    }

    if let Some(children) = &node.children {
      children.iter().try_for_each(|&idx| -> Result<(), String> {
        let (loaded_vertices, loaded_indices, loaded_submeshes) = 
          Self::load_node(
            nodes.get(idx).unwrap(), nodes, accessors, buffer_views, meshes, base, bin, 
            initial_v_offset + u32::try_from(vertices.len()).map_err(|e| e.to_string())?, 
            initial_index_offset + u32::try_from(indices.len()).map_err(|e| e.to_string())?, &world_matrix
          )?;
        vertices.extend(loaded_vertices);
        indices.extend(loaded_indices);
        submeshes.extend(loaded_submeshes);
        Ok(())
      })?;
    }

    Ok((vertices, indices, submeshes))
  }

  fn node_has_valid_children(nodes: &Vec<Node>, children_indices: &Vec<usize>) -> bool
  {
    match children_indices.iter().try_for_each(|&idx| -> Result<(), String> {
      if let Some(child) = nodes.get(idx).as_ref() {
        match &child.children {
          Some(children) => match Self::node_has_valid_children(nodes, &children) { 
            true => (), false => Err("Invalid node index!")?
          }, None => ()
        };
        Ok(())
      } else {
        Err("Invalid node index!")?
      }
    }) {
      Ok(_) => true,
      Err(_) => false
    }
  }

  pub fn load_geometry(base: &GltfDocument, bin: &Vec<Vec<u8>>, initial_v_offset: u32, initial_index_offset: u32) -> Result<(Vec<Vertex>, Vec<u32>, Vec<SubMesh>), String>
  {
    let mut vertices: Vec<Vertex> = Vec::new();
    let mut indices: Vec<u32> = Vec::new();
    let mut submeshes: Vec<SubMesh> = Vec::new();

    // Only load if there is a default scene
    if base.scene.is_none() { Err("No default scene found!")?}
    let scene: &Scene = match base.scenes.as_ref() {
      Some(scenes) => match scenes.get(base.scene.unwrap()) { Some(v) => v, None => Err("`scene` did not contain a valid index!")?},
      None => Err("No scene data found!")?
    };
    let node_indices: &Vec<usize> = match scene.nodes.as_ref() { Some(nodes) => nodes, None => Err("No nodes found in scene!")? };

    // Scene exists, make sure required data views exist
    let accessors = match base.accessors.as_ref() {
      Some(accessors) => accessors,
      None => Err("Document needs accessors to get mesh data!")?
    };
    let buffer_views = match base.buffer_views.as_ref() {
      Some(buffer_views) => buffer_views,
      None => Err("Document needs buffer views to get mesh data!")?
    };
    let meshes = match base.meshes.as_ref() {
      Some(meshes) => meshes,
      None => Err("Document needs meshes to get mesh data!")?
    };
    let nodes = match base.nodes.as_ref() {
      Some(nodes) => nodes,
      None => Err("Document needs nodes to get mesh data!")?
    };

    // Check nodes and child nodes are valid
    let _ = node_indices.iter().try_for_each(|&idx| -> Result<(), String> {
      match &nodes.get(idx) {
        Some(node) => match &node.children {
          Some(children_indices) => {
            match Self::node_has_valid_children(&nodes, &children_indices) { 
              true => Ok(()), 
              _ => Err("Invalid node index!")?
            }}, None => Ok(())
        }, None => Err("Invalid node index!")?
      }
    });

    node_indices.iter().try_for_each(|&node_idx| -> Result<(), String> {
      let node = nodes.get(node_idx).unwrap();
      let (loaded_verts, loaded_indices, loaded_submeshes) = 
        Self::load_node(&node, nodes, accessors, buffer_views, meshes, base, bin, 
          initial_v_offset + u32::try_from(vertices.len()).map_err(|e| e.to_string())?, initial_index_offset + u32::try_from(indices.len()).map_err(|e| e.to_string())?, 
          &glm::Mat4::identity()
        )?;
      vertices.extend(loaded_verts);
      indices.extend(loaded_indices);
      submeshes.extend(loaded_submeshes);
      Ok(())
    })?;
    Ok((vertices, indices, submeshes))
  }
}