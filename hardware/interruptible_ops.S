extern probe_source_pointer datausage
extern probe_destination_pointer datausage
extern probe_byte_count datausage
extern probe_rows datausage
extern probe_source_stride datausage
extern probe_destination_stride datausage

global hw_probe_current_priority routineusage
hw_probe_current_priority:
	ldpri
	ret

global hw_probe_move routineusage
hw_probe_move:
	ldc probe_source_pointer
	ldnl 0
	ldc probe_destination_pointer
	ldnl 0
	ldc probe_byte_count
	ldnl 0
	move
	ret

global hw_probe_devmove routineusage
hw_probe_devmove:
	ldc probe_source_pointer
	ldnl 0
	ldc probe_destination_pointer
	ldnl 0
	ldc probe_byte_count
	ldnl 0
	devmove
	ret

global hw_probe_move2dall routineusage
hw_probe_move2dall:
	ldc probe_source_stride
	ldnl 0
	ldc probe_destination_stride
	ldnl 0
	ldc probe_rows
	ldnl 0
	move2dinit
	ldc probe_source_pointer
	ldnl 0
	ldc probe_destination_pointer
	ldnl 0
	ldc probe_byte_count
	ldnl 0
	move2dall
	ret

global hw_probe_move2dnonzero routineusage
hw_probe_move2dnonzero:
	ldc probe_source_stride
	ldnl 0
	ldc probe_destination_stride
	ldnl 0
	ldc probe_rows
	ldnl 0
	move2dinit
	ldc probe_source_pointer
	ldnl 0
	ldc probe_destination_pointer
	ldnl 0
	ldc probe_byte_count
	ldnl 0
	move2dnonzero
	ret

global hw_probe_move2dzero routineusage
hw_probe_move2dzero:
	ldc probe_source_stride
	ldnl 0
	ldc probe_destination_stride
	ldnl 0
	ldc probe_rows
	ldnl 0
	move2dinit
	ldc probe_source_pointer
	ldnl 0
	ldc probe_destination_pointer
	ldnl 0
	ldc probe_byte_count
	ldnl 0
	move2dzero
	ret
