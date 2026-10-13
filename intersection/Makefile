ARTIFACT = intersection

# Build architecture/variant string:
# x86_64 for the current QNX VM.
PLATFORM ?= x86_64

# Build profile:
# release, debug, profile, coverage
BUILD_PROFILE ?= debug

CONFIG_NAME ?= $(PLATFORM)-$(BUILD_PROFILE)

OUTPUT_DIR = build/$(CONFIG_NAME)
TARGET = $(OUTPUT_DIR)/$(ARTIFACT)

# ============================================================
# Compiler
# ============================================================

CC = qcc -Vgcc_nto$(PLATFORM)
CXX = qcc -lang-c++ -Vgcc_nto$(PLATFORM)
LD = $(CC)

# ============================================================
# Include directories
# ============================================================

# Intersection's own headers
INCLUDES += -IInclude

# Shared protocol/configuration used by I1, I2, X1, Central
INCLUDES += -I../common

# ============================================================
# Compiler flags
# ============================================================

CCFLAGS_release += -O2

CCFLAGS_debug += -g -O0 -fno-builtin

CCFLAGS_coverage += -g -O0 -ftest-coverage -fprofile-arcs
LDFLAGS_coverage += -ftest-coverage -fprofile-arcs

CCFLAGS_profile += -g -O0 -finstrument-functions
LIBS_profile += -lprofilingS

CCFLAGS_all += -Wall -fmessage-length=0
CCFLAGS_all += $(CCFLAGS_$(BUILD_PROFILE))

LDFLAGS_all += $(LDFLAGS_$(BUILD_PROFILE))
LIBS_all += $(LIBS_$(BUILD_PROFILE))

DEPS = -Wp,-MMD,$(@:%.o=%.d),-MT,$@

# ============================================================
# Recursive source discovery
# ============================================================

rwildcard = $(wildcard $(addprefix $1/*.,$2)) \
	$(foreach d,$(wildcard $1/*),$(call rwildcard,$d,$2))

# All C source files under src/
SRCS = $(call rwildcard,src,c)

# Convert:
# src/main.c
# into:
# build/x86_64-debug/src/main.o
OBJS = $(addprefix $(OUTPUT_DIR)/,$(addsuffix .o,$(basename $(SRCS))))

# ============================================================
# Compile
# ============================================================

$(OUTPUT_DIR)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) -c $(DEPS) -o $@ \
		$(INCLUDES) \
		$(CCFLAGS_all) \
		$(CCFLAGS) \
		$<

# ============================================================
# Link
# ============================================================

$(TARGET): $(OBJS)
	$(LD) -o $(TARGET) \
		$(LDFLAGS_all) \
		$(LDFLAGS) \
		$(OBJS) \
		$(LIBS_all) \
		$(LIBS)

# ============================================================
# Targets
# ============================================================

all: $(TARGET)

clean:
	rm -fr $(OUTPUT_DIR)

rebuild: clean all

# Automatically generated header dependencies
-include $(OBJS:%.o=%.d)