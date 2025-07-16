# --- buxn ---

add_library(buxn INTERFACE)
target_include_directories(buxn INTERFACE "../include")

# --- buxn-vm ---

add_library(buxn-vm STATIC "vm/vm.c")
target_link_libraries(buxn-vm PUBLIC buxn)

# --- buxn-asm ---

add_library(buxn-asm STATIC "asm/asm.c")
target_link_libraries(buxn-asm PUBLIC buxn)

# --- buxn-asm-chess ---

add_library(buxn-asm-chess STATIC "asm/chess.c")
target_link_libraries(buxn-asm-chess PUBLIC buxn)

# --- buxn-asm-annotation ---

add_library(buxn-asm-annotation STATIC "asm/annotation.c")
target_link_libraries(buxn-asm-annotation PUBLIC buxn)

# --- buxn-dbg-core ---

add_library(buxn-dbg-core STATIC "dbg/core.c")
target_link_libraries(buxn-dbg-core PUBLIC buxn)

# --- buxn-dbg-symtab ---

add_library(buxn-dbg-symtab STATIC "dbg/symtab.c")
target_link_libraries(buxn-dbg-symtab PUBLIC buxn blibs)

# --- buxn-dbg-protocol ---

add_library(buxn-dbg-protocol STATIC "dbg/protocol.c")
target_link_libraries(buxn-dbg-protocol PUBLIC buxn blibs)

# --- buxn-dbg-wire ---

add_library(buxn-dbg-wire STATIC "dbg/wire.c")
target_link_libraries(buxn-dbg-wire PUBLIC buxn blibs)

# --- buxn-dbg-transport ---

add_library(buxn-dbg-transport STATIC
	"dbg/transports/fd.c"
	"dbg/transports/file.c"
	"dbg/transports/from_str.c"
	"dbg/transports/stream.c"
)
target_link_libraries(buxn-dbg-transport PUBLIC buxn blibs)

# --- buxn-dbg-integration ---

add_library(buxn-dbg-integration STATIC "dbg.c")
target_link_libraries(buxn-dbg-integration PUBLIC
	buxn
	buxn-dbg-core
	buxn-dbg-wire
	buxn-dbg-protocol
	buxn-dbg-transport
)
target_link_libraries(buxn-dbg-integration PRIVATE blibs)

# --- buxn-metadata ---

add_library(buxn-metadata STATIC "metadata.c")
target_link_libraries(buxn-metadata PRIVATE buxn)

# --- buxn-devices ---

add_library(buxn-devices STATIC
	"devices/audio.c"
	"devices/controller.c"
	"devices/file.c"
	"devices/screen.c"
	"devices/console.c"
	"devices/datetime.c"
	"devices/mouse.c"
	"devices/system.c"
)
target_link_libraries(buxn-devices PUBLIC buxn)

# --- buxn-physfs ---

add_library(buxn-physfs STATIC "physfs.c")
target_link_libraries(buxn-physfs PRIVATE buxn-devices physfs blibs)

# --- buxn-cli ---

add_executable(buxn-cli "cli.c")
target_link_libraries(buxn-cli PRIVATE
	physfs
	buxn-vm
	buxn-devices
	buxn-physfs
	buxn-dbg-integration
	blibs
)

# --- buxn-gui ---

set(BUXN_GUI_COMMON_SOURCES "gui.c" "libs.c")
set(BUXN_GUI_LINUX_SOURCES "linux/platform.c")

if (LINUX)
	add_executable(buxn-gui ${BUXN_GUI_COMMON_SOURCES} ${BUXN_GUI_LINUX_SOURCES})
	target_compile_definitions(buxn-gui PRIVATE SOKOL_GLES3 BUXN_CMAKE_PATH)
	target_link_libraries(buxn-gui PRIVATE
		GL EGL m X11 Xcursor Xi asound
	)
endif ()

target_link_libraries(buxn-gui PRIVATE
	physfs
	buxn-vm
	buxn-metadata
	buxn-devices
	buxn-physfs
	buxn-dbg-integration
	sokol
	sokol_gp
	blibs
)

# --- buxn-asm-frontend ---

add_executable(buxn-asm-frontend "asm.c")
target_link_libraries(buxn-asm-frontend PRIVATE
	buxn-asm
	buxn-asm-chess
	buxn-dbg-symtab
	blibs
	utf8proc
)
set_target_properties(buxn-asm-frontend PROPERTIES
	OUTPUT_NAME buxn-asm
)

# --- buxn-rom2exe ---

add_executable(buxn-rom2exe "rom2exe.c")

# --- buxn-romviz ---

add_executable(buxn-romviz "romviz.c")
target_link_libraries(buxn-romviz PRIVATE buxn-dbg-symtab termbox2)

# --- buxn-ctags ---

add_executable(buxn-ctags "ctags.c")
target_link_libraries(buxn-ctags PRIVATE buxn-asm blibs)

# --- buxn-bindgen ---

add_executable(buxn-bindgen "bindgen.c")
target_link_libraries(buxn-bindgen PRIVATE buxn-asm buxn-asm-annotation blibs)

# --- buxn-repl ---

add_executable(buxn-repl "repl.c")
target_link_libraries(buxn-repl PRIVATE
	blibs
	buxn-asm
	buxn-asm-chess
	bestline
	physfs
	utf8proc
	buxn-devices
	buxn-vm
	buxn-physfs
)
target_compile_definitions(buxn-repl PRIVATE BUXN_REPL_CMAKE_PATH)
