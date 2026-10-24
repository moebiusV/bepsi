## systemd units and sysusers snippet.

systemdsystemunit_DATA = bepsid.service
sysusers_DATA = bepsi.sysusers

EXTRA_DIST = bepsid.service.in bepsi-watch-lightning.service.in bepsi.sysusers
CLEANFILES = bepsid.service bepsi-watch-lightning.service

if BUILD_WATCHERS
systemdsystemunit_DATA += bepsi-watch-lightning.service
endif

SUBST_UNIT = sed -e 's|@sbindir[@]|$(sbindir)|g' -e 's|@sysconfdir[@]|$(sysconfdir)|g'

bepsid.service: bepsid.service.in Makefile
	$(AM_V_GEN)$(SUBST_UNIT) $(srcdir)/bepsid.service.in > $@

bepsi-watch-lightning.service: bepsi-watch-lightning.service.in Makefile
	$(AM_V_GEN)$(SUBST_UNIT) $(srcdir)/bepsi-watch-lightning.service.in > $@
