PLUGIN := splicemanipulator
BUILD_DIR := build
CONFIG := Release

# SET CMAKE Generators, config for Windows vs Linux
ifeq ($(OS),Windows_NT)
    CMAKE_GENERATOR := Visual Studio 18 2026
    CMAKE_ARCH := x64

    PLUGIN_DIR := $(CURDIR)/$(BUILD_DIR)/$(CONFIG)
    PATHSEP := ;

    CMAKE_CONFIGURE_ARGS := \
        -G "$(CMAKE_GENERATOR)" \
        -A $(CMAKE_ARCH)

    CMAKE_BUILD_ARGS := \
        --config $(CONFIG)

    PYTHON := python

else

    PLUGIN_DIR := $(CURDIR)/$(BUILD_DIR)
    PATHSEP := :

    CMAKE_CONFIGURE_ARGS := \
        $(if $(CMAKE_GENERATOR),-G "$(CMAKE_GENERATOR)") \
        -DCMAKE_BUILD_TYPE=$(CONFIG)

    CMAKE_BUILD_ARGS :=

    PYTHON := python3

endif

export PLUGIN
export PLUGIN_DIR

.PHONY: build run tests clean install uninstall

build:
	cmake -B $(BUILD_DIR) $(CMAKE_CONFIGURE_ARGS)
	cmake --build $(BUILD_DIR) $(CMAKE_BUILD_ARGS)

run:
ifeq ($(OS),Windows_NT)
	set TSPLUGINS_PATH=$(PLUGIN_DIR);%TSPLUGINS_PATH% && \
	tsp \
	--add-input-stuffing 1/5 \
	-I file tests\data\synthetic.ts \
	-P pmt --service 1 --add-programinfo-id 0x43554549 --add-pid 96/0x86 \
	-P inject --pid 96 --inter-packet 1 --repeat 1 tests\data\tests.bin \
	-P ${PLUGIN} --splice-pid 96 --rules tests\data\rules\rules.json \
	-P splicemonitor --splice-pid 96 --all-commands --display-commands --meta-sections --json-line \
	-O drop		
else
	TSPLUGINS_PATH="$(PLUGIN_DIR):$$TSPLUGINS_PATH" \
	tsp -v \
	--add-input-stuffing 1/5 \
	-I file tests/data/synthetic.ts \
	-P pmt --service 1 --add-programinfo-id 0x43554549 --add-pid 96/0x86 \
	-P inject --pid 96 --inter-packet 1 --repeat 1 tests/data/tests.bin \
	-P ${PLUGIN} --splice-pid 96 --rules tests/data/rules/rules.json \
	-P splicemonitor --splice-pid 96 --all-commands --display-commands --meta-sections --json-line \
	-O drop
endif

clean:
ifeq ($(OS),Windows_NT)
	if exist $(BUILD_DIR) rmdir /s /q $(BUILD_DIR)
else
	rm -rf $(BUILD_DIR)
endif

tests:
	$(PYTHON) -m pytest -s tests -v

install:
	make build
	cmake --install build

uninstall:
	cmake --build $(BUILD_DIR) --target uninstall $(CMAKE_BUILD_ARGS)