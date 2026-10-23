IOS5_GENERATED_SOURCE := $(abspath $(srcdir))
include $(IOS5_GENERATED_SOURCE)/.ios5-overrides.mk

$(IOS5_GENERATED_SOURCE)/.ios5-overrides.mk: $(IOS5_OVERRIDE_INPUTS) $(IOS5_ORIGINAL_SOURCE)/backends/platform/ios5/compat.patch $(IOS5_ORIGINAL_SOURCE)/backends/platform/ios5/prepare.py
	python3 $(IOS5_ORIGINAL_SOURCE)/backends/platform/ios5/prepare.py --source $(IOS5_ORIGINAL_SOURCE) --output $(IOS5_GENERATED_SOURCE)

IOS5_DIST_FILES = $(DIST_FILES_THEMES) $(DIST_FILES_ENGINEDATA) $(DIST_FILES_ENGINEDATA_BIG) $(DIST_FILES_NETWORKING) $(DIST_FILES_VKEYBD) $(DIST_FILES_SOUNDFONTS) $(DIST_FILES_SHADERS) $(DIST_FILES_DOCS)

ios5bundle: $(EXECUTABLE)
	python3 $(IOS5_ORIGINAL_SOURCE)/dists/ios5/package.py --source $(IOS5_ORIGINAL_SOURCE) --build $(CURDIR) $(IOS5_DIST_FILES)

ios5deb: $(EXECUTABLE)
	python3 $(IOS5_ORIGINAL_SOURCE)/dists/ios5/package.py --source $(IOS5_ORIGINAL_SOURCE) --build $(CURDIR) --deb $(IOS5_DIST_FILES)

.PHONY: ios5bundle ios5deb
