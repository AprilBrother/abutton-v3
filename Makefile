
flash:
	pio run -t upload

flashfs:
	pio run -t uploadfs

monitor:
	pio device monitor -b 115200

erase:
	pio run -t erase

clean:
	pio run -t clean

archive:
	git archive --format=zip --output=build/archive.zip HEAD
