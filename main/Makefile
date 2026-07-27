#

LN      =       ln -sf
MAKE    =       make -i -r
RM      =       rm -f

MODULES =  module_wrf_top.F

OBJS    =

LIBPATHS = 

include ../configure.wrf

# LibTorch implicit scheme library
libwrf_implicit.a:
	( cd $(WRF_SRC_ROOT_DIR)/../libtorch_ad/src ; \
	  $(MAKE) clean ; \
	  $(MAKE) ; \
	  cp libtorch_acoustic_interface.a $(WRF_SRC_ROOT_DIR)/main/libwrf_implicit.a )

$(SOLVER)_wrf : wrf.o ../main/module_wrf_top.o
	$(RANLIB) $(RLFLAGS) $(LIBWRFLIB)
	$(LD) -o wrf.exe $(LDFLAGS) wrf.o ../main/module_wrf_top.o $(LIBWRFLIB) $(CMAQLIB) $(LIB) 

$(SOLVER)_wrfplus : wrf.o ../main/module_wrf_top.o
	$(RANLIB) $(RLFLAGS) $(LIBWRFLIB)
	$(LD) -o wrfplus.exe $(LDFLAGS) wrf.o ../main/module_wrf_top.o $(LIBWRFLIB) $(LIB)

$(SOLVER)_wrf_SST_ESMF : wrf_ESMFMod.o wrf_SST_ESMF.o ../main/module_wrf_top.o
	$(RANLIB) $(RLFLAGS) $(LIBWRFLIB)
	$(LD) -o wrf_SST_ESMF.exe $(LDFLAGS) wrf_SST_ESMF.o wrf_ESMFMod.o ../main/module_wrf_top.o $(LIBWRFLIB) $(LIB)

$(SOLVER)_ideal : module_initialize ideal_$(SOLVER).o
	$(RANLIB) $(RLFLAGS) $(LIBWRFLIB)
	$(LD) -o ideal.exe $(LDFLAGS) ideal_$(SOLVER).o ../dyn_$(SOLVER)/module_initialize_$(IDEAL_CASE).o $(LIBWRFLIB) $(LIB)

$(SOLVER)_real : module_initialize ndown_$(SOLVER).o tc_$(SOLVER).o real_$(SOLVER).o
	$(RANLIB) $(RLFLAGS) $(LIBWRFLIB)
	$(LD) -o ndown.exe $(LDFLAGS) ndown_$(SOLVER).o  ../dyn_$(SOLVER)/module_initialize_$(IDEAL_CASE).o $(LIBWRFLIB) $(LIB)
	$(LD) -o tc.exe $(LDFLAGS) tc_$(SOLVER).o  ../dyn_$(SOLVER)/module_initialize_$(IDEAL_CASE).o $(LIBWRFLIB) $(LIB)
	$(LD) -o real.exe $(LDFLAGS) real_$(SOLVER).o ../dyn_$(SOLVER)/module_initialize_$(IDEAL_CASE).o $(LIBWRFLIB) $(LIB)

convert_em : convert_em.o
	$(RANLIB) $(RLFLAGS) $(LIBWRFLIB)
	$(LD) -o convert_em.exe $(LDFLAGS) convert_em.o $(LIBWRFLIB) $(LIB)

# 9F.D25 (review section 12): Fortran INCLUDE dependencies are NOT tracked anywhere
# in this build, so editing an included file leaves the .o stale while make reports
# success. OBSERVED: after moving bwave_pert_scale_parse.inc, `compile` exited 0
# against an object built from the file's old location.
#
# The dangerous shape is silent -- the hosted-CI parser contract compiles the .inc
# DIRECTLY, so CI goes green on the NEW parser while the linked ideal.exe can still
# contain the OLD one. Green CI would then certify a binary that lacks the code it
# tested.
#
# It must be declared HERE, not in dyn_em/Makefile: the ideal-case object is built
# while make runs in main/ (recipe below is commented out, so a suffix rule fires),
# and dyn_em/Makefile is never loaded in that context. A dyn_em-side declaration was
# tried first and measured to have NO effect.
../dyn_$(SOLVER)/module_initialize_$(IDEAL_CASE).o: ../dyn_$(SOLVER)/bwave_pert_scale_parse.inc

module_initialize : ../dyn_$(SOLVER)/module_initialize_$(IDEAL_CASE).o
#	( cd ../dyn_$(SOLVER) ;  $(MAKE) module_initialize_$(IDEAL_CASE).o )

## prevent real being compiled for OMP -- only for regtesting
#$(SOLVER)_real : module_initialize real_$(SOLVER).o
#	$(RANLIB) $(RLFLAGS) $(LIBWRFLIB)
#	if [ -z "$(OMP)" ] ; then $(FC) -o real.exe $(LDFLAGS) real_$(SOLVER).o ../dyn_$(SOLVER)/module_initialize_$(IDEAL_CASE).o $(LIBWRFLIB) $(LIB) ; fi
#
## prevent module_initialize being compiled for OMP --remove after IBM debugging
#module_initialize :
#	if [ -z "$(OMP)" ] ; then ( cd ../dyn_$(SOLVER) ;  $(MAKE) module_initialize_$(IDEAL_CASE).o ) ; fi
# end of regtest changes

clean:
	@ echo 'use the clean script'

# DEPENDENCIES : only dependencies after this line (don't remove the word DEPENDENCIES)

include depend.common

# DO NOT DELETE
