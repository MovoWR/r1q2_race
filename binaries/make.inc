CFLAGS+=-fcommon -fgnu89-inline -DNDEBUG -DLINUX -O2 -fno-strict-aliasing -Wall -ffloat-store -pipe -g -MF .depends/$*.d -MMD
ALLOBJ:=$(ALLSRC:.c=.o)

$(ALLOBJ): | .depends
.depends:
	mkdir -p $@

.PHONY: clean distclean

clean:
	-rm -f $(ALLOBJ) $(TARGETS)

DEPENDS=$(foreach file, $(ALLSRC:.c=.d), .depends/$(file))

distclean: clean
	-rm -f $(DEPENDS)

-include $(DEPENDS)


# Refresh Git identity on every build, retaining the header mtime if unchanged.
PYTHON?=python3
CFLAGS+=-DR1Q2_GENERATED_VERSION -I.depends
.PHONY: race-version-force
.depends/race_version_generated.h: race-version-force
	$(PYTHON) ../../tools/generate_version.py --source ../.. --output $@
$(ALLOBJ): .depends/race_version_generated.h
