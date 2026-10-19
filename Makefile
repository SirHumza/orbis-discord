.PHONY: all elf fself pkg test clean

all: elf

elf:
	./scripts/build.sh elf

fself:
	./scripts/build.sh fself

pkg:
	./scripts/pkg.sh

test:
	$(MAKE) -C tests clean
	$(MAKE) -C tests test

clean:
	rm -rf build
	$(MAKE) -C tests clean
