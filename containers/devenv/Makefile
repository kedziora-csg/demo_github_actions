extras: ../../scripts/
	mkdir -p $@
	cp $</*.* $@/
	cp -R $</app.d $@/
	cp Dockerfile $@/

clean:
	rm -f *~

clobber:
	git clean -xdf .
