# Makefile, based on makefiletutorial.com

TARGET_EXEC := main

BUILD_DIR := build
SRC_DIR := src
OBJ_DIR := obj
LIB_DIR := lib

CC := clang
CXX := clang++

# Grab all the C and C++ files we want to compile from the SRC_DIR
# Single quotes around the * lets the wildcard be passed to the find command, not evaluated inline
# The find command returns all the files in a directory, including the directory as a prefix
SRCS := $(shell find $(SRC_DIR) \( -name '*.cpp' -or -name '*.c' \))

# Grab the pre-compiled unassembled and unlinked object files
OBJS := $(SRCS:%=$(OBJ_DIR)/%.o)

# Grab all the .d files correlated to .o
MAKES := $(OBJS:.o=.d)

# All the directories that contain files we need access to at compile?linking? time
INC_DIRS := $(shell find $(SRC_DIR) -type d) include/
INC_FLAGS := $(addprefix -I,$(INC_DIRS))

WARNINGS := all error extra
WARNING_FLAGS := $(addprefix -W,$(WARNINGS))

OPTIM_LEVEL := -O0

DEBUG := -g

# C Preprocessor flags
CPP_FLAGS := $(WARNING_FLAGS) $(INC_FLAGS) -MMD -MP $(OPTIM_LEVEL) $(DEBUG)

MODULES_DIR := pcm.cache
VULKAN_HPP_MODULE := vulkan_hpp
MODULE_FLAG := -fmodule-file=$(VULKAN_HPP_MODULE)=$(MODULES_DIR)/$(VULKAN_HPP_MODULE).pcm

CXX_VERSION := -std=c++20
CXX_FLAGS := $(CXX_VERSION) $(MODULE_FLAG)

C_VERSION := -std=c17
C_FLAGS := $(C_VERSION)

LD_FLAGS := -L$(LIB_DIR) -lglfw3 -L$(VULKAN_SDK)/lib -lvulkan -lglm

# $@ is target name
# $^ is all prerequisites
# $< is the first prerequisite
# $? is all prerequisites newer than target

$(BUILD_DIR)/$(TARGET_EXEC): $(OBJS) $(MODULES_DIR)/$(VULKAN_HPP_MODULE).pcm
	mkdir -p $(dir $@)
	$(CXX) $(OBJS) -o $@ $(LD_FLAGS)

$(OBJ_DIR)/%.c.o: %.c
	mkdir -p $(dir $@)
	$(CC) $(CPP_FLAGS) $(C_FLAGS) -c $< -o $@

$(OBJ_DIR)/%.cpp.o: %.cpp
	mkdir -p $(dir $@)
	$(CXX) $(CPP_FLAGS) $(CXX_FLAGS) -c $< -o $@

$(MODULES_DIR)/$(VULKAN_HPP_MODULE).pcm: $(VULKAN_SDK)/include/vulkan/vulkan.cppm
	$(CXX) $(CXX_VERSION) $(WARNING_FLAGS) -DVULKAN_HPP_NO_STRUCT_CONSTRUCTORS -I$(VULKAN_SDK)/include --precompile $< -o $@
.PHONY: clean run

clean:
	rm -r $(OBJ_DIR)/*

run:
	$(BUILD_DIR)/$(TARGET_EXEC)
