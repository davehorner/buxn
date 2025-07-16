add_executable(buxn-tests
	"main.c"
	"common.c"
	"asm.c"
	"asm-extensions.c"
	"vm.c"
	"dbg.c"
	"chess.c"
)
target_link_libraries(buxn-tests PRIVATE
	blibs
	buxn-vm
	buxn-asm
	buxn-asm-chess
	buxn-devices
	buxn-dbg-core
	buxn-dbg-wire
	buxn-dbg-protocol
	buxn-dbg-transport
)
target_compile_definitions(buxn-tests PRIVATE BUXN_CMAKE_PATH)
