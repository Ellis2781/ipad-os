build:
	cctools-port/package.sh
	make -C xcode_tools
	make -C kernel
