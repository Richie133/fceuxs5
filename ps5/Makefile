# fceuxs5
#   make ps5    -> build/ps5/fceuxs5.elf   the payload to send with the PS5 Payload Manager / ELF loader:
#                  it installs or updates the dashboard app /data/homebrew/$(TITLE_ID)/ (FCEUX itself,
#                  icon included) and stays running as the helper that lets the app reach /data
#   make app    -> build/app/$(TITLE_ID)/     the app folder on its own (eboot.bin, sce_module/, sce_sys/)
#   make host   -> build/host/                Linux builds of the same code (PS5 calls faked by host/sce_host.cpp)
#   make dist   -> build/dist/fceuxs5-v$(VERSION).elf + fceuxs5-v$(VERSION)-$(TITLE_ID).zip (the app's own
#                  updates) + fceuxs5-v$(VERSION)-src.zip (release names)
#   make send   -> sends fceuxs5.elf to $(PS5_HOST):$(PS5_PORT) with the SDK's prospero-deploy
#
# The layout is PS5SX2's (and Snes9x PS5's, Mesen2 PS5's): eboot.bin is the emulator, a native PS5 title (the
# console gives the controller to the title in front only), and the payload is its installer and helper:
#   1. build/ps5/fceuxs5-helper.elf   the helper alone (payload); the app sends it to the ELF loader
#   2. build/app/$(TITLE_ID)/                   eboot.bin = FCEUX's core (through its libretro port,
#                                               ../src/drivers/libretro/libretro.c) + the PS5 frontend + the helper from
#                                               step 1 (native title, signed); sce_module/libc.prx; sce_sys/
#   3. build/ps5/fceuxs5.elf          installer + helper, with the app folder from step 2 built in
#
# FCEUX's core (../src) is compiled as it is, with the flags of its own libretro build (Makefile.common,
# Makefile.libretro's unix platform): every one of its sources builds for the PS5 unchanged.
#
# Dependencies: the ps5-payload-dev SDK (v0.42 or newer, https://github.com/ps5-payload-dev/sdk) in
# $(PS5_PAYLOAD_SDK), with clang/lld 18 on the host.

PS5_PAYLOAD_SDK ?= /opt/ps5-payload-sdk
PS5_HOST ?= ps5
PS5_PORT ?= 9021
VERSION ?= 1.0
# the FCEUX this port is built from (TASEmulators/fceux master, 0f7c216, November 2026)
CORE_VERSION := 2.8.0
TITLE_ID ?= PPSA51855
# where the app looks for its updates (Settings, SYSTEM, Updates): this repository's GitHub releases
UPDATE_REPO ?= Richie133/fceuxs5
APP_OUT := build/app/$(TITLE_ID)

ROOT := ..

FC := src
# FCEUX's core: the sources of its own CMake build (src/CMakeLists.txt SRC_CORE, without Lua and the debugger UI), the
# board files by wildcard, and the scalers of drivers/common (hq2x/hq3x/Scale2x/Scale3x) plus Blargg's NTSC filter.
CORE_SRC := $(sort $(patsubst $(ROOT)/%,%,$(wildcard $(ROOT)/$(FC)/boards/*.cpp))) \
	$(addprefix $(FC)/,asm.cpp cart.cpp cheat.cpp conddebug.cpp config.cpp debug.cpp debugsymboltable.cpp drawing.cpp \
		fceu.cpp fds.cpp file.cpp emufile.cpp filter.cpp ines.cpp input.cpp ld65dbg.cpp \
		movie.cpp netplay.cpp nsf.cpp oldmovie.cpp palette.cpp profiler.cpp ppu.cpp sound.cpp \
		state.cpp unif.cpp video.cpp vsuni.cpp boardctl.cpp wave.cpp x6502.cpp input/arkanoid.cpp \
		input/bworld.cpp input/cursor.cpp input/fkb.cpp input/fns.cpp input/ftrainer.cpp input/hypershot.cpp input/mahjong.cpp input/mouse.cpp \
		input/oekakids.cpp input/pec586kb.cpp input/powerpad.cpp input/quiz.cpp input/shadow.cpp input/snesmouse.cpp input/suborkb.cpp input/toprider.cpp \
		input/virtualboy.cpp input/lcdcompzapper.cpp input/zapper.cpp utils/xstring.cpp utils/crc32.cpp utils/endian.cpp utils/general.cpp utils/guid.cpp \
		utils/md5.cpp utils/memory.cpp utils/mutex.cpp utils/timeStamp.cpp drivers/common/hq2x.cpp drivers/common/hq3x.cpp drivers/common/scale2x.cpp drivers/common/scale3x.cpp \
		drivers/common/scalebit.cpp drivers/common/vidblit.cpp drivers/common/os_utils.cpp) \
	ps5/fceux/fceux_driver.cpp
CORE_C_SRC := $(FC)/drivers/common/nes_ntsc.c $(FC)/boards/emu2413.c $(sort $(patsubst $(ROOT)/%,%,$(wildcard $(ROOT)/ps5/third_party/zlib/*.c)))
CORE_DEFS := -DPSS_STYLE=1 -DHAVE_ASPRINTF -DFCEUXS5=1 -DPUBLIC_RELEASE=1 -DHAVE_UNISTD_H -DPATH_MAX=1024
CORE_FORCE := -include $(ROOT)/ps5/fceux/fceux_ps5.h

# netfs: network shares as folders (SMB and NFS), from Beetle PCE PS5 1.1.1 (Genesis Plus GX PS5 2.1.1's, Snes9x PS5's:
# thatboialex's design; netfs/netfs_backend.h). backend_smb.cpp / backend_nfs.cpp are the protocols; the stubs (which
# fail with "not built in") stand in for them in the netfs unit test.
NETFS_CORE_SRC := netfs/netfs_share.cpp netfs/netfs_core.cpp netfs/netfs_cache.cpp netfs/netfs_vfs.cpp netfs/netfs_wrap.cpp netfs/netfs_edit.cpp netfs/netfs_shims_ps5.cpp
# the libc calls netfs answers for /smb/... and /nfs/... paths (netfs/netfs_wrap.cpp has a __wrap_ function for each).
# They go into both links as --wrap, so a call anywhere in the app (the frontend's game reads, minizip, the 7z reader,
# FCEUX's libretro-common file streams, libc++'s file streams) lands in netfs first and is passed on to the real call
# when the path or handle is not netfs's. NES games are read whole when they start; nothing streams while one runs.
NETFS_WRAPPED := fopen fclose fread fseek fseeko ftell ftello rewind feof ferror clearerr \
	fileno fgetc getc ungetc fgets fflush setvbuf flockfile funlockfile open close read pread lseek fstat stat lstat \
	access realpath opendir readdir closedir
# glibc only: the console's libc has no 64-bit variants (off_t is 64 bits), and minizip maps its fopen64 calls to
# fopen on FreeBSD targets (third_party/ioapi.h), so a --wrap of these would leave __real_* undefined in the console link.
NETFS_WRAPPED_LFS64 := fopen64 fseeko64 ftello64 stat64 lstat64 fstat64 readdir64 open64 pread64 lseek64
# console only (netfs/netfs_shims_ps5.cpp): sockets made non-blocking (fcntl, then SO_NBIO), and a getaddrinfo that works
# in a native title
NETFS_WRAPPED_PS5 := fcntl getaddrinfo freeaddrinfo
NETFS_WRAP_LLD := $(foreach s,$(NETFS_WRAPPED) $(NETFS_WRAPPED_PS5),--wrap=$(s))
NETFS_WRAP_GCC := $(foreach s,$(NETFS_WRAPPED) $(NETFS_WRAPPED_LFS64),-Wl,--wrap=$(s))
NETFS_SRC := $(NETFS_CORE_SRC) netfs/backend_smb.cpp netfs/backend_nfs.cpp
# libsmb2 (57fea04) and libnfs (2d368ad), LGPL-2.1-or-later, vendored with their notices as Snes9x PS5, Genesis Plus GX
# PS5 and Beetle PCE PS5 have them (third_party/libsmb2/README.snes9xps5, third_party/libnfs/PATCHES.md). libsmb2 is
# built from a config.h of its folder (no CMake). It needs no PS4_PLATFORM: prospero-clang defines __FreeBSD__, which
# selects libsmb2's FreeBSD headers. Their fcntl and getaddrinfo calls reach the console shims through the --wrap list.
LIBSMB2 := third_party/libsmb2
LIBNFS := third_party/libnfs
LIBSMB2_SRC := $(sort $(wildcard $(LIBSMB2)/lib/*.c))
LIBNFS_SRC := $(sort $(foreach d,lib mount nfs nfs4 portmap nlm nsm rquota,$(wildcard $(LIBNFS)/$(d)/*.c)))
LIBSMB2_INC := -I$(LIBSMB2)/include -I$(LIBSMB2)/include/smb2 -I$(LIBSMB2)/lib
LIBSMB2_FLAGS := -I$(LIBSMB2) $(LIBSMB2_INC) -DHAVE_CONFIG_H -D_FILE_OFFSET_BITS=64 '-D_U_=__attribute__((unused))'
LIBNFS_FLAGS := -DHAVE_CONFIG_H -I$(LIBNFS) -I$(LIBNFS)/include -I$(LIBNFS)/include/nfsc \
	$(addprefix -I$(LIBNFS)/,mount nfs nfs4 nlm nsm portmap rquota)
# netfs/backend_smb.cpp includes libsmb2's headers as system headers (they are not ours to keep -Werror clean)
%/port/netfs/backend_smb.o: EXTRA_FLAGS = $(patsubst -I%,-isystem %,$(LIBSMB2_INC))
%/port/netfs/backend_nfs.o: EXTRA_FLAGS = $(LIBNFS_FLAGS)

# the app (eboot.bin): the PS5 layer and the frontend
EMU_SRC := coreorbis/main-boot.cpp \
	coreorbis/orbis-shims/orbis_paths.cpp coreorbis/orbis-shims/ProsperoNotify.cpp \
	coreorbis/orbis-shims/ProsperoVideo.cpp coreorbis/orbis-shims/ProsperoAudio.cpp \
	coreorbis/orbis-shims/ProsperoInput.cpp coreorbis/orbis-shims/ProsperoJailbreak.cpp \
	coreorbis/orbis-shims/ProsperoCrash.cpp coreorbis/orbis-shims/ProsperoCrt.cpp coreorbis/orbis-shims/ProsperoUpdate.cpp \
	frontend/fe_emu.cpp frontend/fe_menu.cpp frontend/fe_settings.cpp frontend/fe_i18n.cpp frontend/fe_text.cpp \
	frontend/fe_games.cpp frontend/fe_http.cpp frontend/fe_covers.cpp frontend/fe_shelf.cpp \
	frontend/fe_prefetch.cpp frontend/fe_coverfetch.cpp frontend/fe_coverworker.cpp frontend/fe_nesdb.cpp frontend/fe_patch.cpp \
	frontend/fe_media.cpp frontend/fe_draw.cpp frontend/fe_theme.cpp frontend/fe_gameopts.cpp \
	frontend/fe_netscreen.cpp frontend/fe_osk.cpp frontend/fe_ui.cpp $(NETFS_SRC)
# 1.1: games inside a .7z (fe_7z.cpp), with the LZMA SDK 26.05's C 7z decoder (public domain; third_party/lzma7z/README.md):
# no threads, no AES (an encrypted archive is refused), no assembler. FCEUX links no other LZMA copy, but every external
# name of this one is prefixed fce7z_ all the same (fe7z_names.h, force-included), so another can't meet it, and
# lzma7z/names.ok stops the build if an object defines a name without it.
EMU_SRC += frontend/fe_7z.cpp
LZMA7Z := third_party/lzma7z
LZMA7Z_SRC := $(sort $(wildcard $(LZMA7Z)/*.c))
LZMA7Z_DEFS := -DZ7_ST -DZ7_PPMD_SUPPORT -DZ7_EXTRACT_ONLY
LZMA7Z_FLAGS := $(LZMA7Z_DEFS) -include $(LZMA7Z)/fe7z_names.h -I$(LZMA7Z) -w
%/port/frontend/fe_7z.o: EXTRA_FLAGS = $(LZMA7Z_DEFS) -isystem $(LZMA7Z)
%/port/frontend/fe_7z.o: $(LZMA7Z)/fe7z_names.h
# every global name the decoder's objects define must carry the prefix (a newer SDK may add one: put it in fe7z_names.h)
define LZMA7Z_NAMES_CHECK
	@bad=$$($(1) -g --defined-only $(2) | awk 'NF==3 && $$3 !~ /^(fce7z_|__odr_asan\.fce7z_)/ {print $$3}'); \
	if [ -n "$$bad" ]; then echo "lzma7z: names without the fce7z_ prefix: $$bad (add them to $(LZMA7Z)/fe7z_names.h)"; exit 1; fi
	@touch $@
endef
# minizip (zlib licence) for games inside a .zip, built as C with the core's zlib
EMU_C_SRC := frontend/third_party/unzip.c frontend/third_party/ioapi.c
# the payloads: the helper, and the installer (= helper + install)
# (the helper also downloads the covers in the background: fe_coverworker.cpp, with the app's HTTPS code)
HELPER_SRC := coreorbis/orbis-shims/orbis_paths.cpp coreorbis/orbis-shims/ProsperoNotify.cpp \
	coreorbis/orbis-shims/ProsperoHelper.cpp coreorbis/orbis-shims/ProsperoCrash.cpp \
	coreorbis/orbis-shims/ProsperoUpdate.cpp coreorbis/orbis-shims/ProsperoAppPlace.cpp \
	frontend/fe_http.cpp frontend/fe_coverfetch.cpp frontend/fe_coverworker.cpp frontend/fe_tlshttp.cpp
# the helper's own HTTPS (fe_tlshttp.cpp): Mbed TLS 3.6 (Apache-2.0), with fe_mbedtls_config.h on its default settings
MBEDTLS := frontend/third_party/mbedtls
MBEDTLS_SRC := $(sort $(wildcard $(MBEDTLS)/library/*.c))
MBEDTLS_FLAGS := -I$(MBEDTLS)/include -Ifrontend '-DMBEDTLS_USER_CONFIG_FILE="fe_mbedtls_config.h"'
INSTALLER_SRC := installer/installer_main.cpp $(HELPER_SRC) coreorbis/orbis-shims/ProsperoInstall.cpp

CORE_INCS := -I$(ROOT)/$(FC) -I$(ROOT)/$(FC)/drivers/common -I$(ROOT)/ps5/third_party/zlib -I$(ROOT)/ps5/frontend/third_party
INCS := -Inetfs -Icoreorbis/include-orbis -Icoreorbis/orbis-shims -Ifrontend -isystem $(ROOT)/ps5/third_party/zlib
DEFS := -DFCEUXS5_VERSION='"$(VERSION)"' -DFCEUXS5_CORE_VERSION='"$(CORE_VERSION)"' -DFCEUXS5_TITLE_ID='"$(TITLE_ID)"' \
	-DFCEUXS5_UPDATE_REPO='"$(UPDATE_REPO)"'
# 1.2 (FBNeo PS5 2.2's build/defs.stamp): what DEFS carries, so a new VERSION / TITLE_ID / UPDATE_REPO / core version
# rebuilds every object built with them; make alone doesn't see a changed flag, and an object left from the last build
# kept the old version in the eboot and the payloads (the version the app tells the helper, the installer's
# notification). The file is written while the Makefile is read, only when its text differs ...
DEFS_STAMP := build/defs.stamp
DEFS_TEXT := $(VERSION) $(TITLE_ID) $(UPDATE_REPO) $(CORE_VERSION)
ifneq ($(shell cat $(DEFS_STAMP) 2>/dev/null),$(DEFS_TEXT))
$(shell mkdir -p build && printf '%s\n' '$(DEFS_TEXT)' >$(DEFS_STAMP))
endif
# ... and made again when something removed it after the Makefile was read (make clean ps5: clean deletes build/)
$(DEFS_STAMP):
	@mkdir -p $(dir $@) && printf '%s\n' '$(DEFS_TEXT)' >$@
MINIZIP_FLAGS := -DNOUNCRYPT -I$(ROOT)/ps5/third_party/zlib -w
PORT_WARN := -Wall -Wextra -Werror -Wno-unused-parameter -Wno-missing-field-initializers

# the game-name table built into fe_games.o (tools/make_gamedb.py), the header corrections and the four-player games
# into fe_nesdb.o (tools/make_nesdb.py, tools/make_nes4p.py)
GAMEDB_DEFS := -DGAMEDB_TSV='"$(abspath frontend/data/nointro.tsv)"'
%/port/frontend/fe_games.o: EXTRA_FLAGS = $(GAMEDB_DEFS)
%/port/frontend/fe_games.o: frontend/data/nointro.tsv
%/port/frontend/fe_nesdb.o: EXTRA_FLAGS = -DNESDB_TSV='"$(abspath frontend/data/nesdb.tsv)"' \
	-DNES4P_TSV='"$(abspath frontend/data/nes4p.tsv)"'
%/port/frontend/fe_nesdb.o: frontend/data/nesdb.tsv frontend/data/nes4p.tsv
# the FCEUX glue: FCEUX's headers (as system headers: their warnings are not ours to keep -Werror clean)
%/port/frontend/fe_emu.o: EXTRA_FLAGS = -Ifceux -isystem $(ROOT)/src -isystem $(ROOT)/src/drivers/common $(CORE_DEFS)
%/port/frontend/fe_patch.o: EXTRA_FLAGS = -isystem $(ROOT)/ps5/third_party/zlib
# the intro (MPEG-1, pl_mpeg) and the menu music (MP3, dr_mp3) built into fe_media.o; the decoders' warnings are theirs
%/port/frontend/fe_media.o: EXTRA_FLAGS = -DFE_MEDIA_DIR='"$(abspath frontend/assets/media)"' -Wno-error -Wno-sign-compare \
	-Wno-unused-function -Wno-implicit-fallthrough -Wno-unused-parameter -Wno-unused-variable -Wno-unused-but-set-variable \
	-Wno-missing-field-initializers
build/ps5/port/frontend/fe_media.o build/host/port/frontend/fe_media.o: frontend/assets/media/intro.mpg frontend/assets/media/music.mp3
# stb_image in fe_covers.o has warnings of its own
%/port/frontend/fe_covers.o: EXTRA_FLAGS = -Wno-error -Wno-sign-compare -Wno-unused-but-set-variable -Wno-implicit-fallthrough
# the UI fonts (PS5SX2's: PCSX2's Roboto Regular and PromptFont; Font Awesome's brands) built into fe_text.o
%/port/frontend/fe_text.o: EXTRA_FLAGS = -DFE_FONT_DIR='"$(abspath frontend/assets/fonts)"' -Wno-error -Wno-sign-compare -Wno-unused-function
build/ps5/port/frontend/fe_text.o build/host/port/frontend/fe_text.o: frontend/assets/fonts/Roboto-Regular.ttf frontend/assets/fonts/promptfont.otf \
	frontend/assets/fonts/fa-brands-400.otf frontend/assets/fonts/VarelaRound-Regular.ttf frontend/assets/fonts/Bungee-Regular.ttf
# the helper's HTTPS client: Mbed TLS's headers, and Mozilla's CA list built in
%/port/frontend/fe_tlshttp.o: EXTRA_FLAGS = $(MBEDTLS_FLAGS) -DCACERT_PEM='"$(abspath frontend/data/cacert.pem)"'
%/port/frontend/fe_tlshttp.o: frontend/data/cacert.pem frontend/fe_mbedtls_config.h
# the helper payload is installer_main.cpp without the install step
%/port/installer/helper_main.o: EXTRA_FLAGS = -DFCEUXS5_HELPER_ONLY

# ---------------- PS5 ----------------
PS5_CC  := $(PS5_PAYLOAD_SDK)/bin/prospero-clang
PS5_CXX := $(PS5_PAYLOAD_SDK)/bin/prospero-clang++
PS5_OUT := build/ps5
# The PS5's CPU is Zen 2 (PS5SX2 builds with the same -march).
PS5_OPT := -O3 -fPIC -march=znver2 -fno-strict-aliasing
# the payloads: notifications, and the kernel access the helper needs (libkernel_sys: sceKernelGetAppInfo)
# the payload SDK's network libraries (its http2_get sample's), for the helper's cover downloads
PS5_PAYLOAD_LIBS := -lkernel_sys -lSceNet -lSceNetCtl -lSceSsl -lSceHttp2 -lpthread

PS5_CORE_OBJ := $(patsubst %.cpp,$(PS5_OUT)/core/%.o,$(CORE_SRC)) $(patsubst %.c,$(PS5_OUT)/core/%.o,$(CORE_C_SRC))
PS5_EMU_OBJ := $(patsubst %.cpp,$(PS5_OUT)/port/%.o,$(EMU_SRC)) $(patsubst %.c,$(PS5_OUT)/port/%.o,$(EMU_C_SRC))
PS5_MBEDTLS_OBJ := $(patsubst $(MBEDTLS)/library/%.c,$(PS5_OUT)/mbedtls/%.o,$(MBEDTLS_SRC))
PS5_HELPER_OBJ := $(PS5_OUT)/port/installer/helper_main.o $(patsubst %.cpp,$(PS5_OUT)/port/%.o,$(HELPER_SRC)) $(PS5_MBEDTLS_OBJ)
PS5_INSTALLER_OBJ := $(patsubst %.cpp,$(PS5_OUT)/port/%.o,$(INSTALLER_SRC)) $(PS5_MBEDTLS_OBJ)
PS5_LIBSMB2_OBJ := $(patsubst $(LIBSMB2)/%.c,$(PS5_OUT)/libsmb2/%.o,$(LIBSMB2_SRC))
PS5_LIBNFS_OBJ := $(patsubst $(LIBNFS)/%.c,$(PS5_OUT)/libnfs/%.o,$(LIBNFS_SRC))
PS5_LZMA7Z_OBJ := $(patsubst $(LZMA7Z)/%.c,$(PS5_OUT)/lzma7z/%.o,$(LZMA7Z_SRC))

ps5: $(PS5_OUT)/fceuxs5.elf
# the core alone
ps5-core: $(PS5_CORE_OBJ)

# the core's own flags (Makefile.libretro), its warnings left to it
$(PS5_OUT)/core/%.o: $(ROOT)/%.c
	@mkdir -p $(dir $@)
	$(PS5_CC) $(PS5_OPT) $(CORE_DEFS) $(CORE_INCS) -w -c -o $@ $<

$(PS5_OUT)/mbedtls/%.o: $(MBEDTLS)/library/%.c frontend/fe_mbedtls_config.h
	@mkdir -p $(dir $@)
	$(PS5_CC) -Os -fPIC -march=znver2 $(MBEDTLS_FLAGS) -c -o $@ $<

$(PS5_OUT)/port/%.o: %.c
	@mkdir -p $(dir $@)
	$(PS5_CC) $(PS5_OPT) $(MINIZIP_FLAGS) -c -o $@ $<

$(PS5_OUT)/core/%.o: $(ROOT)/%.cpp fceux/fceux_ps5.h
	@mkdir -p $(dir $@)
	$(PS5_CXX) -std=gnu++17 $(PS5_OPT) $(CORE_DEFS) $(CORE_FORCE) $(CORE_INCS) -Ifceux -Icoreorbis/include-orbis -Icoreorbis/orbis-shims -Ifrontend -w -c -o $@ $<

$(PS5_OUT)/port/%.o: %.cpp coreorbis/include-orbis/*.h coreorbis/orbis-shims/*.h frontend/*.h netfs/*.h $(DEFS_STAMP)
	@mkdir -p $(dir $@)
	$(PS5_CXX) -std=c++17 $(PS5_OPT) $(DEFS) $(INCS) $(PORT_WARN) $(EXTRA_FLAGS) -c -o $@ $<

$(PS5_OUT)/libsmb2/%.o: $(LIBSMB2)/%.c
	@mkdir -p $(dir $@)
	$(PS5_CC) -O2 -fPIC -march=znver2 $(LIBSMB2_FLAGS) -w -c -o $@ $<

$(PS5_OUT)/libnfs/%.o: $(LIBNFS)/%.c
	@mkdir -p $(dir $@)
	$(PS5_CC) -O2 -fPIC -march=znver2 $(LIBNFS_FLAGS) -w -c -o $@ $<

$(PS5_OUT)/lzma7z/%.o: $(LZMA7Z)/%.c $(wildcard $(LZMA7Z)/*.h)
	@mkdir -p $(dir $@)
	$(PS5_CC) $(PS5_OPT) $(LZMA7Z_FLAGS) -c -o $@ $<
$(PS5_OUT)/lzma7z/names.ok: $(PS5_LZMA7Z_OBJ)
	$(call LZMA7Z_NAMES_CHECK,$(PS5_PAYLOAD_SDK)/bin/llvm-nm,$^)

$(PS5_OUT)/port/installer/helper_main.o: installer/installer_main.cpp coreorbis/include-orbis/*.h coreorbis/orbis-shims/*.h $(DEFS_STAMP)
	@mkdir -p $(dir $@)
	$(PS5_CXX) -std=c++17 $(PS5_OPT) $(DEFS) $(INCS) $(PORT_WARN) $(EXTRA_FLAGS) -c -o $@ $<

# 1. the helper payload (built into the app, which sends it to the ELF loader when no helper answers)
$(PS5_OUT)/fceuxs5-helper.elf: $(PS5_HELPER_OBJ)
	$(PS5_CXX) -o $@ $^ $(PS5_PAYLOAD_LIBS)

$(PS5_OUT)/helper_data.o: coreorbis/orbis-shims/helper_data.cpp coreorbis/orbis-shims/ProsperoJailbreak.h $(PS5_OUT)/fceuxs5-helper.elf $(DEFS_STAMP)
	$(PS5_CXX) -std=c++17 $(PS5_OPT) $(DEFS) $(INCS) $(PORT_WARN) -DHELPER_ELF='"$(abspath $(PS5_OUT)/fceuxs5-helper.elf)"' -c -o $@ $<

# 3. the installer + helper, with the app folder built in
APP_FILES = $(APP_OUT)/eboot.bin $(APP_OUT)/sce_sys/param.json $(APP_OUT)/sce_sys/icon0.png $(APP_OUT)/sce_sys/pic0.dds \
	$(APP_OUT)/sce_sys/pic1.dds \
	$(APP_OUT)/sce_module/libc.prx
APP_DATA_DEFS = -DAPP_EBOOT='"$(abspath $(APP_OUT)/eboot.bin)"' -DAPP_PARAM='"$(abspath $(APP_OUT)/sce_sys/param.json)"' \
	-DAPP_ICON0='"$(abspath $(APP_OUT)/sce_sys/icon0.png)"' -DAPP_PIC0='"$(abspath $(APP_OUT)/sce_sys/pic0.dds)"' \
	-DAPP_PIC1='"$(abspath $(APP_OUT)/sce_sys/pic1.dds)"' \
	-DAPP_LIBC='"$(abspath $(APP_OUT)/sce_module/libc.prx)"' \
	$(if $(shell cmp -s app/sce_sys/pic0.dds app/sce_sys/pic1.dds && echo same),-DAPP_PIC1_IS_PIC0)

$(PS5_OUT)/install_data.o: coreorbis/orbis-shims/install_data.cpp coreorbis/orbis-shims/ProsperoInstall.h $(APP_FILES) $(DEFS_STAMP)
	$(PS5_CXX) -std=c++17 $(PS5_OPT) $(DEFS) $(INCS) $(PORT_WARN) $(APP_DATA_DEFS) -c -o $@ $<

$(PS5_OUT)/fceuxs5.elf: $(PS5_INSTALLER_OBJ) $(PS5_OUT)/install_data.o
	$(PS5_CXX) -o $@ $^ $(PS5_PAYLOAD_LIBS)
	@echo "built $@ ($$(stat -c %s $@) bytes)"

$(PS5_OUT)/fceuxs5-stripped.elf: $(PS5_OUT)/fceuxs5.elf
	$(PS5_PAYLOAD_SDK)/bin/prospero-strip -o $@ $<

strip: $(PS5_OUT)/fceuxs5-stripped.elf

# the release files, named with the version: build/dist/fceuxs5-v$(VERSION).elf and the sources zip
DIST := build/dist
# + the app folder as a stored zip with its manifest (fceuxs5-v$(VERSION)-$(TITLE_ID).zip): attach it to the GitHub
# release tagged v$(VERSION), so the app can update itself (Settings, SYSTEM, Updates)
dist: $(PS5_OUT)/fceuxs5.elf $(APP_FILES)
	@mkdir -p $(DIST)
	cp $< $(DIST)/fceuxs5-v$(VERSION).elf
	python3 tools/make_release_zip.py $(APP_OUT) $(TITLE_ID) $(DIST)/fceuxs5-v$(VERSION)-$(TITLE_ID).zip
	cd ../.. && rm -f $(abspath $(DIST))/fceuxs5-v$(VERSION)-src.zip && \
		zip -qr $(abspath $(DIST))/fceuxs5-v$(VERSION)-src.zip $(notdir $(abspath ..)) \
		-x '*/ps5/build/*' '*/proto/native/build/*' '*.o' '*/.git/*'
	@ls -l $(DIST)

send: $(PS5_OUT)/fceuxs5.elf
	$(PS5_PAYLOAD_SDK)/bin/prospero-deploy -h $(PS5_HOST) -p $(PS5_PORT) $<

# ---------------- the app (eboot.bin = FCEUX) ----------------
# 2. A native PS5 title linked the way PS5SX2 links its eboot (ps5/coreorbis/link-vk.sh): the boilerplate's
# app_crt.cpp (_start -> _init_env -> main) and app_cpp_runtime.cpp, the emulator's objects (the same ones,
# compiled once), a PIE laid out by ps5-pie.ld (+ PS5SX2's ehframe.ld) against the SDK's system-module stubs
# and its static libc++/libc++abi/libunwind/libc, every defined symbol local (PS5SX2's genmap.py result:
# localize.map); then ps5-native-tool link (SCE dynamic tables, process parameters) and self --sign.
# The folder carries sce_module/libc.prx, the C runtime module a native title needs (without it the console
# stops the app with PRX_SCE_MODULE_LOAD_ERROR, 0xa0020102).
NATIVE := proto/native
NATIVE_TOOL := $(NATIVE)/build/host/ps5-native-tool
LIBC_BUILDER := $(NATIVE)/build/host/libc-builder
LLD ?= ld.lld
NATIVE_CLANG = PS5_PAYLOAD_SDK=$(PS5_PAYLOAD_SDK) sh $(NATIVE)/tooling/prospero-clang18
SDK_LIB := $(PS5_PAYLOAD_SDK)/target/lib
# every system-module stub but the two PS5SX2 leaves out (link-vk.sh), and not libkernel_sys / libkernel_web either
# (1.2, Snes9x PS5 2.10's and FBNeo PS5 2.2's): a title can't load them (Media Player Classic PS5 1.5 picked
# libkernel_sys up through statfs and crashed at launch); with them left out such a symbol is a link error.
# tools/check_imports.py checks the result.
APP_STUBS = $(filter-out %/libScePosixForWebKit.so %/libSceGLSlimVSH.so %/libkernel_sys.so %/libkernel_web.so,\
	$(sort $(wildcard $(SDK_LIB)/*.so)))

$(NATIVE_TOOL): $(NATIVE)/tooling/native/native_app_builder.cpp $(NATIVE)/tooling/native/elf_object.cpp \
		$(NATIVE)/tooling/native/self_container.cpp $(NATIVE)/tooling/native/sce_module_writer.cpp
	@mkdir -p $(dir $@)
	$(HOST_CXX_NATIVE) -std=c++20 -O2 -o $@ $^ -lz
HOST_CXX_NATIVE ?= clang++

$(LIBC_BUILDER): $(NATIVE)/tooling/native/libc_builder.cpp
	@mkdir -p $(dir $@)
	$(HOST_CXX_NATIVE) -std=c++20 -O2 -Wall -Wextra -Werror -o $@ $<

# sce_module/libc.prx, deterministic: it must match the published digest (proto/native/libc.prx.sha256)
build/app/libc.prx: $(LIBC_BUILDER) $(NATIVE_TOOL) $(wildcard $(NATIVE)/tooling/native/runtime/*.txt)
	@mkdir -p $(dir $@)
	$(LIBC_BUILDER) $(NATIVE)/tooling/native/runtime/api-surface.txt $(NATIVE)/tooling/native/runtime/imports.txt build/app/libc.raw.elf
	$(NATIVE_TOOL) self --sign --in build/app/libc.raw.elf --out $@
	cd build/app && sha256sum --check --strict $(abspath $(NATIVE)/libc.prx.sha256)

$(APP_OUT)/sce_module/libc.prx: build/app/libc.prx
	@mkdir -p $(dir $@)
	cp $< $@

app: $(APP_FILES)

# app_cpp_runtime.cpp (operator new / delete) with exceptions (1.2, as FBNeo PS5 2.2 and Snes9x PS5 2.10): a failed
# allocation throws std::bad_alloc, as the C++ standard says, instead of stopping the app with SIGILL; the eboot's C++
# runtime is whole (below), so the throw reaches a catch, or std::terminate, which the crash log reports
APP_CRT_EH_app_crt := -fno-exceptions
APP_CRT_EH_app_cpp_runtime := -fexceptions
build/app/app_crt.o build/app/app_cpp_runtime.o: build/app/%.o: $(NATIVE)/tooling/native/%.cpp
	@mkdir -p $(dir $@)
	$(NATIVE_CLANG) -std=c++20 -O2 -Wall -Wextra $(APP_CRT_EH_$*) -fno-rtti -ffunction-sections -fdata-sections \
		-c -o $@ $<

EBOOT_OBJS = build/app/app_crt.o build/app/app_cpp_runtime.o $(PS5_CORE_OBJ) $(PS5_EMU_OBJ) $(PS5_LIBSMB2_OBJ) $(PS5_LIBNFS_OBJ) \
	$(PS5_LZMA7Z_OBJ) $(PS5_OUT)/helper_data.o
# 1.2 ("lesson 2" of Snes9x PS5 2.10 and FBNeo PS5 2.2): the C++ runtime is the eboot's own, whole: libc++, libc++abi
# (__cxa_throw, the personality routine, the type_info vtables) and libunwind (which finds the FDEs through ehframe.ld's
# __eh_frame_hdr_start), named before the system stubs. Up to 1.1 they came after them, and libSceLibcInternal's stub won
# the personality routine, __cxa_get_globals and the type_info / std exception vtables while __cxa_throw and the
# unwinder were static: the mix Stella PS5 1.0 crashed with at its first throw. tools/check_imports.py refuses any C++
# runtime import, any module outside the allow-list and any symbol only libkernel_sys / libkernel_web has.
# tools/check_syscalls.py (FBNeo PS5 2.2's final review): the link traces (-y) every function of the SDK's
# libc.a(syscalls.o), the payload SDK's raw system calls; the PIE may call none of them.
build/app/fceuxs5-pie.elf: $(EBOOT_OBJS) $(NATIVE)/tooling/native/ps5-pie.ld $(NATIVE)/tooling/native/ehframe.ld \
		$(NATIVE)/tooling/native/app-symbols.map $(NATIVE)/tooling/native/localize.map $(PS5_OUT)/lzma7z/names.ok \
		tools/check_imports.py tools/check_syscalls.py
	python3 tools/check_syscalls.py rsp $(SDK_LIB)/libc.a build/app/syscalls-y.rsp
	$(LLD) -m elf_x86_64 -pie -z max-page-size=0x4000 --hash-style=gnu \
		-T $(NATIVE)/tooling/native/ps5-pie.ld -T $(NATIVE)/tooling/native/ehframe.ld --eh-frame-hdr \
		--version-script $(NATIVE)/tooling/native/app-symbols.map --version-script $(NATIVE)/tooling/native/localize.map \
		-e _start --no-dynamic-linker $(NETFS_WRAP_LLD) -o $@ $(EBOOT_OBJS) \
		$(SDK_LIB)/libc++.a $(SDK_LIB)/libc++abi.a $(SDK_LIB)/libunwind.a \
		--as-needed $(APP_STUBS) -L $(SDK_LIB) $(SDK_LIB)/libc.a -lpthread @build/app/syscalls-y.rsp \
		--trace >build/app/fceuxs5-pie.trace
	python3 tools/check_imports.py $@ $(SDK_LIB) || { rm -f $@; exit 1; }
	python3 tools/check_syscalls.py check build/app/fceuxs5-pie.trace || { rm -f $@; exit 1; }

build/app/eboot.elf: build/app/fceuxs5-pie.elf $(NATIVE_TOOL)
	$(NATIVE_TOOL) link --in $< --out $@ --stub-dir $(SDK_LIB) --module-sdk 0x02000009 \
		--companion-sdk 0x08050001 --file-name eboot.elf
	python3 tools/prepare_console_elf.py --input $@ --output build/app/eboot-console-ready.elf

$(APP_OUT)/eboot.bin: build/app/eboot.elf $(NATIVE_TOOL)
	@mkdir -p $(dir $@)
	$(NATIVE_TOOL) self --sign --in $< --out $@ --magic 0x1D3D154F
	$(NATIVE_TOOL) self --inspect --file $@

$(APP_OUT)/sce_sys/param.json: app/sce_sys/param.json
	@mkdir -p $(dir $@)
	sed 's/PPSA51855/$(TITLE_ID)/g' $< > $@

$(APP_OUT)/sce_sys/%.png: app/sce_sys/%.png
	@mkdir -p $(dir $@)
	cp $< $@

# pic0.dds (the home screen's background while the icon is selected) and pic1.dds (the launch background):
# 3840x2160 BC7_UNORM DX10 DDS without mipmaps, made from app/sce_sys/background-source.png with the
# ps5-native-app-boilerplate's tools/prepare-assets.sh --background (pinned bc7enc_rdo).
$(APP_OUT)/sce_sys/%.dds: app/sce_sys/%.dds
	@mkdir -p $(dir $@)
	cp $< $@

# ---------------- host ----------------
HOST_CC ?= gcc
HOST_CXX ?= g++
HOST_OUT := build/host
HOST_SAN ?= -fsanitize=address,undefined -fno-omit-frame-pointer
# The core under AddressSanitizer only (UBSan would report its deliberate shifts and unaligned reads).
HOST_CORE_SAN ?= -fsanitize=address -fno-omit-frame-pointer
# no _FORTIFY_SOURCE: it turns fread/fgets/read into __fread_chk/..., which --wrap (netfs) does not see
HOST_OPT := -O1 -g -mavx2 -fno-strict-aliasing -U_FORTIFY_SOURCE
HOST_CORE_OPT := -O2 -g1 -mavx2 -U_FORTIFY_SOURCE

HOST_CORE_OBJ := $(patsubst %.cpp,$(HOST_OUT)/core/%.o,$(CORE_SRC)) $(patsubst %.c,$(HOST_OUT)/core/%.o,$(CORE_C_SRC))
HOST_EMU_OBJ := $(patsubst %.cpp,$(HOST_OUT)/port/%.o,$(EMU_SRC) host/sce_host.cpp host/netfs_dirbackend.cpp) $(patsubst %.c,$(HOST_OUT)/port/%.o,$(EMU_C_SRC))
HOST_MBEDTLS_OBJ := $(patsubst $(MBEDTLS)/library/%.c,$(HOST_OUT)/mbedtls/%.o,$(MBEDTLS_SRC))
HOST_HELPER_OBJ := $(HOST_OUT)/port/installer/helper_main.o $(patsubst %.cpp,$(HOST_OUT)/port/%.o,$(HELPER_SRC) host/sce_host.cpp) \
	$(HOST_MBEDTLS_OBJ)
HOST_INSTALLER_OBJ := $(patsubst %.cpp,$(HOST_OUT)/port/%.o,$(INSTALLER_SRC) host/sce_host.cpp) $(HOST_MBEDTLS_OBJ)
HOST_LIBSMB2_OBJ := $(patsubst $(LIBSMB2)/%.c,$(HOST_OUT)/libsmb2/%.o,$(LIBSMB2_SRC))
HOST_LIBNFS_OBJ := $(patsubst $(LIBNFS)/%.c,$(HOST_OUT)/libnfs/%.o,$(LIBNFS_SRC))
HOST_LZMA7Z_OBJ := $(patsubst $(LZMA7Z)/%.c,$(HOST_OUT)/lzma7z/%.o,$(LZMA7Z_SRC))

host-core: $(HOST_CORE_OBJ)
host: $(HOST_OUT)/fceuxs5-host $(HOST_OUT)/fceuxs5-installer $(HOST_OUT)/fceuxs5-helper

$(HOST_OUT)/core/%.o: $(ROOT)/%.c
	@mkdir -p $(dir $@)
	$(HOST_CC) -fPIC $(HOST_CORE_OPT) $(HOST_CORE_SAN) $(CORE_DEFS) $(CORE_INCS) -w -c -o $@ $<

$(HOST_OUT)/mbedtls/%.o: $(MBEDTLS)/library/%.c frontend/fe_mbedtls_config.h
	@mkdir -p $(dir $@)
	$(HOST_CC) -O2 -g1 $(MBEDTLS_FLAGS) -c -o $@ $<

$(HOST_OUT)/port/%.o: %.c
	@mkdir -p $(dir $@)
	$(HOST_CC) $(HOST_OPT) $(MINIZIP_FLAGS) -c -o $@ $<

$(HOST_OUT)/core/%.o: $(ROOT)/%.cpp fceux/fceux_ps5.h
	@mkdir -p $(dir $@)
	$(HOST_CXX) -std=gnu++17 -fPIC $(HOST_CORE_OPT) $(HOST_CORE_SAN) $(CORE_DEFS) $(CORE_FORCE) $(CORE_INCS) -Ifceux -Icoreorbis/include-orbis -Icoreorbis/orbis-shims -Ifrontend -w -c -o $@ $<

$(HOST_OUT)/port/%.o: %.cpp coreorbis/include-orbis/*.h coreorbis/orbis-shims/*.h frontend/*.h netfs/*.h $(DEFS_STAMP)
	@mkdir -p $(dir $@)
	$(HOST_CXX) -std=c++17 $(HOST_OPT) $(HOST_SAN) $(DEFS) $(INCS) $(PORT_WARN) $(EXTRA_FLAGS) -c -o $@ $<

$(HOST_OUT)/libsmb2/%.o: $(LIBSMB2)/%.c
	@mkdir -p $(dir $@)
	$(HOST_CC) -O2 -g1 -D_GNU_SOURCE $(LIBSMB2_FLAGS) -w -c -o $@ $<

$(HOST_OUT)/libnfs/%.o: $(LIBNFS)/%.c
	@mkdir -p $(dir $@)
	$(HOST_CC) -O2 -g1 $(LIBNFS_FLAGS) -w -c -o $@ $<

# the 7z decoder under AddressSanitizer too (it reads archives from anywhere; UBSan would report its unaligned reads)
$(HOST_OUT)/lzma7z/%.o: $(LZMA7Z)/%.c $(wildcard $(LZMA7Z)/*.h)
	@mkdir -p $(dir $@)
	$(HOST_CC) -fPIC $(HOST_OPT) $(HOST_CORE_SAN) $(LZMA7Z_FLAGS) -c -o $@ $<
$(HOST_OUT)/lzma7z/names.ok: $(HOST_LZMA7Z_OBJ)
	$(call LZMA7Z_NAMES_CHECK,nm,$^)

$(HOST_OUT)/port/installer/helper_main.o: installer/installer_main.cpp coreorbis/include-orbis/*.h coreorbis/orbis-shims/*.h $(DEFS_STAMP)
	@mkdir -p $(dir $@)
	$(HOST_CXX) -std=c++17 $(HOST_OPT) $(HOST_SAN) $(DEFS) $(INCS) $(PORT_WARN) $(EXTRA_FLAGS) -c -o $@ $<

# The host installer carries a stand-in app folder (host/fake-eboot.bin, host/fake-libc.prx + the real
# param/icon/pic); the host app carries the host helper, which it hands to a fake ELF loader in the tests.
HOST_APP_DEFS = -DAPP_EBOOT='"$(abspath host/fake-eboot.bin)"' -DAPP_PARAM='"$(abspath app/sce_sys/param.json)"' \
	-DAPP_ICON0='"$(abspath app/sce_sys/icon0.png)"' -DAPP_PIC0='"$(abspath app/sce_sys/pic0.dds)"' \
	-DAPP_PIC1='"$(abspath app/sce_sys/pic1.dds)"' \
	-DAPP_LIBC='"$(abspath host/fake-libc.prx)"'

$(HOST_OUT)/install_data.o: coreorbis/orbis-shims/install_data.cpp coreorbis/orbis-shims/ProsperoInstall.h \
		host/fake-eboot.bin host/fake-libc.prx app/sce_sys/param.json app/sce_sys/icon0.png app/sce_sys/pic0.dds app/sce_sys/pic1.dds \
		$(DEFS_STAMP)
	@mkdir -p $(dir $@)
	$(HOST_CXX) -std=c++17 $(HOST_OPT) $(DEFS) $(INCS) $(PORT_WARN) $(HOST_APP_DEFS) -c -o $@ $<

$(HOST_OUT)/helper_data.o: coreorbis/orbis-shims/helper_data.cpp coreorbis/orbis-shims/ProsperoJailbreak.h $(HOST_OUT)/fceuxs5-helper $(DEFS_STAMP)
	@mkdir -p $(dir $@)
	$(HOST_CXX) -std=c++17 $(HOST_OPT) $(DEFS) $(INCS) $(PORT_WARN) -DHELPER_ELF='"$(abspath $(HOST_OUT)/fceuxs5-helper)"' -c -o $@ $<

$(HOST_OUT)/fceuxs5-helper: $(HOST_HELPER_OBJ)
	$(HOST_CXX) $(HOST_SAN) -o $@ $^ -lpthread

$(HOST_OUT)/fceuxs5-installer: $(HOST_INSTALLER_OBJ) $(HOST_OUT)/install_data.o
	$(HOST_CXX) $(HOST_SAN) -o $@ $^ -lpthread

$(HOST_OUT)/fceuxs5-host: $(HOST_CORE_OBJ) $(HOST_EMU_OBJ) $(HOST_LIBSMB2_OBJ) $(HOST_LIBNFS_OBJ) $(HOST_LZMA7Z_OBJ) \
		$(HOST_OUT)/helper_data.o | $(HOST_OUT)/lzma7z/names.ok
	$(HOST_CXX) $(HOST_SAN) -o $@ $^ $(NETFS_WRAP_GCC) -lpthread -lm

clean:
	rm -rf build

.PHONY: ps5 ps5-core host-core host send strip app dist clean
.SECONDARY:
