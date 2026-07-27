#

LN      =       ln -sf
MAKE    =       make -i -r
RM      =       rm -f


MODULES =                 		\
        module_advect_em.o   		\
        module_ieva_em.o            \
	  module_diffusion_em.o  		\
	  module_small_step_em.o 		\
        module_big_step_utilities_em.o  \
        module_em.o         		\
        module_solvedebug_em.o    	\
        module_bc_em.o                  \
        module_init_utilities.o         \
        module_wps_io_arw.o             \
	  module_damping_em.o		\
	  module_polarfft.o		\
        module_force_scm.o              \
        module_first_rk_step_part1.o    \
        module_first_rk_step_part2.o    \
        module_avgflx_em.o              \
	  module_sfs_nba.o		\
        module_convtrans_prep.o         \
	  module_sfs_driver.o		\
	  module_stoch.o			\
	  module_after_all_rk_steps.o	\
	  module_implicit_sdirk3.o	\
	  $(CASE_MODULE)

# possible CASE_MODULE settings
#	module_initialize_b_wave.o      \
#	module_initialize_grav2d_x.o    \
#	module_initialize_heldsuarez.o  \
#	module_initialize_hill2d_x.o    \
#	module_initialize_quarter_ss.o  \
#	module_initialize_real.o        \
#	module_initialize_lsm_x.o       \
#	module_initialize_les.o         \
#	module_initialize_squall2d_x.o  \
#	module_initialize_squall2d_y.o 

OBJS    = 		        \
	init_modules_em.o       \
	solve_em.o              \
        start_em.o              \
        shift_domain_em.o       \
        couple_or_uncouple_em.o \
        nest_init_utils.o	\
        adapt_timestep_em.o     \
        interp_domain_em.o
 

LIBTARGET    =  dyn_em
TARGETDIR    =  ./
$(LIBTARGET) :  $(MODULES) $(OBJS)
		$(AR) $(ARFLAGS) ../main/$(LIBWRFLIB) $(MODULES) $(OBJS)

include ../configure.wrf

cio.o :
	$(CC) -c $(CFLAGS) cio.c

clean:
	@ echo 'use the clean script'

# DEPENDENCIES : only dependencies after this line (don't remove the word DEPENDENCIES)

include depend.dyn_em

# 9F.D25 (review section 12): Fortran INCLUDE dependencies are NOT tracked.
#
# depend.dyn_em carries no entry for module_initialize_ideal at all, and the WRF
# dependency generator does not scan Fortran INCLUDE lines, so editing an included
# file leaves the .o stale while make reports success. That was OBSERVED, not
# theorised: after moving bwave_pert_scale_parse.inc, `compile` exited 0 against an
# object built from the file's previous location, and it only rebuilt once the .o was
# force-removed.
#
# The dangerous shape is specific and silent: the hosted CI parser contract compiles
# the .inc DIRECTLY, so CI stays green on the NEW parser while the linked wrf.exe can
# still contain the OLD one. Green CI would then certify a binary that does not
# contain the code it tested.
#
# Declared here rather than in depend.dyn_em because that file is generated and a
# regeneration would silently drop the line. make unions dependencies from all
# sources, so this survives.
module_initialize_ideal.o: bwave_pert_scale_parse.inc

# DO NOT DELETE
