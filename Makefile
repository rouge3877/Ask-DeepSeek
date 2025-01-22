ARG =

build: src/main.c
	mkdir -p build
	gcc src/main.c -o build/ads -lcurl -lcjson

clean:
	rm -rf build/

# Check if ARG is set before running the test
ifneq ($(ARG),)
test: clean build
	@echo "The argument is: $(ARG)"
	./build/ads $(ARG)
else
test:
	@echo "Error: ARG is not set. Usage: make test ARG=\"<your question>\""
	exit 1
endif

.PHONY: clean test