# Usage instructions:
#   make debug    - build debug version (with debug symbols, optimization level 0)
#   make release  - build release version (optimization level 3)
#   make clean    - remove all built artifacts (including .adsenv copy)
#   make help     - display help message

# Toolchain configuration
CC := gcc

# Project configuration
EXECUTABLE := ads
SRC_DIR := src
BUILD_DIR := build
INCLUDE_DIRS := include /usr/local/include/cjson

# Source file configuration (recursively find all C source files)
SOURCES := $(shell find $(SRC_DIR) -name '*.c')

# Common compilation options
CFLAGS_COMMON := -Wall -Wextra $(addprefix -I,$(INCLUDE_DIRS))
LDFLAGS_COMMON := -L/usr/local/lib -lcurl -lcjson

# Dependency generation config
DEPFLAGS = -MT $@ -MMD -MP -MF $(@:.o=.d)

# Build mode configuration
DEBUG_BUILD_DIR := $(BUILD_DIR)/debug
RELEASE_BUILD_DIR := $(BUILD_DIR)/release

DEBUG_CFLAGS := -g -O0
RELEASE_CFLAGS := -O3

DEBUG_OBJS := $(patsubst $(SRC_DIR)/%.c,$(DEBUG_BUILD_DIR)/%.o,$(SOURCES))
RELEASE_OBJS := $(patsubst $(SRC_DIR)/%.c,$(RELEASE_BUILD_DIR)/%.o,$(SOURCES))

.PHONY: all debug release clean help

# Build the release version by default
all: release

# Debug version build
debug: CFLAGS := $(CFLAGS_COMMON) $(DEBUG_CFLAGS)
debug: BUILD_TARGET := $(DEBUG_BUILD_DIR)/$(EXECUTABLE)
debug: $(DEBUG_BUILD_DIR)/$(EXECUTABLE)

# Release version build
release: CFLAGS := $(CFLAGS_COMMON) $(RELEASE_CFLAGS)
release: BUILD_TARGET := $(RELEASE_BUILD_DIR)/$(EXECUTABLE)
release: $(RELEASE_BUILD_DIR)/$(EXECUTABLE)

# Debug linking rule
$(DEBUG_BUILD_DIR)/$(EXECUTABLE): $(DEBUG_OBJS)
	$(CC) $^ -o $@ $(LDFLAGS_COMMON)
	# Copy environment config file to debug build directory
	cp .adsenv $(DEBUG_BUILD_DIR)

# Release linking rule
$(RELEASE_BUILD_DIR)/$(EXECUTABLE): $(RELEASE_OBJS)
	$(CC) $^ -o $@ $(LDFLAGS_COMMON)
	# Copy environment config file to release build directory
	cp .adsenv $(RELEASE_BUILD_DIR)

# Create build directories for mode (including dependency generation)
$(DEBUG_BUILD_DIR)/%.o: $(SRC_DIR)/%.c | $(DEBUG_BUILD_DIR)
	$(CC) $(CFLAGS) $(DEPFLAGS) -c $< -o $@

$(RELEASE_BUILD_DIR)/%.o: $(SRC_DIR)/%.c | $(RELEASE_BUILD_DIR)
	$(CC) $(CFLAGS) $(DEPFLAGS) -c $< -o $@

# Create build directory
$(DEBUG_BUILD_DIR) $(RELEASE_BUILD_DIR):
	mkdir -p $@

# Clean build artifacts (including .adsenv copy)
clean:
	rm -rf $(BUILD_DIR)

# Help message
help:
	@echo "Available targets:"
	@echo "  debug    - build debug version (with debug symbols, optimization level 0)"
	@echo "  release  - build release version (optimization level 3)"
	@echo "  clean    - remove all built artifacts (including .adsenv copy)"
	@echo "  help     - show this help message"

# Include auto-generated dependency files
-include $(DEBUG_OBJS:.o=.d)
-include $(RELEASE_OBJS:.o=.d)