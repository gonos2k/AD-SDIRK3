

# SDIRK-3 LibTorch library (external review round 3c):
# The library is ACTUALLY built by the `sdirk3_libtorch` target in configure.wrf,
# which recurses into libtorch_wrf/sdirk3 UNCONDITIONALLY with the configured
# FC/CC/CXX flags — the sub-make's own dependencies decide what rebuilds.
# The legacy target here was doubly hazardous and never ran:
#   (1) the `libtorch_wrf` DIRECTORY shadowed the target name, so make reported
#       "up to date" and skipped the recipe entirely (verified in compile logs);
#   (2) had it run, its archive-exists guard would have skipped rebuilding
#       edited sources (stale-archive), and its flag-less $(MAKE) could have
#       produced object files with the wrong flags that later flag-correct
#       builds would not recompile.
# Kept as an explicit .PHONY no-op so `make libtorch_wrf` can never silently
# produce a stale or wrong-flags build.
.PHONY: libtorch_wrf sdirk3_libtorch
libtorch_wrf sdirk3_libtorch :
	@echo "SDIRK-3 LibTorch library is built unconditionally by the 'sdirk3_libtorch' target in configure.wrf; nothing to do here."

superclean :
	@( cd esmf_time_f90 ;  make -s superclean )
	@( cd io_pnetcdf ;  make -s superclean )
	@( cd io_pio ;  make -s superclean )
	@( cd io_int ; make -s superclean )
	@( cd io_netcdf ;  make -s superclean )
	@( cd io_netcdfpar ;  make -s superclean )
	@( cd io_adios2 ;  make -s superclean )
	@( cd io_phdf5 ; make -s superclean )
	@( cd io_grib1 ; make -s superclean )
	@( cd io_grib_share ; make -s superclean )
	@( cd io_grib2 ; make -s superclean )
	@( cd io_esmf ;  make -s superclean )
	@( cd ioapi_share ;  make -s superclean )
	@( cd RSL_LITE ; make -s superclean )
	@( cd fftpack/fftpack5 ; make -s superclean )
	@( cd atm_ocn ; make -s superclean )
	@( cd libtorch_wrf ; make -s clean )


