BUILD ?= chroma74r
FLAGS += -DBUILD=$(BUILD)

ifeq ($(BUILD),chroma74y)
  BOARD=chroma74
else ifeq ($(BUILD),chroma74r)
  BOARD=chroma74
  FLAGS += -DHW_VARIANT=1
else ifeq ($(BUILD),chroma42_8176)
  BOARD=chroma42
  SCREEN_SRC = screen_8176.c
  FLAGS += -DHW_VARIANT=1
else ifeq ($(BUILD),chroma29_8151)
  BOARD=chroma29
  SCREEN_SRC = screen_8151.c
  FLAGS += -DHW_VARIANT=1
else
  BOARD=$(BUILD)
endif

ifeq ($(SCREEN_SRC),)
  SCREEN_SRC = screen.c
endif

FW_VER := $(shell grep "\#define FW_VERSION" ../common/settings.h | grep -v "suffix" | awk '{print $$3}' | cut -c 3-)

#file containing main() must be first!
SOURCES	= main.c eeprom.c drawing.c comms.c settings.c cpu.c 
SOURCES += board.c powermgt.c syncedproto.c userinterface.c barcode.c
SOURCES += draw_common.c
include ../make/common.mk
SOURCES += $(SOC_DIR)/wdt.c $(SOC_DIR)/timer.c $(SOC_DIR)/sleep.c 
SOURCES += $(SOC_DIR)/adc.c $(SOC_DIR)/radio.c
SOURCES += $(SOC_DIR)/soc.c $(SOC_DIR)/printf.c
SOURCES += board.c $(SCREEN_SRC)

EEPROMDRV ?= eeprom.c
SOURCES += $(EEPROMDRV)
FLAGS += -DPROXY_BUILD
FLAGS += -I. -MD

BUILD_DIR=$(BUILDS_DIR)/$(BUILD)
IMAGE_NAME?=$(BUILD)_full_${FW_VER}
OTA_IMAGE_NAME?=$(BUILD)_ota_${FW_VER}
include $(FIRMWARE_ROOT)/make/rules.mk


