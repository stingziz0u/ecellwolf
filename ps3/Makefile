#----------------------------------------------------------------------------
# ECellWolf on the PS3 (ECWolf + PSL1GHT), standard toolchain, no SDL2
#
# From the root of the tree:
#
#   make -C ps3                  -> ps3/ecellwolf-1.1.pkg
#   make -C ps3 DEBUG_BUILD=1    -> ps3/ecellwolf-debug-1.1.pkg
#   make -C ps3 clean
#
# ECWolf talks to SDL2, SDL2_mixer and SDL2_net. Instead of porting SDL,
# ps3/compat/ declares only what ECWolf uses of them and ps3/source/
# implements it on the PS3 (as Quake2PS3 and ROTT-PS3 do): video on the
# RSX (ps3_gpu.c), the pad as SDL game controller events, the mixer on
# libaudio. SDL_net is not there: no multiplayer.
#
# Packaging (.elf -> .self -> .pkg) is ppu_rules' own pattern rules.
#----------------------------------------------------------------------------

ifeq ($(strip $(PS3DEV)),)
$(error PS3DEV is not set. Do: export PS3DEV=/usr/local/ps3dev)
endif

include $(PS3DEV)/ppu_rules

#----------------------------------------------------------------------------
# Package identity (APPID: exactly 9 characters; ':=' as ppu_rules sets
# TITLE/APPID with '?=' first)
#----------------------------------------------------------------------------

VERSION   := 1.1
ICON0     := $(CURDIR)/icon-wolf3d.png

# DEBUG_BUILD=1: the test package of 1.0 (ECWOLFDBG, installs next to the
# release one, with its own copy of the game data): L3 god mode, R3 next
# level, L1 all weapons (wl_play.cpp)
ifeq ($(DEBUG_BUILD),1)
TARGET    := ecellwolf-debug
TITLE     := ECellWolf Debug
APPID     := ECWOLFDBG
DEBUG_DEFS := -DECWOLF_DEBUG_BUILD
else
TARGET    := ecellwolf
TITLE     := ECellWolf
APPID     := ECWOLF001
DEBUG_DEFS :=
endif

CONTENTID := UP0001-$(APPID)_00-0000000000000001
USRDIR    := /dev_hdd0/game/$(APPID)/USRDIR
PKGFILES  := $(CURDIR)/pkgfiles
OUT_PKG   := $(TARGET)-$(VERSION).pkg

SRC       := $(abspath $(CURDIR)/..)
OBJDIR    := $(CURDIR)/obj/$(TARGET)
BUILDDIR  := $(CURDIR)/build/$(TARGET)

CC        := ppu-gcc
CXX       := ppu-g++

#----------------------------------------------------------------------------
# Flags
#----------------------------------------------------------------------------

COMMON := -O2 -fno-strict-aliasing -MMD -MP
COMMON += -include $(CURDIR)/ps3_compat.h
COMMON += -DPS3_APPID=\"$(APPID)\" -DPS3_USRDIR=\"$(USRDIR)\" $(DEBUG_DEFS)
COMMON += -I$(CURDIR)/compat -I$(CURDIR)/include -I$(CURDIR)/source
COMMON += $(LIBPSL1GHT_INC) -I$(PS3DEV)/portlibs/ppu/include
# PPU is LP64: a missing prototype truncates pointers (handoff notes 3.26)
COMMON_C := -Werror=implicit-function-declaration

ENGINE_CXXFLAGS := $(COMMON) -std=gnu++98 -fsigned-char
ENGINE_CXXFLAGS += -DUSE_GPL -DNO_GTK=1 -DPS3_GAME_PATH=\"$(USRDIR)/ecwolf\"
ENGINE_CXXFLAGS += -I$(SRC)/src -I$(SRC)/src/g_shared
ENGINE_CXXFLAGS += -I$(SRC)/deps/bzip2 -I$(SRC)/deps/jpeg-6b -I$(SRC)/deps/lzma/C -I$(SRC)/deps/gdtoa
ENGINE_CXXFLAGS += -Wall -Wno-unused -Wno-unused-parameter -Wno-missing-field-initializers
ENGINE_CXXFLAGS += -Wno-implicit-fallthrough -Wno-unused-result

DEPS_CFLAGS := $(COMMON) $(COMMON_C) -std=gnu99 -w

PLATFORM_CFLAGS := $(COMMON) $(COMMON_C) -std=gnu99 -Wall -Wno-unused-function
PLATFORM_CXXFLAGS := $(COMMON) -std=gnu++98 -fsigned-char -Wall -I$(SRC)/src -I$(SRC)/src/g_shared

#----------------------------------------------------------------------------
# Sources
#----------------------------------------------------------------------------

ENGINE_SRCS := \
	g_blake/a_smartanim.cpp \
	g_blake/blake_sbar.cpp \
	g_shared/a_deathcam.cpp \
	g_shared/a_inventory.cpp \
	g_shared/a_keys.cpp \
	g_shared/a_patrolpoint.cpp \
	g_shared/a_playerpawn.cpp \
	g_shared/a_randomspawner.cpp \
	g_wolf/a_spearofdestiny.cpp \
	g_wolf/wolf_sbar.cpp \
	r_2d/r_draw.cpp \
	r_2d/r_drawt.cpp \
	r_2d/r_main.cpp \
	r_2d/r_things.cpp \
	r_data/colormaps.cpp \
	r_data/r_translate.cpp \
	r_data/renderstyle.cpp \
	resourcefiles/resourcefile.cpp \
	resourcefiles/ancientzip.cpp \
	resourcefiles/file_7z.cpp \
	resourcefiles/file_audiot.cpp \
	resourcefiles/file_directory.cpp \
	resourcefiles/file_gamemaps.cpp \
	resourcefiles/file_grp.cpp \
	resourcefiles/file_pak.cpp \
	resourcefiles/file_rff.cpp \
	resourcefiles/file_rtl.cpp \
	resourcefiles/file_lump.cpp \
	resourcefiles/file_macbin.cpp \
	resourcefiles/file_vgagraph.cpp \
	resourcefiles/file_vswap.cpp \
	resourcefiles/file_wad.cpp \
	resourcefiles/file_zip.cpp \
	resourcefiles/wolfmapcommon.cpp \
	sfmt/SFMT.cpp \
	textures/anim_switches.cpp \
	textures/animations.cpp \
	textures/automaptexture.cpp \
	textures/bitmap.cpp \
	textures/ddstexture.cpp \
	textures/emptytexture.cpp \
	textures/flattexture.cpp \
	textures/imgztexture.cpp \
	textures/jpegtexture.cpp \
	textures/machudtexture.cpp \
	textures/multipatchtexture.cpp \
	textures/patchtexture.cpp \
	textures/picttexture.cpp \
	textures/pcxtexture.cpp \
	textures/pngtexture.cpp \
	textures/rawpagetexture.cpp \
	textures/rottflattexture.cpp \
	textures/solidtexture.cpp \
	textures/texture.cpp \
	textures/texturemanager.cpp \
	textures/tgatexture.cpp \
	textures/warptexture.cpp \
	textures/wolfrawtexture.cpp \
	textures/wolfshapetexture.cpp \
	thingdef/thingdef.cpp \
	thingdef/thingdef_codeptr.cpp \
	thingdef/thingdef_expression.cpp \
	thingdef/thingdef_parse.cpp \
	thingdef/thingdef_properties.cpp \
	thingdef/thingdef_type.cpp \
	actor.cpp \
	am_map.cpp \
	colormatcher.cpp \
	config.cpp \
	c_cvars.cpp \
	dobject.cpp \
	dobjgc.cpp \
	farchive.cpp \
	files.cpp \
	filesys.cpp \
	filesys_steam.cpp \
	g_conversation.cpp \
	g_intermission.cpp \
	g_mapinfo.cpp \
	gamemap.cpp \
	gamemap_planes.cpp \
	gamemap_uwmf.cpp \
	gitinfo.cpp \
	id_ca.cpp \
	id_in.cpp \
	id_sd.cpp \
	id_sd_n3dmus.cpp \
	id_us_1.cpp \
	id_vh.cpp \
	id_vl.cpp \
	language.cpp \
	lnspec.cpp \
	lumpremap.cpp \
	m_alloc.cpp \
	m_classes.cpp \
	m_random.cpp \
	m_png.cpp \
	name.cpp \
	p_switch.cpp \
	r_gpu.cpp \
	r_sprites.cpp \
	scanner.cpp \
	sdlvideo.cpp \
	sndinfo.cpp \
	sndseq.cpp \
	thinker.cpp \
	v_draw.cpp \
	v_font.cpp \
	v_palette.cpp \
	v_pfx.cpp \
	v_text.cpp \
	v_video.cpp \
	w_wad.cpp \
	weaponslots.cpp \
	wl_act2.cpp \
	wl_agent.cpp \
	wl_atmos.cpp \
	wl_cloudsky.cpp \
	wl_debug.cpp \
	wl_dir3dspr.cpp \
	wl_draw.cpp \
	wl_floorceiling.cpp \
	wl_game.cpp \
	wl_inter.cpp \
	wl_iwad.cpp \
	wl_loadsave.cpp \
	wl_main.cpp \
	wl_menu.cpp \
	wl_net.cpp \
	wl_parallax.cpp \
	wl_play.cpp \
	wl_state.cpp \
	wl_text.cpp \
	zstrformat.cpp \
	zstring.cpp \
	dosbox/dbopl.cpp \
	posix/i_main.cpp

BZ2_SRCS  := blocksort.c bzlib.c compress.c crctable.c decompress.c huffman.c randtable.c
LZMA_SRCS := 7zArcIn.c 7zBuf.c 7zCrc.c 7zCrcOpt.c 7zDec.c 7zStream.c Bcj2.c Bra.c Bra86.c \
	BraIA64.c CpuArch.c Delta.c LzFind.c Lzma2Dec.c LzmaDec.c LzmaEnc.c Ppmd7.c Ppmd7Dec.c
GDTOA_SRCS := dmisc.c dtoa.c misc.c
JPEG_SRCS := $(notdir $(wildcard $(SRC)/deps/jpeg-6b/*.c))

PLATFORM_SRCS := $(notdir $(wildcard $(CURDIR)/source/*.c))
PLATFORM_CPP  := $(notdir $(wildcard $(CURDIR)/source/*.cpp))

OBJS := $(patsubst %.cpp,$(OBJDIR)/engine/%.o,$(filter %.cpp,$(ENGINE_SRCS))) \
	$(patsubst %.c,$(OBJDIR)/engine/%.o,$(filter %.c,$(ENGINE_SRCS))) \
	$(patsubst %.c,$(OBJDIR)/bzip2/%.o,$(BZ2_SRCS)) \
	$(patsubst %.c,$(OBJDIR)/lzma/%.o,$(LZMA_SRCS)) \
	$(patsubst %.c,$(OBJDIR)/gdtoa/%.o,$(GDTOA_SRCS)) \
	$(patsubst %.c,$(OBJDIR)/jpeg/%.o,$(JPEG_SRCS)) \
	$(patsubst %.c,$(OBJDIR)/platform/%.o,$(PLATFORM_SRCS)) \
	$(patsubst %.cpp,$(OBJDIR)/platform/%.o,$(PLATFORM_CPP))

LIBS := -lstdc++ -lz -lgcm_sys -lrsx -lsysutil -lio -laudio -lrt -llv2 -lm

#----------------------------------------------------------------------------
# Rules
#----------------------------------------------------------------------------

all: $(OUT_PKG)

$(OBJDIR)/engine/%.o: $(SRC)/src/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(ENGINE_CXXFLAGS) -c $< -o $@

$(OBJDIR)/engine/%.o: $(SRC)/src/%.c
	@mkdir -p $(dir $@)
	$(CC) $(DEPS_CFLAGS) -c $< -o $@

$(OBJDIR)/bzip2/%.o: $(SRC)/deps/bzip2/%.c
	@mkdir -p $(dir $@)
	$(CC) $(DEPS_CFLAGS) -DBZ_NO_STDIO -c $< -o $@

$(OBJDIR)/lzma/%.o: $(SRC)/deps/lzma/C/%.c
	@mkdir -p $(dir $@)
	$(CC) $(DEPS_CFLAGS) -D_7ZIP_PPMD_SUPPPORT -D_7ZIP_ST -c $< -o $@

$(OBJDIR)/gdtoa/%.o: $(SRC)/deps/gdtoa/%.c
	@mkdir -p $(dir $@)
	$(CC) $(DEPS_CFLAGS) -DINFNAN_CHECK -DMULTIPLE_THREADS -c $< -o $@

$(OBJDIR)/jpeg/%.o: $(SRC)/deps/jpeg-6b/%.c
	@mkdir -p $(dir $@)
	$(CC) $(DEPS_CFLAGS) -c $< -o $@

$(OBJDIR)/platform/%.o: $(CURDIR)/source/%.c
	@mkdir -p $(dir $@)
	$(CC) $(PLATFORM_CFLAGS) -c $< -o $@

$(OBJDIR)/platform/%.o: $(CURDIR)/source/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(PLATFORM_CXXFLAGS) -c $< -o $@

# The file that talks to GCM the most at -O1 -fno-inline (PSL1GHT's
# syscall stubs don't survive aggressive inlining)
$(OBJDIR)/platform/ps3_gpu.o: PLATFORM_CFLAGS := $(filter-out -O2,$(PLATFORM_CFLAGS)) -O1 -fno-inline

-include $(OBJS:.o=.d)

$(TARGET).elf: $(OBJS)
	$(CXX) $(OBJS) $(LIBPSL1GHT_LIB) -L$(PS3DEV)/portlibs/ppu/lib -o $@ $(LIBS)

# ecwolf.pk3: wadsrc/static zipped (what tools/zipdir does for CMake)
WADSRC_FILES := $(shell find $(SRC)/wadsrc/static -type f)
$(WADSRC_FILES): ;

$(PKGFILES)/USRDIR/ecwolf.pk3: $(WADSRC_FILES)
	@mkdir -p $(dir $@)
	rm -f $@
	cd $(SRC)/wadsrc/static && zip -q -r -X $@ .

$(TARGET).pkg: $(PKGFILES)/USRDIR/ecwolf.pk3 programs

.PHONY: programs
programs: $(TARGET).self
	mkdir -p $(BUILDDIR)/pkg/USRDIR
	cp $(TARGET).self $(BUILDDIR)/pkg/USRDIR/RELOAD.SELF

$(OUT_PKG): $(TARGET).pkg
	cp $(TARGET).gnpdrm.pkg $@

clean:
	rm -rf $(CURDIR)/obj $(CURDIR)/build $(PKGFILES) *.elf *.self

.PHONY: all clean
