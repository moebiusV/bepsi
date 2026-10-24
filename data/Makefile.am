## systemd units and sysusers snippet.

systemdsystemunit_DATA = bepsid.service
sysusers_DATA = bepsi.sysusers

EXTRA_DIST = bepsid.service.in bepsi-watch-ws@.service.in bepsi-httpd.service.in \
	bepsi.sysusers
CLEANFILES = bepsid.service bepsi-watch-ws@.service bepsi-httpd.service

if BUILD_WATCHERS
systemdsystemunit_DATA += bepsi-watch-ws@.service
endif

if BUILD_HTTPD
systemdsystemunit_DATA += bepsi-httpd.service
endif

SUBST_UNIT = sed -e 's|@sbindir[@]|$(sbindir)|g' -e 's|@sysconfdir[@]|$(sysconfdir)|g'

bepsid.service: bepsid.service.in Makefile
	$(AM_V_GEN)$(SUBST_UNIT) $(srcdir)/bepsid.service.in > $@

bepsi-watch-ws@.service: bepsi-watch-ws@.service.in Makefile
	$(AM_V_GEN)$(SUBST_UNIT) $(srcdir)/bepsi-watch-ws@.service.in > $@

bepsi-httpd.service: bepsi-httpd.service.in Makefile
	$(AM_V_GEN)$(SUBST_UNIT) $(srcdir)/bepsi-httpd.service.in > $@
