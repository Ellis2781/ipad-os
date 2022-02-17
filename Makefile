default: build

build:
	docker build -t ipad-os .

run:
	docker run -v $(pwd):/workspace:z bash bash 
#/workspace/build.sh

