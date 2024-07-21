FIRMWARE_ROOT:=$(abspath $(dir $(lastword $(MAKEFILE_LIST)))/..)

FW_COMMON_DIR=$(FIRMWARE_ROOT)/common
BOARD_DIR=$(FIRMWARE_ROOT)/board/$(BOARD)
SOC_DIR=$(FIRMWARE_ROOT)/soc/$(SOC)
CPU_DIR=$(FIRMWARE_ROOT)/cpu/$(CPU)
BUILDS_DIR=$(FIRMWARE_ROOT)/builds
PREBUILT_DIR=$(FIRMWARE_ROOT)/../../OpenEPaperLink/binaries/Tag
RELEASE_BINS = $(PREBUILT_DIR)/$(IMAGE_NAME).bin
RELEASE_BINS += $(PREBUILT_DIR)/$(IMAGE_NAME).hex
RELEASE_BINS += $(PREBUILT_DIR)/$(OTA_IMAGE_NAME).bin
SHARED_DIR=$(FIRMWARE_ROOT)/../shared
VPATH = $(COMMON_DIR) $(FW_COMMON_DIR) $(BOARD_DIR) $(SOC_DIR) $(CPU_DIR) $(BUILD_DIR) 

FLAGS += -I$(BOARD_DIR)
FLAGS += -I$(SOC_DIR)
FLAGS += -I$(CPU_DIR)
FLAGS += -I$(FW_COMMON_DIR)
FLAGS += -I.
FLAGS += -I$(SHARED_DIR)
VPATH += $(SHARED_DIR)

all:	#make sure it is the first target

include $(BOARD_DIR)/make.mk
include $(SOC_DIR)/make.mk
include $(CPU_DIR)/make.mk

