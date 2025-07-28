# Makefile, based on makefiletutorial.com

TARGET_EXEC := main

BUILD_DIR := build
SRC_DIR := src
DEPS_DIR := deps
OBJS_DIR := objs
LIBS_DIR := libs

CC := clang
CXX := clang++

# Grab all the C and C++ files we want to compile from the SRC_DIR
# Single quotes around the * lets the wildcard be passed to the find command, not evaluated inline
# The find command returns all the files in a directory, including the directory as a prefix
SRCS := $(shell find $(SRC_DIR) \( -name '*.cpp' -or -name '*.c' \))

# Grab the files in DEPS_DIR without the $(DEPS_DIR) prefix
DEPS := $(shell find $(DEPS_DIR) \( -name '*.cpp' -or -name '*.c' \) -printf '%P\n')

# Grab the pre-compiled unassembled and unlinked object files
OBJS := $(shell find $(OBJS_DIR) \( -name '*.o' -or -name '*.d' \))

# Grab all the .d files correlated to .o
MAKES := $(OBJS:.o=.d)

# All the directories that contain files we need access to at compile?linking? time
INC_DIRS := $(shell find $(SRCS_DIR) -type d) $(shell find $(DEPS_DIR) -type d) include/

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

LD_FLAGS := -lglfw -L$(VULKAN_SDK)/lib -lvulkan -lglm

# $@ is target name
# $^ is all prerequisites
# $< is the first prerequisite
# $? is all prerequisites newer than target

# The final build step assembles and links all objects, default target
# Prerequisites (everything right of :) are either found or built according to existence and recency
# Makefiles use tabs, not spaces
$(BUILD_DIR)/$(TARGET_EXEC): $(OBJS) $(MODULES_DIR)/%.pcm
	$(CXX) $(OBJS) -o $@ $(LD_FLAGS)

# Building C objects
$(OBJS_DIR)/%.c.o: %.c $(DEPS_DIR)/%.c
	mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

# Building C++ objects
$(OBJS_DIR)/%.cpp.o: %.cpp $(DEPS_DIR)/%.cpp
	mkdir -p $(dir $@)
	$(CXX) $(CPP_FLAGS) $(CXX_FLAGS) -c $< -o $@

$(MODULES_DIR)/$(VULKAN_HPP_MODULE).pcm: $(VULKAN_SDK)/include/vulkan/vulkan.cppm
	$(CXX) $(WARNING_FLAGS) $(DEBUG) $(CXX_VERSION) -DVULKAN_HPP_NO_STRUCT_CONSTRUCTORS -I$(VULKAN_SDK)/include --precompile $< -o $@

.PHONY: clean run

clean:
	rm -r $(OBJS_DIR)/*

run:
	$(BUILD_DIR)/$(TARGET_EXEC)
