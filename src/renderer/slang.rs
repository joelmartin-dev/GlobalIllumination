use std::{ffi::CString, fs::File, io::Write, path::Path};
use shader_slang::Downcast;

pub struct SlangCompiler
{
  global_session: shader_slang::GlobalSession
}

impl SlangCompiler
{
  pub fn new() -> Self
  {
    let global_session = shader_slang::GlobalSession::new().expect("failed to create shader-slang global session!");
    Self
    {
      global_session
    }
  }

  pub fn compile_shader(&self, src: &Path, dst: &Path)
  {
    let global_session = &self.global_session;

    let fsrc = File::open(src);
    if fsrc.is_err() { println!("failed to open {}!", src.display()); return; }

    // We need to establish what this compilation session can and will do
    // Targeting SPIR-V v1.4 and writing it straight to a file
    let target_desc = shader_slang::TargetDesc::default()
    .format(shader_slang::CompileTarget::Spirv)
    .profile(global_session.find_profile("spirv_1_4"));
  
    // target_desc.flags = SlangTargetFlagGenerateSpirvDirectly;

    let targets = [target_desc];

    // Some options that ensure proper output
    let compiler_option_entries = shader_slang::CompilerOptions::default()
      .vulkan_use_entry_point_name(true)
      .matrix_layout_column(true)
      .emit_spirv_directly(true)
      .capability(global_session.find_capability("vk_mem_model"));
 
    // Slang likes to look for the files by itself, even if you pass in an absolute path, so direct it to look in the
    // parent directory of src
    let search_path = CString::new(src.parent().unwrap().to_str().unwrap())
      .expect("failed to convert source file path to CString!");
    let search_paths = [search_path.as_ptr()];

    let session_desc = shader_slang::SessionDesc::default()
      .options(&compiler_option_entries)
      .search_paths(&search_paths)
      .targets(&targets);

    // Create this session from the global session
    // Notice writeRef(). ComPtr is not directly interfaceable, but comes with helper functions like writeRef for passing
    // by reference
    let session = global_session.create_session(&session_desc).expect("failed to create slang session!");

    // Slang does not expect the entirety of a shader to be in one file. It treats each file like a module, links all 
    // the modules together and then outputs the SPIR-V.
    let module = session.load_module(src.to_str().unwrap()).expect("failed to load slang module!");
    let vertex_entry_point = module.find_entry_point_by_name("vertMain").expect("failed to load vertex entry point!");
    let fragment_entry_point = module.find_entry_point_by_name("fragMain").expect("failed to load frag entry point!");
    let compute_entry_point = module.find_entry_point_by_name("compMain").expect("failed to load compute entry point!");

    // Compose/Assemble a program from the module and entrypoints
    let components = [
      module.downcast().clone(), vertex_entry_point.downcast().clone(), 
      fragment_entry_point.downcast().clone(), compute_entry_point.downcast().clone()
    ];
    let program = session.create_composite_component_type(&components).expect("failed to create composite component type!");

    // Grab everything from the shader-imported modules
    let linked_program = program.link().expect("failed to link slang program!");

    // Convert the linked program into target-compatible bytecode
    let shader_byte_code = linked_program.target_code(0).expect("failed to get target code from linked program");
    match File::create(dst) {
      Ok(mut f) => match f.write_all(shader_byte_code.as_slice()) {
        Err(_) => println!("failed to write bytecode to file!"),
        _ => ()
      },
      _ => println!("failed to create or write to {}!", dst.display())
    }
  }
}