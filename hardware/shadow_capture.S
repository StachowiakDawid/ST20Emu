extern probe_snapshot_address datausage

global hw_probe_capture_low_shadow routineusage
hw_probe_capture_low_shadow:
	ldc probe_snapshot_address
	ldnl 0
	ldc 1
	ldc 15
	stshadow
	ret
