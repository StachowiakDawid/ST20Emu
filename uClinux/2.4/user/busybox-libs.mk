# Extra makefile for busybox 1.00 (make -f Makefile -f busybox-libs.mk bb-libs): builds the
# applet archives like the `busybox' target, but leaves the final link (lit, twice, for
# mkflat.py) to user/build-user.sh, which reads the archive list from busybox.libs.
bb-libs: $(ALL_MAKEFILES) .depend include/config.h $(libraries-y) busybox.links
	echo $(libraries-y) > /work/user/build/busybox.libs
