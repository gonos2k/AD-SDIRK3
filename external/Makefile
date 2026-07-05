

# SDIRK-3 LibTorch library build target
libtorch_wrf sdirk3_libtorch :
	@echo "Building SDIRK-3 LibTorch library..."
	@( cd libtorch_wrf/sdirk3 ; \
	  if [ ! -f libwrf_sdirk3_libtorch.a ] ; then \
	    echo "Library not found, building..." ; \
	    $(MAKE) ; \
	  else \
	    echo "Library already exists, skipping build" ; \
	  fi )

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


