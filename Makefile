# Makefile, based on makefiletutorial.com

TARGET_EXEC := main

BUILD_DIR := bin
SRC_DIR := src
DEPS_DIR := deps
OBJ_DIR := obj
LIB_DIR := lib
MODULES_DIR := modules
ASSETS_DIR := assets
MODEL_PATH := ../$(ASSETS_DIR)/sponza/Sponza.gltf

CC := clang
CXX := clang++

# Grab all the C and C++ files we want to compile from the SRC_DIR
# Single quotes around the * lets the wildcard be passed to the find command, not evaluated inline
# The find command returns all the files in a directory, including the directory as a prefix
SRCS := $(shell find $(SRC_DIR) \( -name '*.cpp' -or -name '*.c' \))

DEPS := $(shell find $(DEPS_DIR) \( -name '*.cpp' -or -name '*.c' \))

# VULKAN_HPP := vulkan_hpp
# VULKAN_HPP_MODULE := $(MODULES_DIR)/$(VULKAN_HPP).pcm
# MODULES := $(VULKAN_HPP)=$(VULKAN_HPP_MODULE)
# MODULES_FLAGS := $(addprefix -fmodule-file=,$(MODULES))

# Grab the pre-compiled unassembled and unlinked object files
OBJS := $(SRCS:%=$(OBJ_DIR)/%.o) $(DEPS:%=$(OBJ_DIR)/%.o)

# Grab all the .d files correlated to .o
MAKES := $(OBJS:.o=.d)

# All the directories that contain files we need access to at compile?linking? time
INC_DIRS := $(shell find $(SRC_DIR) -type d) $(shell find $(DEPS_DIR) -type d) $(VULKAN_SDK)/include include
INC_FLAGS := $(addprefix -I,$(INC_DIRS))

WARNINGS := all extra no-missing-field-initializers
WARNING_FLAGS := $(addprefix -W,$(WARNINGS))

OPTIM_LEVEL := -O0

DEBUG := -g

DEFINES := VULKAN_HPP_NO_STRUCT_CONSTRUCTORS IMGUI_IMPL_VULKAN_USE_VOLK MODEL_PATH=\"$(MODEL_PATH)\"# NDEBUG
D_FLAGS := $(addprefix -D,$(DEFINES))

# C Preprocessor flags
CPP_FLAGS := $(WARNING_FLAGS) $(INC_FLAGS) -MMD -MP $(OPTIM_LEVEL) $(D_FLAGS) $(DEBUG)

CXX_VERSION := -std=c++20
CXX_FLAGS := $(CXX_VERSION)# $(MODULES_FLAGS)

C_VERSION := -std=c17
C_FLAGS := $(C_VERSION)

LD_FLAGS := -L$(LIB_DIR) -lglfw3 -lvolk -lktx_read

# $@ is target name
# $^ is all prerequisites
# $< is the first prerequisite
# $? is all prerequisites newer than target

$(BUILD_DIR)/$(TARGET_EXEC): $(OBJS) shaders
	mkdir -p $(dir $@)
	$(CXX) $(OBJS) -o $@ $(LD_FLAGS)

$(OBJ_DIR)/%.c.o: %.c
	mkdir -p $(dir $@)
	$(CC) $(CPP_FLAGS) $(C_FLAGS) -c $< -o $@

$(OBJ_DIR)/%.cpp.o: %.cpp# $(VULKAN_HPP_MODULE)
	mkdir -p $(dir $@)
	$(CXX) $(CPP_FLAGS) $(CXX_FLAGS) -c $< -o $@

# $(VULKAN_HPP_MODULE): $(VULKAN_SDK)/include/vulkan/vulkan.cppm
# 	mkdir -p $(dir $@)
# 	$(CXX) $(CXX_VERSION) $(WARNING_FLAGS) $(D_FLAGS) -I$(VULKAN_SDK)/include --precompile $< -o $@

ASSETS_DIR := assets
SHADERS_DIR := shaders
SPIRVS_DIR := shaders

SHADERS = $(shell find $(ASSETS_DIR)/$(SHADERS_DIR) \( -name '*.slang' \) -printf '%P\n')
SPIRVS = $(SHADERS:%.slang=$(ASSETS_DIR)/$(SPIRVS_DIR)/%.spv)

$(ASSETS_DIR)/$(SPIRVS_DIR)/%.spv: $(ASSETS_DIR)/$(SHADERS_DIR)/%.slang
	mkdir -p $(dir $@)
	slangc $< -target spirv -profile spirv_1_4 -emit-spirv-directly -fvk-use-entrypoint-name -entry vertMain -entry fragMain -o $@

# KTX_EXEC := ~/Documents/GraphicsProjects/KTX-Software/build/Release/toktx

# SPONZA_DIR := $(ASSETS_DIR)/sponza

# ALBEDO_DIR :=  $(SPONZA_DIR)/albedo
# ALBEDO_JPG := $(shell find $(ALBEDO_DIR) -type f -name '*.jpg')
# ALBEDO_PNG := $(shell find $(ALBEDO_DIR) -type f -name '*.png')
# ALBEDO_KTX := $(ALBEDO_JPG:%.jpg=%.ktx2) $(ALBEDO_PNG:%.png=%.ktx2)

# $(ALBEDO_DIR)/%.ktx2: $(ALBEDO_DIR)/%.jpg
# 	$(KTX_EXEC) --t2 --target_type RGBA --genmipmap $@ $<

# $(ALBEDO_DIR)/%.ktx2: $(ALBEDO_DIR)/%.png
# 	$(KTX_EXEC) --t2 --target_type RGBA --genmipmap $@ $<

# NORMAL_DIR := $(SPONZA_DIR)/normal
# NORMAL_JPG := $(shell find $(NORMAL_DIR) -type f -name '*.jpg')
# NORMAL_KTX := $(NORMAL_JPG:%.jpg=%.ktx2)

# $(NORMAL_DIR)/%.ktx2: $(NORMAL_DIR)/%.jpg
# 	$(KTX_EXEC) --t2 --target_type RGBA --assign_oetf linear --genmipmap --assign_primaries none $@ $<

# METALROUGH_DIR := $(SPONZA_DIR)/metalrough
# METALROUGH_JPG := $(shell find $(METALROUGH_DIR) -type f -name '*.jpg')
# METALROUGH_KTX := $(METALROUGH_JPG:%.jpg=%.ktx2)

# $(METALROUGH_DIR)/%.ktx2: $(METALROUGH_DIR)/%.jpg
# 	$(KTX_EXEC) --t2 --target_type RGBA --assign_oetf linear --genmipmap --assign_primaries none $@ $<

.PHONY: printf shaders textures clean clean_modules clean_albedo clean_normal clean_metalrough clean_textures run

run:
	cd $(BUILD_DIR); ./$(TARGET_EXEC)

shaders: $(SPIRVS)

clean:
	rm -rf $(OBJ_DIR)
	rm -rf $(BUILD_DIR)

clean_shaders:
	rm -r $(ASSETS_DIR)/$(SPIRVS_DIR)/*.spv

printf:

# albedos: $(ALBEDO_KTX)

# normals: $(NORMAL_KTX)

# metalroughs: $(METALROUGH_KTX)

# textures: albedos normals metalroughs

# clean_modules:
# 	rm -rf $(MODULES_DIR)

# clean_albedo:
# 	rm -f $(ALBEDO_KTX)

# clean_normal:
# 	rm -f $(NORMAL_KTX)

# clean_metalrough:
# 	rm -f $(METALROUGH_KTX)

# clean_textures: clean_albedo clean_normal clean_metalrough
	
