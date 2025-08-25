# Compilation Requirements
 * VulkanSDK 1.4.313.0 (Windows -> installer.exe, Linux -> tarball and add "source path/to/sdk/setup-env.sh" to .bashrc)
 * Vulkan drivers (should be included with GPU manufacturer's drivers)

## Linux
 * glm (libglm-dev on Ubuntu, glm on Arch)
 * Make
 * Clang

### Ubuntu VM
 * libgl-dev

### Arch VM
No suitable GPU will be detected for the virtual machine.\
Using software rasterisation instead.
 * mesa
 * vulkan-swrast
