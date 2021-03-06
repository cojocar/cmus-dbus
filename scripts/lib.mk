#
# Copyright 2005-2006 Timo Hirvonen
#
# This file is licensed under the GPLv2.
#
# cmd macro copied from kbuild (Linux kernel build system)

# Build verbosity:
#   make V=0    silent
#   make V=1    clean (default)
#   make V=2    verbose

# build verbosity (0-2), default is 1
ifneq ($(origin V),command line)
  V := 1
endif
ifneq ($(findstring s,$(MAKEFLAGS)),)
  V := 0
endif

ifeq ($(V),2)
  quiet =
  Q =
else
  ifeq ($(V),1)
    quiet = quiet_
    Q = @
  else
    quiet = silent_
    Q = @
  endif
endif

# simple wrapper around install(1)
#
#  - creates directories automatically
#  - adds $(DESTDIR) to front of files
INSTALL		:= @$(topdir)/scripts/install
INSTALL_LOG	:= $(topdir)/.install.log

dependencies	:= $(wildcard .dep-*)
clean		:= $(dependencies)
distclean	:=

LC_ALL		:= C
LANG		:= C

export INSTALL_LOG LC_ALL LANG GINSTALL

# remove files generated by make
clean:
	rm -f $(clean)

# remove files generated by make and configure
distclean: clean
	rm -f $(distclean)

uninstall:
	@$(topdir)/scripts/uninstall

%.o: %.S
	$(call cmd,as)

# object files for programs and static libs
%.o: %.c
	$(call cmd,cc)

%.o: %.cc
	$(call cmd,cxx)

%.o: %.cpp
	$(call cmd,cxx)

# object files for shared libs
%.lo: %.c
	$(call cmd,cc_lo)

%.lo: %.cc
	$(call cmd,cxx_lo)

%.lo: %.cpp
	$(call cmd,cxx_lo)

# CC for program object files (.o)
quiet_cmd_cc    = CC     $@
      cmd_cc    = $(CC) -c $(CFLAGS) -o $@ $<

# CC for shared library and dynamically loadable module objects (.lo)
quiet_cmd_cc_lo = CC     $@
      cmd_cc_lo = $(CC) -c $(CFLAGS) $(SOFLAGS) -o $@ $<

# LD for programs, optional parameter: libraries
quiet_cmd_ld = LD     $@
      cmd_ld = $(LD) $(LDFLAGS) -o $@ $^ $(1)

# LD for shared libraries, optional parameter: libraries
quiet_cmd_ld_so = LD     $@
      cmd_ld_so = $(LD) $(LDSOFLAGS) $(LDFLAGS) -o $@ $^ $(1)

# LD for dynamically loadable modules, optional parameter: libraries
quiet_cmd_ld_dl = LD     $@
      cmd_ld_dl = $(LD) $(LDDLFLAGS) $(LDFLAGS) -o $@ $^ $(1)

# CXX for program object files (.o)
quiet_cmd_cxx    = CXX    $@
      cmd_cxx    = $(CXX) -c $(CXXFLAGS) -o $@ $<

# CXX for shared library and dynamically loadable module objects (.lo)
quiet_cmd_cxx_lo = CXX    $@
      cmd_cxx_lo = $(CXX) -c $(CXXFLAGS) $(SOFLAGS) -o $@ $<

# CXXLD for programs, optional parameter: libraries
quiet_cmd_cxxld = CXXLD  $@
      cmd_cxxld = $(CXXLD) $(CXXLDFLAGS) -o $@ $^ $(1)

# CXXLD for shared libraries, optional parameter: libraries
quiet_cmd_cxxld_so = CXXLD  $@
      cmd_cxxld_so = $(CXXLD) $(LDSOFLAGS) $(CXXLDFLAGS) -o $@ $^ $(1)

# CXXLD for dynamically loadable modules, optional parameter: libraries
quiet_cmd_cxxld_dl = CXXLD  $@
      cmd_cxxld_dl = $(CXXLD) $(LDDLFLAGS) $(CXXLDFLAGS) -o $@ $^ $(1)

# create archive
quiet_cmd_ar = AR     $@
      cmd_ar = $(AR) $(ARFLAGS) $@ $^

# assembler
quiet_cmd_as = AS     $@
      cmd_as = $(AS) -c $(ASFLAGS) -o $@ $<

cmd = @$(if $($(quiet)cmd_$(1)),echo '   $(call $(quiet)cmd_$(1),$(2))' &&) $(call cmd_$(1),$(2))

ifneq ($(dependencies),)
-include $(dependencies)
endif

.SECONDARY:

.PHONY: clean distclean uninstall
