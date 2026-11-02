WRF_SRC_ROOT_DIR=/frozen/wrf

#
DESCRIPTION     =       GNU ($SFC/$SCC): SDIRK-3 LibTorch Scheme
DMPARALLEL      =       1
OMPCPP          =       # -D_OPENMP
OMP             =       # -fopenmp
OMPCC           =       # -fopenmp
SFC             =       gfortran
SCC             =       gcc
CCOMP           =       gcc
CXX             =       g++
DM_FC           =       mpif90
DM_CC           =       mpicc
FC              =       time $(DM_FC)
CC              =       $(DM_CC) -DFSEEKO_OK 
# PR 9C.3 (P0-EH, measured): the gfortran/mpif90 final link CANNOT unwind
# C++ exceptions on this toolchain (a same-TU try/catch still terminates;
# see external/libtorch_wrf/sdirk3/eh_link_contract/). The final link is
# therefore driven by the C++ compiler, with the Fortran and MPI runtimes
# (from `mpif90 -show`) made explicit below, and LD_FCFLAGS overridden so
# Fortran-only compile flags never reach the C++ link driver.
LD              =       g++
LD_FCFLAGS      =      
RWORDSIZE       =       $(NATIVE_RWORDSIZE)
PROMOTION       =       #-fdefault-real-8
ARCH_LOCAL      =       -DNONSTANDARD_SYSTEM_SUBR -DMACOS  -DWRF_USE_CLM $(NETCDF4_IO_OPTS) -DUSE_SDIRK3 -DDM_PARALLEL
CFLAGS_LOCAL    =       -w -O3 -c -DMACOS -DRPC_TYPES=1 -DDM_PARALLEL -DUSE_SDIRK3 -I$(WRF_SRC_ROOT_DIR)/external/libtorch_wrf/sdirk3
CXXFLAGS        =       -std=c++17 -O3 -c -DMACOS -DDMPARALLEL -DUSE_SDIRK3 -I$(WRF_SRC_ROOT_DIR)/external/libtorch_wrf/sdirk3 -I/opt/homebrew/opt/pytorch/libexec/lib/python3.14/site-packages/torch/include -I/opt/homebrew/opt/pytorch/libexec/lib/python3.14/site-packages/torch/include/torch/csrc/api/include -I/opt/homebrew/include
LDFLAGS_LOCAL   =       -L/opt/homebrew/opt/pytorch/libexec/lib/python3.14/site-packages/torch/lib -ltorch -ltorch_cpu -lc10 -Wl,-rpath,/opt/homebrew/opt/pytorch/libexec/lib/python3.14/site-packages/torch/lib -L/opt/homebrew/opt/gcc/lib/gcc/current -lgfortran -lquadmath -Wl,-rpath,/opt/homebrew/opt/gcc/lib/gcc/current -L/opt/homebrew/opt/open-mpi/lib -lmpi_usempif08 -lmpi_usempi_ignore_tkr -lmpi_mpifh -lmpi -Wl,-rpath,/opt/homebrew/opt/open-mpi/lib
CPLUSPLUSLIB    =       -lc++
ESMF_LDFLAG     =       $(CPLUSPLUSLIB)
FCOPTIM         =       -O2 -ftree-vectorize -funroll-loops
FCREDUCEDOPT    =       $(FCOPTIM)
FCNOOPT         =       -O0
FCDEBUG         =       # -g $(FCNOOPT) # -fbacktrace -ggdb -fcheck=bounds,do,mem,pointer -ffpe-trap=invalid,zero,overflow
FORMAT_FIXED    =       -ffixed-form
FORMAT_FREE     =       -ffree-form -ffree-line-length-none
FCSUFFIX        =       
BYTESWAPIO      =       -fconvert=big-endian -frecord-marker=4
FCCOMPAT        =       -fallow-argument-mismatch -fallow-invalid-boz
FCBASEOPTS_NO_G =       -w $(FORMAT_FREE) $(BYTESWAPIO) $(FCCOMPAT) -DUSE_SDIRK3
FCBASEOPTS      =       $(FCBASEOPTS_NO_G) $(FCDEBUG)
MODULE_SRCH_FLAG =
TRADFLAG        =       -traditional-cpp $(NETCDF4_IO_OPTS) -DUSE_SDIRK3
CPP             =       cpp -P -nostdinc -xassembler-with-cpp
AR              =       ar
ARFLAGS         =       ru
M4              =       m4
RANLIB          =       ranlib
RLFLAGS         =
CC_TOOLS        =       $(SCC) 

# SDIRK3 and LibTorch settings will be configured dynamically

WRF_EM_CORE     =      1

# External library dependencies (to be built)
LIB_BUNDLED     =  $(NETCDF4_DEP_LIB)\
                     $(WRF_SRC_ROOT_DIR)/external/fftpack/fftpack5/libfftpack.a \
                     $(WRF_SRC_ROOT_DIR)/external/io_grib1/libio_grib1.a \
                     $(WRF_SRC_ROOT_DIR)/external/io_grib_share/libio_grib_share.a \
                     $(WRF_SRC_ROOT_DIR)/external/io_int/libwrfio_int.a \
                     $(ESMF_IO_LIB) \
                     $(WRF_SRC_ROOT_DIR)/external/RSL_LITE/librsl_lite.a \
                     $(WRF_SRC_ROOT_DIR)/external/libtorch_wrf/sdirk3/libwrf_sdirk3_libtorch.a \
                     $(WRF_SRC_ROOT_DIR)/frame/module_internal_header_util.o \
                     $(WRF_SRC_ROOT_DIR)/frame/pack_utils.o

# Libraries to be linked
LIB_EXTRA       =      $(CPLUSPLUSLIB) $(NETCDF4_DEP_LIB) $(NETCDF4_DEP_LIB)

#insert new stanza before the Fujitsu block, keep Fujitsu at the end of the list
###########################################################

all:
	@echo LDFLAGS=$(LDFLAGS_LOCAL)
	@echo LIB=$(LIB_BUNDLED)
