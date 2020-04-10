ifneq ($(V),)
  SILENCE =
else
  SILENCE = @
endif

DEPDIR := .deps

define invoke
	export CONAN_DEFAULT_PROFILE_PATH=$(CONAN_PROFILE);		\
	cd $(patsubst %-mangled,%,$(1));						\
	if [ -f "build.sh" ]; then								\
		./build.sh && touch ../$(DEPDIR)/$(1).conandep;		\
	else													\
		conan create . --build=outdated &&					\
		touch ../$(DEPDIR)/$(1).conandep;					\
	fi
endef

define check_trigger
	trigger=0;												\
	for dep in $(2); do 									\
		if [ "$(DEPDIR)/$$dep-mangled.conandep"				\
			-nt $(DEPDIR)/$(1)-mangled.conandep ]; then		\
			trigger=1;										\
			break;											\
		fi;													\
	done;													\
	if [ "$$trigger" -ne 0 ]; then							\
		rm $(DEPDIR)/$(1)-mangled.conandep;					\
		$(call invoke,$(1)-mangled)							\
	fi
endef

.PHONY: clean

LIST = $(shell find $(patsubst %-mangled,%,$(basename $(@F)))	\
			-type f 											\
			-not -path "$(patsubst %-mangled,%,$(basename $(@F)))/build/*")

$(DEPDIR): ; $(SILENCE) mkdir -p $@
clean: ; $(SILENCE) rm -rf $(DEPDIR)
distclean: clean
	$(SILENCE) rm -rf */build
	$(SILENCE) conan remove -f "dory-*"

.SECONDEXPANSION:
$(DEPDIR)/%.conandep: $$(LIST) | $(DEPDIR)
	$(SILENCE) $(call invoke,$(basename $(@F)))

############################ Start of editable area ############################
.PHONY: shared extern memstore crypto ctrl conn
.PHONY: crash-consensus neb

# Define the targets you want to compile as conan libraries/conan binaries.
TARGETS := extern shared memstore crypto ctrl conn crash-consensus neb

# Specify only the local dependencies for the given conan libraries/binaries
#  (that have local dependencies)
memstore : shared extern
ctrl : shared extern
conn : shared ctrl memstore
crypto : shared memstore

crash-consensus: extern shared memstore ctrl conn
neb: extern shared memstore ctrl conn

############################# End of editable area #############################
.PHONY: all
all : $(TARGETS)
.DEFAULT_GOAL := all

# Use the intermediate mangled target to gather all the rule prerequisites
# together
$(TARGETS) : % : %-mangled

$(TARGETS):
	$(SILENCE) $(call check_trigger,$@,$(filter-out $@-mangled,$^))

MANGLED_TARGETS := $(patsubst %,%-mangled,$(TARGETS))
$(MANGLED_TARGETS) : % : $(DEPDIR)/%.conandep

.PHONY: list
list:
	@$(MAKE) -pRrq -f $(lastword $(MAKEFILE_LIST)) : 2>/dev/null | awk -v RS= -F: '/^# File/,/^# Finished Make data base/ {if ($$1 !~ "^[#.]") {print $$1}}' | sort | egrep -v -e '^[^[:alnum:]]' -e '^$@$$'

