MODULE module_sdirk3_interface
   
   
   
   
   
   USE iso_c_binding
   
   IMPLICIT NONE
   PRIVATE

   
   PUBLIC :: sdirk3_solver_init
   PUBLIC :: sdirk3_solver_finalize
   PUBLIC :: sdirk3_solver_step
   PUBLIC :: sdirk3_tile_create
   PUBLIC :: sdirk3_tile_destroy
   PUBLIC :: sdirk3_get_solver_stats

   
   INTERFACE

      
      INTEGER(c_int) FUNCTION wrf_sdirk3_init_solver_with_maps( &
               nx, ny, nz, dx, dy, dt, &
               msftx, msfty, msfux, msfuy, msfvx, msfvy, &
               unified_physics_mode) BIND(C, name="wrf_sdirk3_init_solver_with_maps")
         USE iso_c_binding
         INTEGER(c_int), VALUE :: nx, ny, nz
         REAL(c_float), VALUE :: dx, dy, dt
         REAL(c_float), DIMENSION(*), INTENT(IN) :: msftx, msfty
         REAL(c_float), DIMENSION(*), INTENT(IN) :: msfux, msfuy
         REAL(c_float), DIMENSION(*), INTENT(IN) :: msfvx, msfvy
         INTEGER(c_int), VALUE :: unified_physics_mode
      END FUNCTION wrf_sdirk3_init_solver_with_maps

      
      SUBROUTINE wrf_sdirk3_finalize_solver() BIND(C, name="wrf_sdirk3_finalize_solver")
         USE iso_c_binding
      END SUBROUTINE wrf_sdirk3_finalize_solver

      
      INTEGER(c_int) FUNCTION wrf_sdirk3_solve_stage_ptr( &
               k_stage, u_prev, k_prev_ptr, stage_num, dt, nx, ny, nz) &
               BIND(C, name="wrf_sdirk3_solve_stage")
         USE iso_c_binding
         REAL(c_float), DIMENSION(*), INTENT(OUT) :: k_stage
         REAL(c_float), DIMENSION(*), INTENT(IN) :: u_prev
         TYPE(c_ptr), VALUE :: k_prev_ptr  
         INTEGER(c_int), VALUE :: stage_num
         REAL(c_float), VALUE :: dt
         INTEGER(c_int), VALUE :: nx, ny, nz
      END FUNCTION wrf_sdirk3_solve_stage_ptr

      
      TYPE(c_ptr) FUNCTION wrf_sdirk3_create_tile_solver_with_maps( &
               nx, ny, nz, dx, dy, dt, &
               its, ite, jts, jte, kts, kte, &
               msftx_tile, msfty_tile, msfux_tile, msfuy_tile, &
               msfvx_tile, msfvy_tile, &
               unified_physics_mode, tile_id) &
               BIND(C, name="wrf_sdirk3_create_tile_solver_with_maps")
         USE iso_c_binding
         INTEGER(c_int), VALUE :: nx, ny, nz
         REAL(c_float), VALUE :: dx, dy, dt
         INTEGER(c_int), VALUE :: its, ite, jts, jte, kts, kte
         REAL(c_float), DIMENSION(*), INTENT(IN) :: msftx_tile, msfty_tile
         REAL(c_float), DIMENSION(*), INTENT(IN) :: msfux_tile, msfuy_tile
         REAL(c_float), DIMENSION(*), INTENT(IN) :: msfvx_tile, msfvy_tile
         INTEGER(c_int), VALUE :: unified_physics_mode, tile_id
      END FUNCTION wrf_sdirk3_create_tile_solver_with_maps

      
      SUBROUTINE wrf_sdirk3_destroy_tile_solver(solver) &
               BIND(C, name="wrf_sdirk3_destroy_tile_solver")
         USE iso_c_binding
         TYPE(c_ptr), VALUE :: solver
      END SUBROUTINE wrf_sdirk3_destroy_tile_solver

      
      SUBROUTINE wrf_sdirk3_get_stats(newton_iters, gmres_iters, final_residual) &
               BIND(C, name="wrf_sdirk3_get_stats")
         USE iso_c_binding
         INTEGER(c_int), INTENT(OUT) :: newton_iters, gmres_iters
         REAL(c_float), INTENT(OUT) :: final_residual
      END SUBROUTINE wrf_sdirk3_get_stats

      

   END INTERFACE

CONTAINS

   
   
   
   
   SUBROUTINE sdirk3_solver_init(nx, ny, nz, dx, dy, dt, &
                                 msftx, msfty, msfux, msfuy, msfvx, msfvy, &
                                 unified_mode, ierr)
      IMPLICIT NONE
      
      INTEGER, INTENT(IN) :: nx, ny, nz
      REAL, INTENT(IN) :: dx, dy, dt
      REAL, DIMENSION(:,:), INTENT(IN) :: msftx, msfty
      REAL, DIMENSION(:,:), INTENT(IN) :: msfux, msfuy
      REAL, DIMENSION(:,:), INTENT(IN) :: msfvx, msfvy
      INTEGER, INTENT(IN) :: unified_mode
      INTEGER, INTENT(OUT) :: ierr
      
      ierr = wrf_sdirk3_init_solver_with_maps( &
               nx, ny, nz, REAL(dx,c_float), REAL(dy,c_float), REAL(dt,c_float), &
               REAL(msftx,c_float), REAL(msfty,c_float), &
               REAL(msfux,c_float), REAL(msfuy,c_float), &
               REAL(msfvx,c_float), REAL(msfvy,c_float), &
               unified_mode)
               
   END SUBROUTINE sdirk3_solver_init

   
   
   
   
   SUBROUTINE sdirk3_solver_finalize()
      IMPLICIT NONE
      CALL wrf_sdirk3_finalize_solver()
   END SUBROUTINE sdirk3_solver_finalize

   
   
   
   
   SUBROUTINE sdirk3_solver_step(k_stage, u_prev, k_prev, &
                                 stage_num, dt, nx, ny, nz, ierr)
      USE iso_c_binding
      IMPLICIT NONE
      
      REAL, DIMENSION(:,:,:,:), INTENT(OUT) :: k_stage
      REAL, DIMENSION(:,:,:,:), INTENT(IN) :: u_prev
      REAL, DIMENSION(:,:,:,:,:), INTENT(IN), OPTIONAL, TARGET :: k_prev
      INTEGER, INTENT(IN) :: stage_num
      REAL, INTENT(IN) :: dt
      INTEGER, INTENT(IN) :: nx, ny, nz
      INTEGER, INTENT(OUT) :: ierr
      
      
      TYPE(c_ptr) :: k_prev_ptr
      
      
      IF (PRESENT(k_prev)) THEN
         k_prev_ptr = C_LOC(k_prev(1,1,1,1,1))
      ELSE
         k_prev_ptr = C_NULL_PTR
      END IF
      
      
      ierr = wrf_sdirk3_solve_stage_ptr( &
               k_stage, u_prev, k_prev_ptr, &
               stage_num, REAL(dt,c_float), &
               nx, ny, nz)
               
   END SUBROUTINE sdirk3_solver_step

   
   
   
   
   FUNCTION sdirk3_tile_create(nx, ny, nz, dx, dy, dt, &
                              its, ite, jts, jte, kts, kte, &
                              msftx_tile, msfty_tile, &
                              msfux_tile, msfuy_tile, &
                              msfvx_tile, msfvy_tile, &
                              unified_mode, tile_id) RESULT(solver_ptr)
      IMPLICIT NONE
      
      INTEGER, INTENT(IN) :: nx, ny, nz
      REAL, INTENT(IN) :: dx, dy, dt
      INTEGER, INTENT(IN) :: its, ite, jts, jte, kts, kte
      REAL, DIMENSION(its:ite,jts:jte), INTENT(IN) :: msftx_tile, msfty_tile
      REAL, DIMENSION(its:ite,jts:jte), INTENT(IN) :: msfux_tile, msfuy_tile
      REAL, DIMENSION(its:ite,jts:jte), INTENT(IN) :: msfvx_tile, msfvy_tile
      INTEGER, INTENT(IN) :: unified_mode, tile_id
      TYPE(c_ptr) :: solver_ptr
      
      solver_ptr = wrf_sdirk3_create_tile_solver_with_maps( &
                     nx, ny, nz, REAL(dx,c_float), REAL(dy,c_float), REAL(dt,c_float), &
                     its, ite, jts, jte, kts, kte, &
                     REAL(msftx_tile,c_float), REAL(msfty_tile,c_float), &
                     REAL(msfux_tile,c_float), REAL(msfuy_tile,c_float), &
                     REAL(msfvx_tile,c_float), REAL(msfvy_tile,c_float), &
                     unified_mode, tile_id)
                     
   END FUNCTION sdirk3_tile_create

   
   
   
   
   SUBROUTINE sdirk3_tile_destroy(solver_ptr)
      IMPLICIT NONE
      TYPE(c_ptr), INTENT(IN) :: solver_ptr
      CALL wrf_sdirk3_destroy_tile_solver(solver_ptr)
   END SUBROUTINE sdirk3_tile_destroy

   
   
   
   
   SUBROUTINE sdirk3_get_solver_stats(newton_iters, gmres_iters, final_residual)
      IMPLICIT NONE
      INTEGER, INTENT(OUT) :: newton_iters, gmres_iters
      REAL, INTENT(OUT) :: final_residual
      REAL(c_float) :: c_residual
      
      CALL wrf_sdirk3_get_stats(newton_iters, gmres_iters, c_residual)
      final_residual = REAL(c_residual)
      
   END SUBROUTINE sdirk3_get_solver_stats

END MODULE module_sdirk3_interface

