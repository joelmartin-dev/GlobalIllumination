# Makefile, based on makefiletutorial.com

TARGET_EXEC := main

BUILD_DIR := build
SRC_DIR := src
DEPS_DIR := deps
OBJ_DIR := obj
LIB_DIR := lib

CC := clang
CXX := clang++

# Grab all the C and C++ files we want to compile from the SRC_DIR
# Single quotes around the * lets the wildcard be passed to the find command, not evaluated inline
# The find command returns all the files in a directory, including the directory as a prefix
SRCS := $(shell find $(SRC_DIR) \( -name '*.cpp' -or -name '*.c' \))

DEPS := $(shell find $(DEPS_DIR) \( -name '*.cpp' -or -name '*.c' \))

# Grab the pre-compiled unassembled and unlinked object files
OBJS := $(SRCS:%=$(OBJ_DIR)/%.o) $(DEPS:%=$(OBJ_DIR)/%.o)

# Grab all the .d files correlated to .o
MAKES := $(OBJS:.o=.d)

# All the directories that contain files we need access to at compile?linking? time
INC_DIRS := $(shell find $(SRC_DIR) -type d) $(shell find $(DEPS_DIR) -type d) $(VULKAN_SDK)/include include
INC_FLAGS := $(addprefix -I,$(INC_DIRS))

WARNINGS := all extra
WARNING_FLAGS := $(addprefix -W,$(WARNINGS))

OPTIM_LEVEL := -O0

DEBUG := -g

DEFINES := VULKAN_HPP_NO_STRUCT_CONSTRUCTORS IMGUI_IMPL_VULKAN_USE_VOLK# NDEBUG
D_FLAGS := $(addprefix -D,$(DEFINES))

# C Preprocessor flags
CPP_FLAGS := $(WARNING_FLAGS) $(INC_FLAGS) -MMD -MP $(OPTIM_LEVEL) $(D_FLAGS) $(DEBUG)

MODULES_DIR := pcm.cache
VULKAN_HPP_MODULE := vulkan_hpp
MODULE_FLAG := -fmodule-file=$(VULKAN_HPP_MODULE)=$(MODULES_DIR)/$(VULKAN_HPP_MODULE).pcm

CXX_VERSION := -std=c++20
CXX_FLAGS := $(CXX_VERSION) $(MODULE_FLAG)

C_VERSION := -std=c17
C_FLAGS := $(C_VERSION)

LD_FLAGS := -L$(LIB_DIR) -lglfw3 -lvolk -lglm

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

$(OBJ_DIR)/%.cpp.o: %.cpp $(MODULES_DIR)/$(VULKAN_HPP_MODULE).pcm
	mkdir -p $(dir $@)
	$(CXX) $(CPP_FLAGS) $(CXX_FLAGS) -c $< -o $@

$(MODULES_DIR)/$(VULKAN_HPP_MODULE).pcm: $(VULKAN_SDK)/include/vulkan/vulkan.cppm
	mkdir -p $(dir $@)
	$(CXX) $(CXX_VERSION) $(WARNING_FLAGS) $(D_FLAGS) -I$(VULKAN_SDK)/include --precompile $< -o $@


ASSETS_DIR := assets
SHADERS_DIR := shaders
SPIRVS_DIR := shaders

SHADERS = $(shell find $(ASSETS_DIR)/$(SHADERS_DIR) \( -name '*.slang' \) -printf '%P\n')
SPIRVS = $(SHADERS:%.slang=$(BUILD_DIR)/$(SPIRVS_DIR)/%.spv)


$(BUILD_DIR)/$(SPIRVS_DIR)/%.spv: $(ASSETS_DIR)/$(SHADERS_DIR)/%.slang
	mkdir -p $(dir $@)
	slangc $< -target spirv -profile spirv_1_4 -emit-spirv-directly -fvk-use-entrypoint-name -entry vertMain -entry fragMain -o $@

.PHONY: printf shaders clean run

printf:

shaders: $(SPIRVS)

clean:
	rm -rf $(OBJ_DIR)
	rm -rf $(BUILD_DIR)

clean_vk:
	rm -rf $(MODULES_DIR)

run:
	cd $(BUILD_DIR); ./$(TARGET_EXEC)
