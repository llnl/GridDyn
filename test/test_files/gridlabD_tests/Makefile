

testFiles:=$(sort $(wildcard ./bin/test-*.tc))

.PHONY: check
check: ${testFiles}

%.tc: clean $@
	@${PARGRID_SRC_DIR}/bin/test-runner $@

.PHONY: demo6
demo6:
	@pgriddyn Simple_3Bus_5040Houses.xml

.PHONY: demo7
demo7:
	@pgriddyn Simple_3Bus_ReplicateTest.xml --powerflow-output=Simple_3Bus_ReplicateTest.powerflow.csv

.PHONY: debug
debug:
	pgriddyn_debug IEEE_13_mod.xml

.PHONY: clean
clean:
	@rm -fr output/*
	@rm -f stdout.* *.out
	@rm -f *.core *.btr
	@rm -f Simple_3Bus_mod3x_current_pw.csv Simple_3Bus_mod3x_impedance.csv Simple_3Bus_mod3x_mix.csv
	@rm -f Simple_3Bus_mod_workdir/* Simple_3Bus_mod_workdir2/* Simple_3Bus_mod_workdir3/*
