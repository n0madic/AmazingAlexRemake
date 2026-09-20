BUILD ?= build
ASSETS ?= $(CURDIR)/build/assets
CONFIG ?= Release

WINDOWS_BUILD ?= build-mingw
LINUX_BUILD ?= build-linux-docker
ANDROID_OUT ?= build/android
WEB_BUILD ?= build-web
WEB_ZIP ?= $(WEB_BUILD)/amazing_alex-web.zip
WEB_ZIP_FILES := index.html amazing_alex.js amazing_alex.wasm amazing_alex.data favicon.png

CMAKE ?= cmake
CTEST ?= ctest
TEST_JOBS ?= 3
GAME := $(BUILD)/app/amazing_alex
ICON_ARG = $(if $(ICON),--icon "$(ICON)",)

.DEFAULT_GOAL := build

.PHONY: help configure build test test-fast run headless viewer macos windows linux android web web-zip

help:
	@echo "Amazing Alex build targets:"
	@echo "  make build       Configure and build the native game (default)"
	@echo "  make test        Build and run the complete CTest suite"
	@echo "  make test-fast   Run the libm self-test and doctest suites"
	@echo "  make run         Run the native game with the imported assets"
	@echo "  make headless    Run the scripted headless game walk"
	@echo "  make viewer      Open the level viewer"
	@echo "  make macos       Build the macOS .app bundle"
	@echo "  make windows     Cross-build the Windows executable with its icon"
	@echo "  make linux       Build and test Linux in Docker"
	@echo "  make android     Build the Android APK"
	@echo "  make web         Build the WebAssembly version"
	@echo "  make web-zip     Build the web version and zip it for deployment"
	@echo
	@echo "Variables: BUILD=$(BUILD), ASSETS=$(ASSETS), CONFIG=$(CONFIG), TEST_JOBS=$(TEST_JOBS), ICON=<override.png>"

configure:
	$(CMAKE) -S . -B "$(BUILD)" -DCMAKE_BUILD_TYPE="$(CONFIG)" -DAA_ASSETS="$(ASSETS)"

build: configure
	$(CMAKE) --build "$(BUILD)" --config "$(CONFIG)" --parallel

test: build
	$(CTEST) --test-dir "$(BUILD)" -C "$(CONFIG)" --output-on-failure --parallel "$(TEST_JOBS)"

test-fast: build
	$(CTEST) --test-dir "$(BUILD)" -C "$(CONFIG)" --output-on-failure -R '^(aa_libm_selftest|aa_tests)$$'

run: build
	"$(GAME)" --assets "$(ASSETS)"

headless: build
	"$(GAME)" --assets "$(ASSETS)" --headless

viewer: build
	"$(GAME)" --viewer --assets "$(ASSETS)"

macos:
	tools/build_macos_app.sh --assets "$(ASSETS)" --build "$(BUILD)" $(ICON_ARG)

windows:
	tools/build_windows_mingw.sh --assets "$(ASSETS)" --build "$(WINDOWS_BUILD)" $(ICON_ARG)

linux:
	tools/build_linux_docker.sh --assets "$(ASSETS)" --build "$(LINUX_BUILD)" $(ICON_ARG)

android:
	tools/build_android.sh --assets "$(ASSETS)" --out "$(ANDROID_OUT)" $(ICON_ARG)

web:
	tools/build_web.sh --assets "$(ASSETS)" --out "$(WEB_BUILD)"

web-zip: web
	rm -f "$(WEB_ZIP)"
	zip -9 -j "$(WEB_ZIP)" $(wildcard $(addprefix $(WEB_BUILD)/app/,$(WEB_ZIP_FILES)))
	@echo "web zip: $(WEB_ZIP)"
