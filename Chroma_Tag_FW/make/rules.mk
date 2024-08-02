BUILD_DIR?=$(BUILDS_DIR)/build

HDR_TOOL_DIR=$(FIRMWARE_ROOT)/add_ota_hdr
HDR_TOOL_BUILD_DIR=$(HDR_TOOL_DIR)/build
HDR_TOOL=$(HDR_TOOL_BUILD_DIR)/add_ota_hdr

OBJS = $(patsubst %.c,$(BUILD_DIR)/%.$(OBJFILEEXT),$(notdir $(SOURCES)))

$(BUILD_DIR)/%.$(OBJFILEEXT): %.c
	$(CC) -c $< $(FLAGS) -o $(BUILD_DIR)/$(notdir $@)

$(BUILD_DIR)/main.ihx: $(OBJS)
	rm -f $(BUILD_DIR)/$(notdir $@)
	$(CC) $^ $(FLAGS) -o $(BUILD_DIR)/$(notdir $@)

$(BUILD_DIR)/main.elf: $(OBJS)
	$(CC) $(FLAGS) -o $(BUILD_DIR)/$(notdir $@) $^

$(BUILD_DIR):
	if [ ! -e $(BUILD_DIR) ]; then mkdir -p $(BUILD_DIR); fi

$(PREBUILT_DIR):
	mkdir -p $(PREBUILT_DIR)

${HDR_TOOL}: $(wildcard $(HDR_TOOL_DIR)/*) $(FW_COMMON_DIR)/ota_hdr.h
	# Make sure we don't try to use SDCC to build the HDR_TOOL_DIR!
	CC=cc cmake -S $(HDR_TOOL_DIR) -B $(HDR_TOOL_BUILD_DIR)
	cmake --build $(HDR_TOOL_BUILD_DIR)

$(PREBUILT_DIR)/$(IMAGE_NAME).bin: $(BUILD_DIR)/$(IMAGE_NAME).bin
	cp $< $@

$(PREBUILT_DIR)/$(IMAGE_NAME).hex: $(BUILD_DIR)/main.ihx
	cp $< $@

$(PREBUILT_DIR)/$(OTA_IMAGE_NAME).bin: $(PREBUILT_DIR)/$(IMAGE_NAME).bin
	$(HDR_TOOL) $<


$(BUILD_DIR)/$(IMAGE_NAME).bin: $(BUILD_DIR)/main.ihx
	objcopy $< $@ -I ihex -O binary
	@# Display sizes, we're critically short for code and RAM space !
	@grep '^Area' $(BUILD_DIR)/main.map | head -1
	@echo '--------------------------------        ----        ----        ------- ----- ------------'
	@grep = $(BUILD_DIR)/main.map | grep XDATA
	@grep = $(BUILD_DIR)/main.map | grep CODE
	@echo -n ".bin size: "
	@ls -l $(BUILD_DIR)/$(IMAGE_NAME).bin | cut -f5 -d' '

oepl-proto.h:
	git submodule update --init

.PHONY: all clean veryclean flash reset release debug build_info

all: $(BUILD_DIR) oepl-proto.h $(BUILD_DIR)/$(IMAGE_NAME).bin

clean:
	@rm -rf $(BUILD_DIR)

veryclean: 
	@rm -rf $(BUILDS_DIR)
	@rm -rf $(PREBUILT_DIR)

flash:	all 
	cc-tool -e -w $(BUILD_DIR)/$(IMAGE_NAME).bin

reset:	
	cc-tool --reset

release: $(PREBUILT_DIR) ${HDR_TOOL} all $(RELEASE_BINS) 
	(cd $(PREBUILT_DIR);git add $(RELEASE_BINS))

DEPFILES := $(SOURCES:%.c=$(BUILD_DIR)/%.d)

debug:
	@echo "PREBUILT_DIR=$(PREBUILT_DIR)"

build_info:
	@echo "[{\"OTA_BIN\":\"$(OTA_BIN)\",\"FW_VER\":\"${FW_VER}\"}]"

include $(wildcard $(DEPFILES))
