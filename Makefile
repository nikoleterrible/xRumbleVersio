# Project Name
VERSION ?= 2

ifeq ($(VERSION),1)
TARGET = RumbleVersio_v1
CPP_SOURCES = RumbleVersio_v1.cpp
else
TARGET = RumbleVersio
CPP_SOURCES = RumbleVersio.cpp
endif

# Includes
C_INCLUDES += -I. -IDsp

# Optimization flags for Cortex-M7
OPT = -O3 -fno-finite-math-only

# Library Locations (defaults to local or sibling paths, or environment)
LIBDAISY_DIR ?= ./libDaisy
DAISYSP_DIR ?= ./DaisySP

# If not found locally, check parent directory
ifeq ($(wildcard $(LIBDAISY_DIR)/core/Makefile),)
LIBDAISY_DIR = ../../libDaisy
DAISYSP_DIR = ../../DaisySP
endif

# Core location, and generic Makefile.
SYSTEM_FILES_DIR = $(LIBDAISY_DIR)/core
include $(SYSTEM_FILES_DIR)/Makefile
