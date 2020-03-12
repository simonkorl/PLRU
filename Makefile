build: clean
	mkdir build
	cd build && cmake ..
	make -C build

update:
	make -C build

run:
	./bin/CacheSim -i trace

clean:
	rm -rf build bin
