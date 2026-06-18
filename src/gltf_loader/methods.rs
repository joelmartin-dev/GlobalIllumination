use std::{any::type_name, fs, path::Path, str::FromStr};

use iri_string::{percent_encode::PercentEncoded, spec::IriSpec, types::IriReferenceStr};
use serde::{Deserialize, Deserializer, Serializer, de::Error};
use base64::{engine::general_purpose::STANDARD, Engine as _};

use crate::gltf_loader::{GltfDocument, Validatable, enums::{MeshPrimitiveMode, Undefinable}};

impl GltfDocument {
  pub fn load(path: &Path) -> Result<(Self, Vec<Vec<u8>>), String> {
    let parsed: Result<GltfDocument, serde_json::Error> = serde_json::from_str(match &fs::read_to_string(path) { Ok(v) => v, Err(e) => Err(e.to_string())?});

    if let Ok(loaded) = parsed {
      loaded.is_valid(&loaded)?;
      
      if let Some(accessors)    = &loaded.accessors     
        { accessors   .iter().try_for_each(|accessor|     accessor    .is_valid(&loaded))? };
      
      if let Some(animations)   = &loaded.animations    
        { animations  .iter().try_for_each(|animation|    animation   .is_valid(&loaded))? };
      
      loaded.asset.is_valid(&loaded)?;
      
      if let Some(buffers)      = &loaded.buffers       
        { buffers     .iter().try_for_each(|buffer|       buffer      .is_valid(&loaded))? };

      if let Some(buffer_views) = &loaded.buffer_views  
        { buffer_views.iter().try_for_each(|buffer_view|  buffer_view .is_valid(&loaded))? };
      
      if let Some(cameras)      = &loaded.cameras       
        { cameras     .iter().try_for_each(|camera|       camera      .is_valid(&loaded))? };
      
      if let Some(images)       = &loaded.images        
        { images      .iter().try_for_each(|image|        image       .is_valid(&loaded))? };
      
      if let Some(materials)    = &loaded.materials     
        { materials   .iter().try_for_each(|material|     material    .is_valid(&loaded))? };
      
      if let Some(meshes)       = &loaded.meshes        
        { meshes      .iter().try_for_each(|mesh|         mesh        .is_valid(&loaded))? };
      
      if let Some(nodes)        = &loaded.nodes         
        { nodes       .iter().try_for_each(|node|         node        .is_valid(&loaded))? };
      
      if let Some(samplers)     = &loaded.samplers      
        { samplers    .iter().try_for_each(|sampler|      sampler     .is_valid(&loaded))? };
      
      if let Some(scenes)       = &loaded.scenes        
        { scenes      .iter().try_for_each(|scene|        scene       .is_valid(&loaded))? };
      
      if let Some(skins)        = &loaded.skins         
        { skins       .iter().try_for_each(|skin|         skin        .is_valid(&loaded))? };
      
      if let Some(textures)     = &loaded.textures      
        { textures    .iter().try_for_each(|texture|      texture     .is_valid(&loaded))? };
      
      let mut loaded_buffers: Vec<Vec<u8>> = Vec::new();
      let parent_path = match path.parent() { Some(v) => v, None => Err(&"failed to get parent path!".to_string())? };
      if let Some(buffers)      = &loaded.buffers
        {
          let _ = buffers.iter().try_for_each(|buffer| -> Result<(), String>
            { 
              if let Some(uri) = &buffer.uri {
                const VALID_URI_MIME_TYPES: &[&str] = &[
                  "application/octet-stream", "application/gltf-buffer"
                ];
                let encoded_iri = match IriReferenceStr::new(uri) { Ok(v) => v, Err(e) => Err(e.to_string())?};
                if encoded_iri.scheme_str() != Some("data") { 
                  let decoded_uri = iri_string::percent_encode::decode::decode_whatwg_bytes(uri.as_bytes());
                  match fs::read(parent_path.join(match decoded_uri.into_string() { Ok(v) => v, Err(e) => Err(e.to_string())?})) {
                    Ok(bytes_vec) => loaded_buffers.push(bytes_vec.clone()),
                    Err(e) => Err(e.to_string())?
                  }
                }
                let body = match encoded_iri.as_str().strip_prefix("data:") {
                  Some(v) => v, None => Err("Invalid embedded buffer!".to_string())?
                };
                let mut segments = body.split(|c| c == ';' || c == ',');
                
                let mime_type = (match segments.next() {
                  Some(v) => v, None => Err("Failed to get mime type from uri!".to_string())?
                }).trim();
                println!("{}", mime_type);
                if !VALID_URI_MIME_TYPES.contains(&mime_type) { Err("Uri contained invalid mime type!".to_string())? };
                let rest = segments.next();
                if rest == Some("base64") {
                  let data = match segments.last() {
                    Some(enc_v) => match STANDARD.decode(enc_v) {
                      Ok(v) => v, Err(e) => Err(e.to_string())?
                    },
                    None => Err("No data found in uri!".to_string())?
                  };
                  loaded_buffers.push(data);
                }
              }
              Ok(())
            });
        };

      Ok((loaded, loaded_buffers))
    }
    else {
      Err(parsed.unwrap_err().to_string())
    }
  }
}

// #region Deserializers
pub fn deserialize_from_usize_to_enum<'de, D, T>(deserializer: D) -> Result<T, D::Error>
where D: Deserializer<'de>, T: From<usize> + Undefinable
{
  let val = usize::deserialize(deserializer)?;
  match val {
    n if T::from(n).is_undefined() => {
      let type_name = type_name::<T>().split("::").last().unwrap_or("unknown");
      Err(Error::custom(format!("Invalid {} field!", type_name)))
    },
    v => Ok(T::from(v))
  }
}

pub fn deserialize_from_option_usize_to_enum<'de, D, T>(deserializer: D) -> Result<Option<T>, D::Error>
where D: Deserializer<'de>, T: From<usize> + Undefinable
{
  let val = Option::<usize>::deserialize(deserializer)?;
  match val {
    Some(n) if T::from(n).is_undefined() => {
      let type_name = type_name::<T>().split("::").last().unwrap_or("unknown");
      Err(Error::custom(format!("Invalid {} field!", type_name)))
    },
    v => Ok(v.map(T::from))
  }
}

pub fn deserialize_from_string_to_enum<'de, D, T>(deserializer: D) -> Result<T, D::Error>
where D: Deserializer<'de>, T: From<String> + Undefinable
{
  let val = String::deserialize(deserializer)?;
  match val {
    n if T::from(n.clone()).is_undefined() => {
      let type_name = type_name::<T>().split("::").last().unwrap_or("unknown");
      Err(Error::custom(format!("Invalid {} field!", type_name)))
    },
    v => Ok(T::from(v))
  }
}

pub fn deserialize_from_option_string_to_enum<'de, D, T>(deserializer: D) -> Result<Option<T>, D::Error>
where D: Deserializer<'de>, T: From<String> + Undefinable
{
  let val = Option::<String>::deserialize(deserializer)?;
  match val {
    Some(n) if T::from(n.clone()).is_undefined() => {
      let type_name = type_name::<T>().split("::").last().unwrap_or("unknown");
      Err(Error::custom(format!("Invalid {} field!", type_name)))
    },
    v => Ok(v.map(T::from))
  }
}

pub fn deserialize_string_to_iri<'de, D>(deserializer: D) -> Result<Option<String>, D::Error>
where D: Deserializer<'de>
{
  let val = Option::<String>::deserialize(deserializer)?;
  match val {
    Some(raw_uri) => match IriReferenceStr::new(&raw_uri) {
      Ok(_) => Ok(Some(raw_uri)),
      Err(_) => Ok(Some(PercentEncoded::<_, IriSpec>::from_path(raw_uri).to_string()))
    },
    None => Ok(None)
  }
}
// #endregion

// #region Serializers
pub fn serialize_to_u64<S, T>(val: &T, serializer: S) -> Result<S::Ok, S::Error>
where S: Serializer, T: Into<u64> + Copy
{
  serializer.serialize_u64((*val).into())
}

pub fn serialize_option_to_u64<S, T>(val: &Option<T>, serializer: S) -> Result<S::Ok, S::Error>
where S: Serializer, T: Into<u64> + Copy
{
  match val {
    Some(v) => serializer.serialize_u64((*v).into()),
    _ => serializer.serialize_none()
  }
}

pub fn serialize_to_str<'se, S, T>(val: &T, serializer: S) -> Result<S::Ok, S::Error>
where S: Serializer, T: Into<&'se str> + Copy
{
  serializer.serialize_str((*val).into())
}

pub fn serialize_option_to_str<'se, S, T>(val: &Option<T>, serializer: S) -> Result<S::Ok, S::Error>
where S: Serializer, T: Into<&'se str> + Copy
{
  match val {
    Some(v) => serializer.serialize_str((*v).into()),
    _ => serializer.serialize_none()
  }
}
// #endregion

// #region Defaults
pub fn default_base_color_factor() -> [f32; 4] { [1.0, 1.0, 1.0, 1.0] }
pub fn default_emissive_factor() -> [f32; 3] { [0.0, 0.0, 0.0] }
pub fn default_f32_1() -> f32 { 1.0 }
pub fn default_f32_half() -> f32 { 0.5 }
pub fn default_f32_0() -> f32 { 0.0 }
pub fn default_matrix() -> [f32; 16] { [ 1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0 ] }
pub fn default_mesh_primitive_mode() -> MeshPrimitiveMode { MeshPrimitiveMode::from(4) }
pub fn default_rotation() -> [f32; 4] { [ 0.0, 0.0, 0.0, 1.0] }
pub fn default_scale() -> [f32; 3] { [1.0, 1.0, 1.0] }
pub fn default_translation() -> [f32; 3] { [0.0, 0.0, 0.0] }

// Extensions
// #endregion






