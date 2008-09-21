SHELL := /bin/sh
VPATH := src/:src/base/:src/num/:src/verif/:src/eismint/:src/ismip/
ALL : all

# get PETSc environment, rules:
include ${PETSC_DIR}/bmake/common/base

#FLAGS:
# PETSc has troubles choosing a linker which can link C++. PISM is C++. Setting
# this to zero would let PETSc choose a linker.
USE_MPICXX = 1

PISM_PREFIX ?= `pwd`
WITH_FFTW ?= 1
LOG_PISM_EVENTS ?= 0
CFLAGS += -DWITH_FFTW=${WITH_FFTW} -DLOG_PISM_EVENTS=${LOG_PISM_EVENTS} -pipe

TESTS_LIB_FLAGS := -L`pwd` -L`pwd`/lib -Wl,-rpath,`pwd` -Wl,-rpath,`pwd`/lib \
   -ltests -lm -lgsl -lgslcblas
ICE_LIB_FLAGS := -lpism ${TESTS_LIB_FLAGS} ${PETSC_LIB} -lnetcdf 
ifeq (${WITH_FFTW}, 1)
	ICE_LIB_FLAGS += -lfftw3
endif


#VARIABLES:
executables := pismr pismd pismv pisms pgrn
extra_execs := simpleABCD simpleE simpleFG simpleH simpleI simpleJ \
   simpleL gridL flowTable tryLCbd pant

ice_sources := extrasGSL.cc grid.cc iMbasal.cc iMbeddef.cc iMbootstrap.cc iMdefaults.cc \
	iMgrainsize.cc iMIO.cc iMmatlab.cc iMnames.cc iMoptions.cc iMpdd.cc \
	iMreport.cc iMssa.cc iMsia.cc iMtemp.cc iMutil.cc iMvelocity.cc iMviewers.cc \
	iceModel.cc materials.cc nc_util.cc iceModelVec.cc iceModelVec3.cc \
	beddefLC.cc forcing.cc
ice_csources := cubature.c pism_signal.c

tests_sources := exactTestsABCDE.c exactTestsFG.c exactTestH.c exactTestsIJ.c \
   exactTestK.c exactTestL.c

other_sources := pismr.cc pismd.cc pismv.cc pisms.cc pant.cc pgrn.cc \
	iceEISModel.cc iceMISMIPModel.cc iceROSSModel.cc iceGRNModel.cc \
	iceEISplModel.cc iceCompModel.cc iceUpwindCompModel.cc iceExactSSAModel.cc iCMthermo.cc \
	flowTable.cc tryLCbd.cc
other_csources := simpleABCD.c simpleE.c simpleFG.c simpleH.c simpleI.c \
   simpleJ.c simpleK.c simpleL.c

#INCLUDE ADDITIONAL make INCLUDE FILES HERE: 
#include config/ryan_make

TESTS_OBJS := $(tests_sources:.c=.o)

ICE_OBJS := $(ice_sources:.cc=.o) $(ice_csources:.c=.o)

depfiles := $(ice_sources:.cc=.d) $(ice_csources:.c=.d) $(tests_sources:.c=.d) \
	$(other_sources:.cc=.d) $(other_csources:.c=.d)

all : depend libpism.so libtests.so $(executables) .pismmakeremind

install : local_install clean

local_install : depend libpism.so libtests.so $(executables)
	@cp libpism.so ${PISM_PREFIX}/lib/
	@cp libtests.so ${PISM_PREFIX}/lib/
	@echo 'PISM libraries installed in ' ${PISM_PREFIX}'/lib/'
	@cp $(executables) ${PISM_PREFIX}/bin/
	@rm -f libpism.so
	@rm -f libtests.so
	@rm -f $(executables)
	@echo 'PISM executables installed in ' ${PISM_PREFIX}'/bin/'
	@rm .pismmakeremind

# Override PETSc's choice of the linker (if asked to):
ifeq ($(USE_MPICXX), 1)
  CXXLINKER = $(subst mpicc,mpicxx,$(CLINKER))
endif

libpism.so : ${ICE_OBJS}
	${CXXLINKER} -shared ${ICE_OBJS} -o $@
#for static-linking:  ar cru -s libpism.a ${ICE_OBJS}   etc

libtests.so : ${TESTS_OBJS}
	${CLINKER} -shared ${TESTS_OBJS} -o $@

pismr : pismr.o libpism.so
	${CXXLINKER} $< ${ICE_LIB_FLAGS} -o $@

pismd : pismd.o iceROSSModel.o libpism.so
	${CXXLINKER} iceROSSModel.o pismd.o ${ICE_LIB_FLAGS} -o $@

pisms : iceEISModel.o iceMISMIPModel.o iceEISplModel.o pisms.o libpism.so
	${CXXLINKER} iceEISModel.o iceMISMIPModel.o iceEISplModel.o pisms.o ${ICE_LIB_FLAGS} -o $@

pismv : iCMthermo.o iceCompModel.o iceUpwindCompModel.o iceExactSSAModel.o pismv.o libpism.so libtests.so
	${CXXLINKER} iCMthermo.o iceCompModel.o iceUpwindCompModel.o iceExactSSAModel.o pismv.o ${ICE_LIB_FLAGS} -o $@

pant : pant.o libpism.so
	${CXXLINKER} $< ${ICE_LIB_FLAGS} -o $@

pgrn : iceGRNModel.o pgrn.o libpism.so
	${CXXLINKER} iceGRNModel.o pgrn.o ${ICE_LIB_FLAGS} -o $@

#shelf : shelf.o libpism.so
#	${CXXLINKER} $< ${ICE_LIB_FLAGS} -o $@

flowTable : flowTable.o materials.o
	${CXXLINKER} flowTable.o materials.o ${ICE_LIB_FLAGS} -o $@

tryLCbd : tryLCbd.o beddefLC.o materials.o
	${CXXLINKER} tryLCbd.o beddefLC.o materials.o ${ICE_LIB_FLAGS} -o $@

simpleABCD : simpleABCD.o libtests.so
	${CLINKER} $< ${TESTS_LIB_FLAGS} -o $@

simpleE : simpleE.o libtests.so
	${CLINKER} $< ${TESTS_LIB_FLAGS} -o $@

simpleFG : simpleFG.o libtests.so
	${CLINKER} $< ${TESTS_LIB_FLAGS} -o $@

simpleH : simpleH.o libtests.so
	${CLINKER} $< ${TESTS_LIB_FLAGS} -o $@

simpleI : simpleI.o libtests.so
	${CLINKER} $< ${TESTS_LIB_FLAGS} -o $@

simpleJ : simpleJ.o libtests.so
	${CLINKER} $< ${TESTS_LIB_FLAGS} -o $@

simpleL : simpleL.o libtests.so
	${CLINKER} $< ${TESTS_LIB_FLAGS} -o $@

simpleK : simpleK.o libtests.so
	${CLINKER} $< ${TESTS_LIB_FLAGS} -o $@

gridL : gridL.o libtests.so
	${CLINKER} $< ${TESTS_LIB_FLAGS} -o $@

# Cancel the implicit rules  WHY?
#% : %.cc
#% : %.c

.pismmakeremind :
	@touch .pismmakeremind
	@echo '*** Remember to "make install".  For now executables are "./pismv" etc ***'

showEnv :
	@echo ${CLINKER}
	@echo ${CXXLINKER}
	@echo ${PETSC_DIR}
	@echo ${PETSC_LIB}
	@echo ${ICE_LIB_FLAGS}
	@echo ${TESTS_LIB_FLAGS}
	@echo ${ICE_OBJS}

# Emacs style tags
.PHONY: tags TAGS
tags TAGS :
	etags *.cc *.hh *.c

# The GNU recommended procedure for automatically generating dependencies.
# This rule updates the `*.d' to reflect changes in `*.cc' files
%.d : %.cc
	@echo "Dependencies from" $< "-->" $@
	@set -e; rm -f $@; \
	 $(CC) -MM $< > $@.$$$$; \
	 sed 's,\($*\)\.o[ :]*,\1.o $@ : ,g' < $@.$$$$ > $@; \
	 rm -f $@.$$$$

# This rule updates the `*.d' to reflect changes in `*.c' files
%.d : %.c
	@echo "Dependencies from" $< "-->" $@
	@set -e; rm -f $@; \
	 $(CC) -MM $< > $@.$$$$; \
	 sed 's,\($*\)\.o[ :]*,\1.o $@ : ,g' < $@.$$$$ > $@; \
	 rm -f $@.$$$$

depend : $(depfiles)

depclean :
	@rm -f *.d

clean : depclean

distclean : clean
	rm -f TAGS \
	libpism.so libtests.so lib/libpism.so lib/libtests.so \
	${executables} $(patsubst %, bin/%, ${executables})\
	${extra_execs} $(patsubst %, bin/%, ${extra_execs})

.PHONY: clean

include $(depfiles)

