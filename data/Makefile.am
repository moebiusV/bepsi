## systemd unit and sysusers snippet.

systemdsystemunit_DATA = bepsid.service
sysusers_DATA = bepsi.sysusers

EXTRA_DIST = bepsid.service.in bepsi.sysusers
CLEANFILES = bepsid.service

bepsid.service: bepsid.service.in Makefile
	$(AM_V_GEN)sed -e 's|@sbindir[@]|$(sbindir)|g' \
	    -e 's|@sysconfdir[@]|$(sysconfdir)|g' $(srcdir)/bepsid.service.in > $@
