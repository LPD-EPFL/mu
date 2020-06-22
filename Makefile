ifneq ($(V),)
  SILENCE =
else
  SILENCE = @
endif

DEPDIR := .deps

define invoke
	$(eval TARGET := $(patsubst %-mangled,%,$(1)))					\
	export CONAN_DEFAULT_PROFILE_PATH=$(CONAN_PROFILE);				\
	if [ "$(filter $(TARGET),$(EXPORTS))" = $(TARGET) ]; then		\
		cd conan/exports/$(TARGET);									\
		conan export . dory/stable &&								\
		touch ../../../$(DEPDIR)/$(1).conandep;						\
	else															\
		cd $(TARGET);												\
		if [ -f "build.sh" ]; then									\
			./build.sh && touch ../$(DEPDIR)/$(1).conandep;			\
		else														\
			conan create . --build=outdated --test-folder=None	&&	\
			touch ../$(DEPDIR)/$(1).conandep;						\
		fi;															\
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

LIST = $(shell find \
			$(patsubst %-mangled,%,$(basename $(@F)))				\
			$(patsubst %-mangled,conan/exports/%,$(basename $(@F)))	\
				-type f 											\
				-not -path "$(patsubst %-mangled,%,$(basename $(@F)))/build/*" 2> /dev/null)

$(DEPDIR): ; $(SILENCE) mkdir -p $@
clean: ; $(SILENCE) rm -rf $(DEPDIR)
distclean: clean
	$(SILENCE) rm -rf */build
	$(SILENCE) conan remove -f "dory-*"

.SECONDEXPANSION:
$(DEPDIR)/%.conandep: $$(LIST) | $(DEPDIR)
	$(SILENCE) $(call invoke,$(basename $(@F)))

############################ Start of editable area ############################
.PHONY: compiler-options
.PHONY: shared extern memstore crypto ctrl conn
# .PHONY: crash-consensus neb
.PHONY: neb

# Define the targets you want to compile as conan libraries/conan binaries.
#TARGETS := compiler-options extern shared memstore crypto ctrl conn crash-consensus neb
TARGETS := compiler-options extern shared memstore crypto ctrl conn neb
# Define targets which should be exported rather than build or packaged
EXPORTS := compiler-options
# Specify only the local dependencies for the given conan libraries/binaries
#  (that have local dependencies)
memstore : compiler-options shared extern
ctrl : compiler-options shared extern
conn : compiler-options shared ctrl memstore
crypto : compiler-options shared memstore

#crash-consensus: compiler-options extern shared memstore ctrl conn
neb: compiler-options extern shared memstore ctrl conn crypto

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

