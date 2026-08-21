# -R because app.d/ is a DIRECTORY that the *.* glob already matches -- "app.d"
# contains a dot.  Adding a separate line for it does not help; the glob picks it
# up either way and plain cp refuses it.  (BSD cp only warns, so this fails in CI
# and not on a Mac.)  HEADER stays out: it has no dot, and it is data for
# update_headers.pl rather than anything the image needs.
extras: ../../scripts/
	mkdir -p $@
	cp -R $</*.* $@/
	cp Dockerfile $@/

clean:
	rm -f *~

clobber:
	git clean -xdf .
