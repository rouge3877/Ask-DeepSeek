# Usage instructions:
#   make debug     - build debug version (with debug symbols, optimization level 0)
#   make release   - build release version (optimization level 3)
#   make install   - install the release version to /usr/local/bin and config to /etc/ads
#   make uninstall - remove installed files from the system
#   make clean     - remove all built artifacts (including .adsenv copy)
#   make help      - display help message

# Toolchain configuration
CC := gcc

# Project configuration
EXECUTABLE := ads
SRC_DIR := src
BUILD_DIR := build
INCLUDE_DIRS := include /usr/local/include/cjson

# Installation paths (customizable)
PREFIX ?= /usr/local
BINDIR ?= $(PREFIX)/bin
SYSCONFDIR ?= /etc
CONFIGDIR ?= $(SYSCONFDIR)/ads

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

.PHONY: all debug release install uninstall clean help

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

# Installation target
install: release
	install -d $(DESTDIR)$(BINDIR)
	install -m 755 $(RELEASE_BUILD_DIR)/$(EXECUTABLE) $(DESTDIR)$(BINDIR)/$(EXECUTABLE)
	install -d $(DESTDIR)$(CONFIGDIR)
	@if [ -f .adsenv ]; then \
		install -m 644 .adsenv $(DESTDIR)$(CONFIGDIR)/.adsenv; \
		echo "Installed configuration file to $(DESTDIR)$(CONFIGDIR)/.adsenv"; \
	else \
		printf "\033[1;31m"; \
		echo "Warning: .adsenv configuration file not found!"; \
		echo "This file is required for application configuration."; \
		echo "Please create it with your API keys and environment settings."; \
		echo "Example contents:"; \
		echo "   API_KEY=your_api_key_here"; \
		echo "   LOG_LEVEL=info"; \
		printf "\033[0m"; \
	fi
	@echo "Installed $(EXECUTABLE) to $(DESTDIR)$(BINDIR)"

# Uninstallation target
uninstall:
	@echo "Removing installed files..."
	-@rm -f $(DESTDIR)$(BINDIR)/$(EXECUTABLE)
	-@rm -f $(DESTDIR)$(CONFIGDIR)/.adsenv
	@echo "Successfully uninstalled $(EXECUTABLE)"

# Debug linking rule
$(DEBUG_BUILD_DIR)/$(EXECUTABLE): $(DEBUG_OBJS)
	$(CC) $^ -o $@ $(LDFLAGS_COMMON)
	@if [ -f .adsenv ]; then \
		cp .adsenv $(DEBUG_BUILD_DIR); \
	else \
		printf "\033[1;31m"; \
		echo "Warning: .adsenv configuration file not found in project root!"; \
		echo "This file is required for proper application operation."; \
		echo "Create a .adsenv file with your environment variables."; \
		echo "A sample template can be found in the documentation."; \
		printf "\033[0m"; \
	fi

# Release linking rule
$(RELEASE_BUILD_DIR)/$(EXECUTABLE): $(RELEASE_OBJS)
	$(CC) $^ -o $@ $(LDFLAGS_COMMON)
	@if [ -f .adsenv ]; then \
		cp .adsenv $(RELEASE_BUILD_DIR); \
	else \
		printf "\033[1;31m"; \
		echo "Warning: .adsenv configuration file not found in project root!"; \
		echo "This file is required for production environment setup."; \
		echo "Create a .adsenv file with your deployment settings before release."; \
		printf "\033[0m"; \
	fi

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
	@echo "  debug     - build debug version (with debug symbols, optimization level 0)"
	@echo "  release   - build release version (optimization level 3)"
	@echo "  install   - install the release version to \$$(BINDIR) (default: $(PREFIX)/bin)"
	@echo "              and config to \$$(CONFIGDIR) (default: $(SYSCONFDIR)/ads)"
	@echo "  uninstall - remove installed files from the system"
	@echo "  clean     - remove all built artifacts (including .adsenv copy)"
	@echo "  help      - show this help message"
	@echo ""
	@echo "Customization:"
	@echo "  PREFIX     - Base installation directory (default: $(PREFIX))"
	@echo "  SYSCONFDIR - System configuration directory (default: $(SYSCONFDIR))"
	@echo "  DESTDIR    - Staging directory for package builders"
	@echo ""
	@printf "\033[1;31mImportant:\033[0m\n"
	@echo "  The .adsenv file is required for configuration. If missing:"
	@echo "  1. Create a .adsenv file in the project root"
	@echo "  2. Add your environment variables (API keys, settings, etc)"
	@echo "  3. Rerun the build or installation command"

# Include auto-generated dependency files
-include $(DEBUG_OBJS:.o=.d)
-include $(RELEASE_OBJS:.o=.d)