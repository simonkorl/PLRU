update:
	make -C build

build: clean
	mkdir build
	cd build && cmake ..
	make -C build

clean:
	rm -rf build bin
