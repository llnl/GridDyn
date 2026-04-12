testFiles=$(sort $(wildcard ./bin/*.tc))

.PHONY: check
check: ${testFiles}

%.tc: clean $@
	@${PARGRID_SRC_DIR}/bin/test-runner $@


.PHONY: clean
clean:
	@rm -fr output/*
	@rm -f dynfault.csv *.tr
	@rm -f geco-flow.xml
