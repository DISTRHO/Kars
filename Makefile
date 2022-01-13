#!/usr/bin/make -f
# Makefile for DISTRHO Plugins #
# ---------------------------- #
# Created by falkTX
#

include dpf/Makefile.base.mk

all: plugins gen

# --------------------------------------------------------------

plugins:
	$(MAKE) all -C plugins/Kars

ifneq ($(CROSS_COMPILING),true)
gen: plugins dpf/utils/lv2_ttl_generator
	@$(CURDIR)/dpf/utils/generate-ttl.sh
ifeq ($(MACOS),true)
	@$(CURDIR)/dpf/utils/generate-vst-bundles.sh
endif

dpf/utils/lv2_ttl_generator:
	$(MAKE) -C dpf/utils/lv2-ttl-generator
else
gen:
endif

# --------------------------------------------------------------

# NOTE: note path must be absolute
MOD_WORKDIR ?= $(HOME)/mod-workdir
MOD_ENVIRONMENT = AR=${1}/host/usr/bin/${2}-gcc-ar CC=${1}/host/usr/bin/${2}-gcc CPP=${1}/host/usr/bin/${2}-cpp CXX=${1}/host/usr/bin/${2}-g++ LD=${1}/host/usr/bin/${2}-ld PKG_CONFIG=${1}/host/usr/bin/pkg-config STRIP=${1}/host/usr/bin/${2}-strip CFLAGS="-I${1}/staging/usr/include" CPPFLAGS= CXXFLAGS="-I${1}/staging/usr/include" LDFLAGS="-L${1}/staging/usr/lib" \ EXE_WRAPPER="qemu-${3}-static -L ${1}/target"

modduo:
	$(MAKE) $(call MOD_ENVIRONMENT,$(MOD_WORKDIR)/modduo,arm-mod-linux-gnueabihf,arm)

modduox:
	$(MAKE) $(call MOD_ENVIRONMENT,$(MOD_WORKDIR)/modduox,aarch64-mod-linux-gnueabi,aarch64)

moddwarf:
	$(MAKE) $(call MOD_ENVIRONMENT,$(MOD_WORKDIR)/moddwarf,aarch64-mod-linux-gnu,aarch64)

publish:
	tar -C bin -cz $(subst bin/,,$(wildcard bin/*.lv2)) | base64 | curl -F 'package=@-' http://192.168.51.1/sdk/install && echo

ifneq (,$(findstring modduo-,$(MAKECMDGOALS)))
$(MAKECMDGOALS):
	$(MAKE) $(call MOD_ENVIRONMENT,$(MOD_WORKDIR)/modduo,arm-mod-linux-gnueabihf,arm) $(subst modduo-,,$(MAKECMDGOALS))
endif

ifneq (,$(findstring modduox-,$(MAKECMDGOALS)))
$(MAKECMDGOALS):
	$(MAKE) $(call MOD_ENVIRONMENT,$(MOD_WORKDIR)/modduox,aarch64-mod-linux-gnueabi,aarch64) $(subst modduox-,,$(MAKECMDGOALS))
endif

ifneq (,$(findstring moddwarf-,$(MAKECMDGOALS)))
$(MAKECMDGOALS):
	$(MAKE) $(call MOD_ENVIRONMENT,$(MOD_WORKDIR)/moddwarf,aarch64-mod-linux-gnu,aarch64) $(subst moddwarf-,,$(MAKECMDGOALS))
endif

# --------------------------------------------------------------

clean:
	$(MAKE) clean -C dpf/utils/lv2-ttl-generator
	$(MAKE) clean -C plugins/Kars
	rm -rf bin build

# --------------------------------------------------------------

.PHONY: plugins
