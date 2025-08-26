# Compilation Requirements
 * VulkanSDK 1.4.313.0 (Windows -> installer.exe, Linux -> tarball and add "source path/to/sdk/setup-env.sh" to .bashrc)
 * Vulkan drivers (should be included with GPU manufacturer's drivers)

## Linux
 * Make
 * Clang

### Ubuntu VM
 * lavapipe

### Arch VM
No suitable GPU will be detected for the virtual machine.\
Using software rasterisation instead.
 * mesa
 * vulkan-swrast
