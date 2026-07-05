














MODULE module_dm

   USE module_machine
   USE module_wrf_error
   USE module_driver_constants


   USE module_cpl, ONLY : coupler_on, cpl_init


   IMPLICIT NONE

   INCLUDE 'mpif.h'


   INTEGER, PARAMETER :: max_halo_width = 6 

   INTEGER :: ips_save, ipe_save, jps_save, jpe_save, itrace
   INTEGER :: lats_to_mic, minx, miny

   INTEGER :: communicator_stack_cursor = 0
   INTEGER :: current_id  = 1
   INTEGER, DIMENSION(max_domains) ::  ntasks_stack, ntasks_y_stack          &
                                     , ntasks_x_stack, mytask_stack          &
                                     , mytask_x_stack, mytask_y_stack        &
                                     , id_stack                            
   INTEGER, DIMENSION(max_domains) ::  ntasks_store, ntasks_y_store          &
                                     , ntasks_x_store, mytask_store          &
                                     , mytask_x_store, mytask_y_store      
   INTEGER ntasks, ntasks_y, ntasks_x, mytask, mytask_x, mytask_y

   INTEGER, DIMENSION(max_domains) :: local_communicator_stack, local_communicator_periodic_stack &
                                     ,local_iocommunicator_stack                                  &
                                     ,local_communicator_x_stack, local_communicator_y_stack
   INTEGER, DIMENSION(max_domains) :: local_communicator_store, local_communicator_periodic_store &
                                     ,local_iocommunicator_store                                  &
                                     ,local_communicator_x_store, local_communicator_y_store

   INTEGER :: mpi_comm_allcompute         = MPI_UNDEFINED
   INTEGER :: local_communicator          = MPI_UNDEFINED
   INTEGER :: local_communicator_periodic = MPI_UNDEFINED
   INTEGER :: local_iocommunicator        = MPI_UNDEFINED
   INTEGER :: local_communicator_x        = MPI_UNDEFINED
   INTEGER :: local_communicator_y        = MPI_UNDEFINED 
   INTEGER :: local_quilt_comm            = MPI_UNDEFINED 
   LOGICAL :: dm_debug_flag = .FALSE.

   INTEGER intercomm_to_mom( max_domains ), intercomm_to_kid( max_nests, max_domains )
   INTEGER mpi_comm_to_mom( max_domains ), mpi_comm_to_kid( max_nests, max_domains )
   INTEGER which_kid(max_domains), nkids(max_domains)
   INTEGER nest_task_offsets(max_domains)
   LOGICAL intercomm_active( max_domains )
   LOGICAL domain_active_this_task( max_domains )

   INTEGER tasks_per_split
   INTEGER comm_start(max_domains)   




   INTEGER nest_pes_x(max_domains)   
   INTEGER nest_pes_y(max_domains)   
   INTEGER comms_i_am_in (max_domains)  
   INTEGER loc_comm(max_domains)
   LOGICAL poll_servers
   INTEGER nio_tasks_per_group(max_domains), nio_groups, num_io_tasks
   NAMELIST /dm_task_split/ tasks_per_split, comm_start, nest_pes_x, nest_pes_y
   NAMELIST /namelist_quilt/ nio_tasks_per_group, nio_groups, poll_servers




   INTERFACE wrf_dm_maxval

     MODULE PROCEDURE wrf_dm_maxval_real , wrf_dm_maxval_integer, wrf_dm_maxval_doubleprecision

   END INTERFACE

   INTERFACE wrf_dm_minval                       

     MODULE PROCEDURE wrf_dm_minval_real , wrf_dm_minval_integer, wrf_dm_minval_doubleprecision

   END INTERFACE

CONTAINS

   SUBROUTINE MPASPECT( P, MINM, MINN, PROCMIN_M, PROCMIN_N )
      IMPLICIT NONE
      INTEGER P, M, N, MINI, MINM, MINN, PROCMIN_M, PROCMIN_N
      MINI = 2*P
      MINM = 1
      MINN = P
      DO M = 1, P
        IF ( MOD( P, M ) .EQ. 0 ) THEN
          N = P / M
          IF ( ABS(M-N) .LT. MINI                &
               .AND. M .GE. PROCMIN_M            &
               .AND. N .GE. PROCMIN_N            &
             ) THEN
            MINI = ABS(M-N)
            MINM = M
            MINN = N
          END IF
        END IF
      END DO
      IF ( MINM .LT. PROCMIN_M .OR. MINN .LT. PROCMIN_N ) THEN
        WRITE( wrf_err_message , * )'MPASPECT: UNABLE TO GENERATE PROCESSOR MESH.  STOPPING.'
        CALL wrf_message ( TRIM ( wrf_err_message ) )
        WRITE( wrf_err_message , * )' PROCMIN_M ', PROCMIN_M
        CALL wrf_message ( TRIM ( wrf_err_message ) )
        WRITE( wrf_err_message , * )' PROCMIN_N ', PROCMIN_N
        CALL wrf_message ( TRIM ( wrf_err_message ) )
        WRITE( wrf_err_message , * )' P         ', P
        CALL wrf_message ( TRIM ( wrf_err_message ) )
        WRITE( wrf_err_message , * )' MINM      ', MINM
        CALL wrf_message ( TRIM ( wrf_err_message ) )
        WRITE( wrf_err_message , * )' MINN      ', MINN
        CALL wrf_message ( TRIM ( wrf_err_message ) )
        CALL wrf_error_fatal3("<stdin>",134,&
'module_dm: mpaspect' )
      END IF
   RETURN
   END SUBROUTINE MPASPECT

   SUBROUTINE compute_mesh( ntasks , ntasks_x, ntasks_y )
     IMPLICIT NONE
     INTEGER, INTENT(IN)  :: ntasks
     INTEGER, INTENT(OUT) :: ntasks_x, ntasks_y
     INTEGER lats_to_mic
     CALL nl_get_nproc_x ( 1, ntasks_x )
     CALL nl_get_nproc_y ( 1, ntasks_y )
     CALL nl_get_lats_to_mic ( 1, lats_to_mic )

     IF ( ntasks_x .GT. 0 .OR. ntasks_y .GT. 0 ) THEN
       
       IF      ( ntasks_x .GT. 0 .AND. ntasks_y .EQ. -1 ) THEN
         ntasks_y = ntasks / ntasks_x
       
       ELSE IF ( ntasks_x .EQ. -1 .AND. ntasks_y .GT. 0 ) THEN
         ntasks_x = ntasks / ntasks_y
       END IF

       IF ( ntasks_x * ntasks_y .NE. ntasks ) THEN
         WRITE( wrf_err_message , * )'WRF_DM_INITIALIZE (RSL_LITE): nproc_x * nproc_y in namelist ne ',ntasks
         CALL wrf_error_fatal3("<stdin>",160,&
wrf_err_message )
       END IF
     ELSE IF ( lats_to_mic .GT. 0 ) THEN
       ntasks_x = ntasks / 2
       ntasks_y = 2
       IF ( ntasks_x * ntasks_y .NE. ntasks ) THEN
         WRITE( wrf_err_message , * )&
           'WRF_DM_INITIALIZE (lats_to_mic > 0) nproc_x (',ntasks_x,')* nproc_y (',ntasks_y,&
           ') in namelist ne ',ntasks
         CALL wrf_error_fatal3("<stdin>",170,&
wrf_err_message )
       END IF
     ELSE
       
       
       
       CALL mpaspect ( ntasks, ntasks_x, ntasks_y, 1, 1 )
     END IF
     ntasks_store(1) = ntasks
     ntasks_x_store(1) = ntasks_x
     ntasks_y_store(1) = ntasks_y
   END SUBROUTINE compute_mesh

   SUBROUTINE wrf_dm_initialize
      IMPLICIT NONE

      INTEGER :: local_comm_per, local_comm_x, local_comm_y, local_comm2, new_local_comm, group, newgroup, p, p1, ierr,itmp
      INTEGER, ALLOCATABLE, DIMENSION(:) :: ranks
      INTEGER comdup
      INTEGER, DIMENSION(2) :: dims, coords
      LOGICAL, DIMENSION(2) :: isperiodic
      LOGICAL :: reorder_mesh

      CALL instate_communicators_for_domain(1)

      CALL wrf_get_dm_communicator ( new_local_comm )
      dims(1) = nest_pes_y(1)  
      dims(2) = nest_pes_x(1)  
      isperiodic(1) = .true.
      isperiodic(2) = .true.
      CALL mpi_cart_create( new_local_comm, 2, dims, isperiodic, .false., local_comm_per, ierr )
      local_communicator_periodic_store(1) = local_comm_per

      local_communicator_periodic_store = local_comm_per
      local_communicator_periodic = local_comm_per


      CALL nl_set_nproc_x ( 1, ntasks_x )
      CALL nl_set_nproc_y ( 1, ntasks_y )
      WRITE( wrf_err_message , * )'Ntasks in X ',ntasks_x,', ntasks in Y ',ntasks_y
      CALL wrf_message( wrf_err_message )

      CALL MPI_BARRIER( local_communicator, ierr )

      RETURN
   END SUBROUTINE wrf_dm_initialize

   SUBROUTINE get_dm_max_halo_width( id, width )
     IMPLICIT NONE
     INTEGER, INTENT(IN) :: id
     INTEGER, INTENT(OUT) :: width
     IF ( id .EQ. 1 ) THEN   
       width = max_halo_width
     ELSE
       width = max_halo_width + 3
     END IF
     RETURN
   END SUBROUTINE get_dm_max_halo_width

   SUBROUTINE patch_domain_rsl_lite( id  , parent, parent_id, &
                                sd1 , ed1 , sp1 , ep1 , sm1 , em1 ,        &
                                sd2 , ed2 , sp2 , ep2 , sm2 , em2 ,        &
                                sd3 , ed3 , sp3 , ep3 , sm3 , em3 ,        &
                                      sp1x , ep1x , sm1x , em1x , &
                                      sp2x , ep2x , sm2x , em2x , &
                                      sp3x , ep3x , sm3x , em3x , &
                                      sp1y , ep1y , sm1y , em1y , &
                                      sp2y , ep2y , sm2y , em2y , &
                                      sp3y , ep3y , sm3y , em3y , &
                                bdx , bdy )


      USE module_domain, ONLY : domain, head_grid, find_grid_by_id, alloc_space_field


      IMPLICIT NONE
      INTEGER, INTENT(IN)   :: sd1 , ed1 , sd2 , ed2 , sd3 , ed3 , bdx , bdy
      INTEGER, INTENT(OUT)  :: sp1 , ep1 , sp2 , ep2 , sp3 , ep3 , &
                               sm1 , em1 , sm2 , em2 , sm3 , em3
      INTEGER, INTENT(OUT)  :: sp1x , ep1x , sp2x , ep2x , sp3x , ep3x , &
                               sm1x , em1x , sm2x , em2x , sm3x , em3x
      INTEGER, INTENT(OUT)  :: sp1y , ep1y , sp2y , ep2y , sp3y , ep3y , &
                               sm1y , em1y , sm2y , em2y , sm3y , em3y
      INTEGER, INTENT(IN)   :: id, parent_id
      TYPE(domain),POINTER  :: parent


      INTEGER               :: ids, ide, jds, jde, kds, kde
      INTEGER               :: ims, ime, jms, jme, kms, kme
      INTEGER               :: ips, ipe, jps, jpe, kps, kpe
      INTEGER               :: imsx, imex, jmsx, jmex, kmsx, kmex
      INTEGER               :: ipsx, ipex, jpsx, jpex, kpsx, kpex
      INTEGER               :: imsy, imey, jmsy, jmey, kmsy, kmey
      INTEGER               :: ipsy, ipey, jpsy, jpey, kpsy, kpey

      INTEGER               :: c_sd1 , c_ed1 , c_sd2 , c_ed2 , c_sd3 , c_ed3
      INTEGER               :: c_sp1 , c_ep1 , c_sp2 , c_ep2 , c_sp3 , c_ep3 , &
                               c_sm1 , c_em1 , c_sm2 , c_em2 , c_sm3 , c_em3
      INTEGER               :: c_sp1x , c_ep1x , c_sp2x , c_ep2x , c_sp3x , c_ep3x , &
                               c_sm1x , c_em1x , c_sm2x , c_em2x , c_sm3x , c_em3x
      INTEGER               :: c_sp1y , c_ep1y , c_sp2y , c_ep2y , c_sp3y , c_ep3y , &
                               c_sm1y , c_em1y , c_sm2y , c_em2y , c_sm3y , c_em3y

      INTEGER               :: c_ids, c_ide, c_jds, c_jde, c_kds, c_kde
      INTEGER               :: c_ims, c_ime, c_jms, c_jme, c_kms, c_kme
      INTEGER               :: c_ips, c_ipe, c_jps, c_jpe, c_kps, c_kpe

      INTEGER               :: idim , jdim , kdim , rem , a, b
      INTEGER               :: i, j, ni, nj, Px, Py, P

      INTEGER               :: parent_grid_ratio, i_parent_start, j_parent_start
      INTEGER               :: shw
      INTEGER               :: idim_cd, jdim_cd, ierr
      INTEGER               :: max_dom



      TYPE(domain), POINTER :: intermediate_grid
      TYPE(domain), POINTER  :: nest_grid
      CHARACTER*256   :: mess

      INTEGER parent_max_halo_width
      INTEGER thisdomain_max_halo_width
      INTEGER lats_to_mic

     lats_to_mic=0
     CALL nl_get_lats_to_mic( 1, lats_to_mic )
      IF ( lats_to_mic .GT. 0 ) THEN
        minx = -99  
        miny = lats_to_mic  
      ELSE
        minx = 1   
        miny = 1   
      END IF



      SELECT CASE ( model_data_order )
         
         CASE ( DATA_ORDER_ZXY )
            ids = sd2 ; ide = ed2
            jds = sd3 ; jde = ed3
            kds = sd1 ; kde = ed1
         CASE ( DATA_ORDER_XYZ )
            ids = sd1 ; ide = ed1
            jds = sd2 ; jde = ed2
            kds = sd3 ; kde = ed3
         CASE ( DATA_ORDER_XZY )
            ids = sd1 ; ide = ed1
            jds = sd3 ; jde = ed3
            kds = sd2 ; kde = ed2
         CASE ( DATA_ORDER_YXZ)
            ids = sd2 ; ide = ed2
            jds = sd1 ; jde = ed1
            kds = sd3 ; kde = ed3
      END SELECT

      CALL nl_get_max_dom( 1 , max_dom )

      CALL get_dm_max_halo_width( id , thisdomain_max_halo_width )
      IF ( id .GT. 1 ) THEN
        CALL get_dm_max_halo_width( parent%id , parent_max_halo_width )
      END IF

      CALL compute_memory_dims_rsl_lite ( id, thisdomain_max_halo_width, 0 , bdx, bdy,   &
                   ids,  ide,  jds,  jde,  kds,  kde, &
                   ims,  ime,  jms,  jme,  kms,  kme, &
                   imsx, imex, jmsx, jmex, kmsx, kmex, &
                   imsy, imey, jmsy, jmey, kmsy, kmey, &
                   ips,  ipe,  jps,  jpe,  kps,  kpe, &
                   ipsx, ipex, jpsx, jpex, kpsx, kpex, &
                   ipsy, ipey, jpsy, jpey, kpsy, kpey )

     
     
     
     

      IF ( id .GT. 1 ) THEN
         CALL nl_get_parent_grid_ratio( id, parent_grid_ratio )
         if ( mod(ime,parent_grid_ratio) .NE. 0 ) ime = ime + parent_grid_ratio - mod(ime,parent_grid_ratio)
         if ( mod(jme,parent_grid_ratio) .NE. 0 ) jme = jme + parent_grid_ratio - mod(jme,parent_grid_ratio)
      END IF

      SELECT CASE ( model_data_order )
         CASE ( DATA_ORDER_ZXY )
            sp2 = ips ; ep2 = ipe ; sm2 = ims ; em2 = ime
            sp3 = jps ; ep3 = jpe ; sm3 = jms ; em3 = jme
            sp1 = kps ; ep1 = kpe ; sm1 = kms ; em1 = kme
            sp2x = ipsx ; ep2x = ipex ; sm2x = imsx ; em2x = imex
            sp3x = jpsx ; ep3x = jpex ; sm3x = jmsx ; em3x = jmex
            sp1x = kpsx ; ep1x = kpex ; sm1x = kmsx ; em1x = kmex
            sp2y = ipsy ; ep2y = ipey ; sm2y = imsy ; em2y = imey
            sp3y = jpsy ; ep3y = jpey ; sm3y = jmsy ; em3y = jmey
            sp1y = kpsy ; ep1y = kpey ; sm1y = kmsy ; em1y = kmey
         CASE ( DATA_ORDER_ZYX )
            sp3 = ips ; ep3 = ipe ; sm3 = ims ; em3 = ime
            sp2 = jps ; ep2 = jpe ; sm2 = jms ; em2 = jme
            sp1 = kps ; ep1 = kpe ; sm1 = kms ; em1 = kme
            sp3x = ipsx ; ep3x = ipex ; sm3x = imsx ; em3x = imex
            sp2x = jpsx ; ep2x = jpex ; sm2x = jmsx ; em2x = jmex
            sp1x = kpsx ; ep1x = kpex ; sm1x = kmsx ; em1x = kmex
            sp3y = ipsy ; ep3y = ipey ; sm3y = imsy ; em3y = imey
            sp2y = jpsy ; ep2y = jpey ; sm2y = jmsy ; em2y = jmey
            sp1y = kpsy ; ep1y = kpey ; sm1y = kmsy ; em1y = kmey
         CASE ( DATA_ORDER_XYZ )
            sp1 = ips ; ep1 = ipe ; sm1 = ims ; em1 = ime
            sp2 = jps ; ep2 = jpe ; sm2 = jms ; em2 = jme
            sp3 = kps ; ep3 = kpe ; sm3 = kms ; em3 = kme
            sp1x = ipsx ; ep1x = ipex ; sm1x = imsx ; em1x = imex
            sp2x = jpsx ; ep2x = jpex ; sm2x = jmsx ; em2x = jmex
            sp3x = kpsx ; ep3x = kpex ; sm3x = kmsx ; em3x = kmex
            sp1y = ipsy ; ep1y = ipey ; sm1y = imsy ; em1y = imey
            sp2y = jpsy ; ep2y = jpey ; sm2y = jmsy ; em2y = jmey
            sp3y = kpsy ; ep3y = kpey ; sm3y = kmsy ; em3y = kmey
         CASE ( DATA_ORDER_YXZ)
            sp2 = ips ; ep2 = ipe ; sm2 = ims ; em2 = ime
            sp1 = jps ; ep1 = jpe ; sm1 = jms ; em1 = jme
            sp3 = kps ; ep3 = kpe ; sm3 = kms ; em3 = kme
            sp2x = ipsx ; ep2x = ipex ; sm2x = imsx ; em2x = imex
            sp1x = jpsx ; ep1x = jpex ; sm1x = jmsx ; em1x = jmex
            sp3x = kpsx ; ep3x = kpex ; sm3x = kmsx ; em3x = kmex
            sp2y = ipsy ; ep2y = ipey ; sm2y = imsy ; em2y = imey
            sp1y = jpsy ; ep1y = jpey ; sm1y = jmsy ; em1y = jmey
            sp3y = kpsy ; ep3y = kpey ; sm3y = kmsy ; em3y = kmey
         CASE ( DATA_ORDER_XZY )
            sp1 = ips ; ep1 = ipe ; sm1 = ims ; em1 = ime
            sp3 = jps ; ep3 = jpe ; sm3 = jms ; em3 = jme
            sp2 = kps ; ep2 = kpe ; sm2 = kms ; em2 = kme
            sp1x = ipsx ; ep1x = ipex ; sm1x = imsx ; em1x = imex
            sp3x = jpsx ; ep3x = jpex ; sm3x = jmsx ; em3x = jmex
            sp2x = kpsx ; ep2x = kpex ; sm2x = kmsx ; em2x = kmex
            sp1y = ipsy ; ep1y = ipey ; sm1y = imsy ; em1y = imey
            sp3y = jpsy ; ep3y = jpey ; sm3y = jmsy ; em3y = jmey
            sp2y = kpsy ; ep2y = kpey ; sm2y = kmsy ; em2y = kmey
         CASE ( DATA_ORDER_YZX )
            sp3 = ips ; ep3 = ipe ; sm3 = ims ; em3 = ime
            sp1 = jps ; ep1 = jpe ; sm1 = jms ; em1 = jme
            sp2 = kps ; ep2 = kpe ; sm2 = kms ; em2 = kme
            sp3x = ipsx ; ep3x = ipex ; sm3x = imsx ; em3x = imex
            sp1x = jpsx ; ep1x = jpex ; sm1x = jmsx ; em1x = jmex
            sp2x = kpsx ; ep2x = kpex ; sm2x = kmsx ; em2x = kmex
            sp3y = ipsy ; ep3y = ipey ; sm3y = imsy ; em3y = imey
            sp1y = jpsy ; ep1y = jpey ; sm1y = jmsy ; em1y = jmey
            sp2y = kpsy ; ep2y = kpey ; sm2y = kmsy ; em2y = kmey
      END SELECT

      IF ( id.EQ.1 ) THEN
         WRITE(wrf_err_message,*)'*************************************'
         CALL wrf_message( TRIM(wrf_err_message) )
         WRITE(wrf_err_message,*)'Parent domain'
         CALL wrf_message( TRIM(wrf_err_message) )
         WRITE(wrf_err_message,*)'ids,ide,jds,jde ',ids,ide,jds,jde
         CALL wrf_message( TRIM(wrf_err_message) )
         WRITE(wrf_err_message,*)'ims,ime,jms,jme ',ims,ime,jms,jme
         CALL wrf_message( TRIM(wrf_err_message) )
         WRITE(wrf_err_message,*)'ips,ipe,jps,jpe ',ips,ipe,jps,jpe
         CALL wrf_message( TRIM(wrf_err_message) )
         WRITE(wrf_err_message,*)'*************************************'
         CALL wrf_message( TRIM(wrf_err_message) )
      END IF

      IF ( id .GT. 1 ) THEN

         CALL nl_get_shw( id, shw )
         CALL nl_get_i_parent_start( id , i_parent_start )
         CALL nl_get_j_parent_start( id , j_parent_start )
         CALL nl_get_parent_grid_ratio( id, parent_grid_ratio )

         SELECT CASE ( model_data_order )
            CASE ( DATA_ORDER_ZXY )
               idim = ed2-sd2+1
               jdim = ed3-sd3+1
               kdim = ed1-sd1+1
               c_kds = sd1                ; c_kde = ed1
            CASE ( DATA_ORDER_ZYX )
               idim = ed3-sd3+1
               jdim = ed2-sd2+1
               kdim = ed1-sd1+1
               c_kds = sd1                ; c_kde = ed1
            CASE ( DATA_ORDER_XYZ )
               idim = ed1-sd1+1
               jdim = ed2-sd2+1
               kdim = ed3-sd3+1
               c_kds = sd3                ; c_kde = ed3
            CASE ( DATA_ORDER_YXZ)
               idim = ed2-sd2+1
               jdim = ed1-sd1+1
               kdim = ed3-sd3+1
               c_kds = sd3                ; c_kde = ed3
            CASE ( DATA_ORDER_XZY )
               idim = ed1-sd1+1
               jdim = ed3-sd3+1
               kdim = ed2-sd2+1
               c_kds = sd2                ; c_kde = ed2
            CASE ( DATA_ORDER_YZX )
               idim = ed3-sd3+1
               jdim = ed1-sd1+1
               kdim = ed2-sd2+1
               c_kds = sd2                ; c_kde = ed2
         END SELECT

         idim_cd = idim / parent_grid_ratio + 1 + 2*shw + 1
         jdim_cd = jdim / parent_grid_ratio + 1 + 2*shw + 1

         c_ids = i_parent_start-shw ; c_ide = c_ids + idim_cd - 1
         c_jds = j_parent_start-shw ; c_jde = c_jds + jdim_cd - 1


         
         

         c_ips = -1
         nj = ( c_jds - j_parent_start ) * parent_grid_ratio + 1 + 1 ;
         ierr = 0
         DO i = c_ids, c_ide
            ni = ( i - i_parent_start ) * parent_grid_ratio + 1 + 1 ;

            CALL task_for_point ( ni, nj, ids, ide, jds, jde, nest_pes_x(id), nest_pes_y(id),Px,Py, &
                                  minx, miny,  ierr )
            IF ( ierr .NE. 0 ) CALL wrf_error_fatal3("<stdin>",491,&
'error code returned by task_for_point in module_dm.F (a)')
            IF ( Px .EQ. mytask_x ) THEN
               c_ipe = i
               IF ( c_ips .EQ. -1 ) c_ips = i
            END IF
         END DO
         IF ( ierr .NE. 0 ) THEN
            CALL tfp_message("<stdin>",528)
         END IF
         IF (c_ips .EQ. -1 ) THEN
            c_ipe = -1
            c_ips = 0
         END IF

         c_jps = -1
         ni = ( c_ids - i_parent_start ) * parent_grid_ratio + 1 + 1 ;
         ierr = 0
         DO j = c_jds, c_jde
            nj = ( j - j_parent_start ) * parent_grid_ratio + 1 + 1 ;

            CALL task_for_point ( ni, nj, ids, ide, jds, jde, nest_pes_x(id), nest_pes_y(id), Px, Py, &
                                  minx, miny, ierr )
            IF ( ierr .NE. 0 ) CALL wrf_error_fatal3("<stdin>",514,&
'error code returned by task_for_point in module_dm.F (b)')


            IF ( Py .EQ. mytask_y ) THEN
               c_jpe = j
               IF ( c_jps .EQ. -1 ) c_jps = j
            END IF
         END DO
         IF ( ierr .NE. 0 ) THEN
            CALL tfp_message("<stdin>",552)
         END IF
         IF (c_jps .EQ. -1 ) THEN
            c_jpe = -1
            c_jps = 0
         END IF




         IF ( c_ips <= c_ipe ) THEN

           IF ( mytask_x .EQ. 0 ) THEN
             c_ips = c_ips - shw

           END IF

           IF ( mytask_x .EQ. nest_pes_x(id)-1 ) THEN
             c_ipe = c_ipe + shw

           END IF
           c_ims = max( c_ips - max(shw,thisdomain_max_halo_width), c_ids - bdx ) - 1
           c_ime = min( c_ipe + max(shw,thisdomain_max_halo_width), c_ide + bdx ) + 1
         ELSE
           c_ims = 0
           c_ime = 0
         END IF



         IF ( c_jps <= c_jpe ) THEN

           IF ( mytask_y .EQ. 0 ) THEN
              c_jps = c_jps - shw

           END IF

           IF ( mytask_y .EQ. nest_pes_y(id)-1 ) THEN
              c_jpe = c_jpe + shw

           END IF
           c_jms = max( c_jps - max(shw,thisdomain_max_halo_width), c_jds - bdx ) - 1
           c_jme = min( c_jpe + max(shw,thisdomain_max_halo_width), c_jde + bdx ) + 1

         ELSE
           c_jms = 0
           c_jme = 0
         END IF
         c_kps = 1
         c_kpe = c_kde
         c_kms = 1
         c_kme = c_kde


         c_sm1x = 1 ; c_em1x = 1 ; c_sm2x = 1 ; c_em2x = 1 ; c_sm3x = 1 ; c_em3x = 1
         c_sm1y = 1 ; c_em1y = 1 ; c_sm2y = 1 ; c_em2y = 1 ; c_sm3y = 1 ; c_em3y = 1



         WRITE(wrf_err_message,*)'*************************************'
         CALL wrf_message( TRIM(wrf_err_message) )
         WRITE(wrf_err_message,*)'Nesting domain'
         CALL wrf_message( TRIM(wrf_err_message) )
         WRITE(wrf_err_message,*)'ids,ide,jds,jde ',ids,ide,jds,jde
         CALL wrf_message( TRIM(wrf_err_message) )
         WRITE(wrf_err_message,*)'ims,ime,jms,jme ',ims,ime,jms,jme
         CALL wrf_message( TRIM(wrf_err_message) )
         WRITE(wrf_err_message,*)'ips,ipe,jps,jpe ',ips,ipe,jps,jpe
         CALL wrf_message( TRIM(wrf_err_message) )
         WRITE(wrf_err_message,*)'INTERMEDIATE domain'
         CALL wrf_message( TRIM(wrf_err_message) )
         WRITE(wrf_err_message,*)'ids,ide,jds,jde ',c_ids,c_ide,c_jds,c_jde
         CALL wrf_message( TRIM(wrf_err_message) )
         WRITE(wrf_err_message,*)'ims,ime,jms,jme ',c_ims,c_ime,c_jms,c_jme
         CALL wrf_message( TRIM(wrf_err_message) )
         WRITE(wrf_err_message,*)'ips,ipe,jps,jpe ',c_ips,c_ipe,c_jps,c_jpe
         CALL wrf_message( TRIM(wrf_err_message) )
         WRITE(wrf_err_message,*)'*************************************'
         CALL wrf_message( TRIM(wrf_err_message) )

         SELECT CASE ( model_data_order )
            CASE ( DATA_ORDER_ZXY )
               c_sd2 = c_ids ; c_ed2 = c_ide ; c_sp2 = c_ips ; c_ep2 = c_ipe ; c_sm2 = c_ims ; c_em2 = c_ime
               c_sd3 = c_jds ; c_ed3 = c_jde ; c_sp3 = c_jps ; c_ep3 = c_jpe ; c_sm3 = c_jms ; c_em3 = c_jme
               c_sd1 = c_kds ; c_ed1 = c_kde ; c_sp1 = c_kps ; c_ep1 = c_kpe ; c_sm1 = c_kms ; c_em1 = c_kme
            CASE ( DATA_ORDER_ZYX )
               c_sd3 = c_ids ; c_ed3 = c_ide ; c_sp3 = c_ips ; c_ep3 = c_ipe ; c_sm3 = c_ims ; c_em3 = c_ime
               c_sd2 = c_jds ; c_ed2 = c_jde ; c_sp2 = c_jps ; c_ep2 = c_jpe ; c_sm2 = c_jms ; c_em2 = c_jme
               c_sd1 = c_kds ; c_ed1 = c_kde ; c_sp1 = c_kps ; c_ep1 = c_kpe ; c_sm1 = c_kms ; c_em1 = c_kme
            CASE ( DATA_ORDER_XYZ )
               c_sd1 = c_ids ; c_ed1 = c_ide ; c_sp1 = c_ips ; c_ep1 = c_ipe ; c_sm1 = c_ims ; c_em1 = c_ime
               c_sd2 = c_jds ; c_ed2 = c_jde ; c_sp2 = c_jps ; c_ep2 = c_jpe ; c_sm2 = c_jms ; c_em2 = c_jme
               c_sd3 = c_kds ; c_ed3 = c_kde ; c_sp3 = c_kps ; c_ep3 = c_kpe ; c_sm3 = c_kms ; c_em3 = c_kme
            CASE ( DATA_ORDER_YXZ)
               c_sd2 = c_ids ; c_ed2 = c_ide ; c_sp2 = c_ips ; c_ep2 = c_ipe ; c_sm2 = c_ims ; c_em2 = c_ime
               c_sd1 = c_jds ; c_ed1 = c_jde ; c_sp1 = c_jps ; c_ep1 = c_jpe ; c_sm1 = c_jms ; c_em1 = c_jme
               c_sd3 = c_kds ; c_ed3 = c_kde ; c_sp3 = c_kps ; c_ep3 = c_kpe ; c_sm3 = c_kms ; c_em3 = c_kme
            CASE ( DATA_ORDER_XZY )
               c_sd1 = c_ids ; c_ed1 = c_ide ; c_sp1 = c_ips ; c_ep1 = c_ipe ; c_sm1 = c_ims ; c_em1 = c_ime
               c_sd3 = c_jds ; c_ed3 = c_jde ; c_sp3 = c_jps ; c_ep3 = c_jpe ; c_sm3 = c_jms ; c_em3 = c_jme
               c_sd2 = c_kds ; c_ed2 = c_kde ; c_sp2 = c_kps ; c_ep2 = c_kpe ; c_sm2 = c_kms ; c_em2 = c_kme
            CASE ( DATA_ORDER_YZX )
               c_sd3 = c_ids ; c_ed3 = c_ide ; c_sp3 = c_ips ; c_ep3 = c_ipe ; c_sm3 = c_ims ; c_em3 = c_ime
               c_sd1 = c_jds ; c_ed1 = c_jde ; c_sp1 = c_jps ; c_ep1 = c_jpe ; c_sm1 = c_jms ; c_em1 = c_jme
               c_sd2 = c_kds ; c_ed2 = c_kde ; c_sp2 = c_kps ; c_ep2 = c_kpe ; c_sm2 = c_kms ; c_em2 = c_kme
         END SELECT

         ALLOCATE ( intermediate_grid )
         ALLOCATE ( intermediate_grid%parents( max_parents ) )
         ALLOCATE ( intermediate_grid%nests( max_nests ) )
         intermediate_grid%allocated=.false.
         NULLIFY( intermediate_grid%sibling )
         DO i = 1, max_nests
            NULLIFY( intermediate_grid%nests(i)%ptr )
         END DO
         NULLIFY  (intermediate_grid%next)
         NULLIFY  (intermediate_grid%same_level)
         NULLIFY  (intermediate_grid%i_start)
         NULLIFY  (intermediate_grid%j_start)
         NULLIFY  (intermediate_grid%i_end)
         NULLIFY  (intermediate_grid%j_end)
         intermediate_grid%id = id   
         intermediate_grid%num_nests = 0
         intermediate_grid%num_siblings = 0
         intermediate_grid%num_parents = 1
         intermediate_grid%max_tiles   = 0
         intermediate_grid%num_tiles_spec   = 0

         intermediate_grid%active_this_task = .true.

         CALL find_grid_by_id ( id, head_grid, nest_grid )

         nest_grid%intermediate_grid => intermediate_grid  
         intermediate_grid%parents(1)%ptr => nest_grid     
         intermediate_grid%num_parents = 1

         intermediate_grid%is_intermediate = .TRUE.
         SELECT CASE ( model_data_order )
            CASE ( DATA_ORDER_ZXY )
               intermediate_grid%nids = nest_grid%sd32 ; intermediate_grid%njds = nest_grid%sd33
               intermediate_grid%nide = nest_grid%ed32 ; intermediate_grid%njde = nest_grid%sd33
            CASE ( DATA_ORDER_ZYX )
               intermediate_grid%nids = nest_grid%sd33 ; intermediate_grid%njds = nest_grid%sd32
               intermediate_grid%nide = nest_grid%ed33 ; intermediate_grid%njde = nest_grid%sd32
            CASE ( DATA_ORDER_XYZ )
               intermediate_grid%nids = nest_grid%sd31 ; intermediate_grid%njds = nest_grid%sd32
               intermediate_grid%nide = nest_grid%ed31 ; intermediate_grid%njde = nest_grid%sd32
            CASE ( DATA_ORDER_YXZ)
               intermediate_grid%nids = nest_grid%sd32 ; intermediate_grid%njds = nest_grid%sd31
               intermediate_grid%nide = nest_grid%ed32 ; intermediate_grid%njde = nest_grid%sd31
            CASE ( DATA_ORDER_XZY )
               intermediate_grid%nids = nest_grid%sd31 ; intermediate_grid%njds = nest_grid%sd33
               intermediate_grid%nide = nest_grid%ed31 ; intermediate_grid%njde = nest_grid%sd33
            CASE ( DATA_ORDER_YZX )
               intermediate_grid%nids = nest_grid%sd33 ; intermediate_grid%njds = nest_grid%sd31
               intermediate_grid%nide = nest_grid%ed33 ; intermediate_grid%njde = nest_grid%sd31
         END SELECT
         intermediate_grid%nids = ids
         intermediate_grid%nide = ide
         intermediate_grid%njds = jds
         intermediate_grid%njde = jde

         intermediate_grid%sm31x                           = c_sm1x
         intermediate_grid%em31x                           = c_em1x
         intermediate_grid%sm32x                           = c_sm2x
         intermediate_grid%em32x                           = c_em2x
         intermediate_grid%sm33x                           = c_sm3x
         intermediate_grid%em33x                           = c_em3x
         intermediate_grid%sm31y                           = c_sm1y
         intermediate_grid%em31y                           = c_em1y
         intermediate_grid%sm32y                           = c_sm2y
         intermediate_grid%em32y                           = c_em2y
         intermediate_grid%sm33y                           = c_sm3y
         intermediate_grid%em33y                           = c_em3y




         

         CALL alloc_space_field ( intermediate_grid, intermediate_grid%id , 1, 2 , .TRUE., nest_grid%active_this_task, &   
                               c_sd1, c_ed1, c_sd2, c_ed2, c_sd3, c_ed3,       &
                               c_sm1,  c_em1,  c_sm2,  c_em2,  c_sm3,  c_em3,  &
                               c_sp1,  c_ep1,  c_sp2,  c_ep2,  c_sp3,  c_ep3,  &
                               c_sm1x, c_em1x, c_sm2x, c_em2x, c_sm3x, c_em3x, &
                               c_sm1y, c_em1y, c_sm2y, c_em2y, c_sm3y, c_em3y, &
                               c_sm1x, c_em1x, c_sm2x, c_em2x, c_sm3x, c_em3x, &   
                               c_sm1y, c_em1y, c_sm2y, c_em2y, c_sm3y, c_em3y  )   

         intermediate_grid%sd31                            =   c_sd1
         intermediate_grid%ed31                            =   c_ed1
         intermediate_grid%sp31                            = c_sp1
         intermediate_grid%ep31                            = c_ep1
         intermediate_grid%sm31                            = c_sm1
         intermediate_grid%em31                            = c_em1
         intermediate_grid%sd32                            =   c_sd2
         intermediate_grid%ed32                            =   c_ed2
         intermediate_grid%sp32                            = c_sp2
         intermediate_grid%ep32                            = c_ep2
         intermediate_grid%sm32                            = c_sm2
         intermediate_grid%em32                            = c_em2
         intermediate_grid%sd33                            =   c_sd3
         intermediate_grid%ed33                            =   c_ed3
         intermediate_grid%sp33                            = c_sp3
         intermediate_grid%ep33                            = c_ep3
         intermediate_grid%sm33                            = c_sm3
         intermediate_grid%em33                            = c_em3

         CALL med_add_config_info_to_grid ( intermediate_grid )

         intermediate_grid%dx = parent%dx
         intermediate_grid%dy = parent%dy
         intermediate_grid%dt = parent%dt
      END IF

      RETURN
  END SUBROUTINE patch_domain_rsl_lite

  SUBROUTINE compute_memory_dims_rsl_lite  (      &
                   id , maxhalowidth ,            &
                   shw , bdx,  bdy ,              &
                   ids,  ide,  jds,  jde,  kds,  kde, &
                   ims,  ime,  jms,  jme,  kms,  kme, &
                   imsx, imex, jmsx, jmex, kmsx, kmex, &
                   imsy, imey, jmsy, jmey, kmsy, kmey, &
                   ips,  ipe,  jps,  jpe,  kps,  kpe, &
                   ipsx, ipex, jpsx, jpex, kpsx, kpex, &
                   ipsy, ipey, jpsy, jpey, kpsy, kpey )

    IMPLICIT NONE
    INTEGER, INTENT(IN)               ::  id , maxhalowidth
    INTEGER, INTENT(IN)               ::  shw, bdx, bdy
    INTEGER, INTENT(IN)     ::  ids, ide, jds, jde, kds, kde
    INTEGER, INTENT(OUT)    ::  ims, ime, jms, jme, kms, kme
    INTEGER, INTENT(OUT)    ::  imsx, imex, jmsx, jmex, kmsx, kmex
    INTEGER, INTENT(OUT)    ::  imsy, imey, jmsy, jmey, kmsy, kmey
    INTEGER, INTENT(OUT)    ::  ips, ipe, jps, jpe, kps, kpe
    INTEGER, INTENT(OUT)    ::  ipsx, ipex, jpsx, jpex, kpsx, kpex
    INTEGER, INTENT(OUT)    ::  ipsy, ipey, jpsy, jpey, kpsy, kpey

    INTEGER Px, Py, P, i, j, k, ierr



    ips = -1
    j = jds
    ierr = 0
    DO i = ids, ide

       CALL task_for_point ( i, j, ids, ide, jds, jde, nest_pes_x(id), nest_pes_y(id), Px, Py, &
                             minx, miny, ierr )
       IF ( ierr .NE. 0 ) CALL wrf_error_fatal3("<stdin>",775,&
'error code returned by task_for_point in module_dm.F (c)')
       IF ( Px .EQ. mytask_x ) THEN
          ipe = i
          IF ( ips .EQ. -1 ) ips = i
       END IF
    END DO
    IF ( ierr .NE. 0 ) THEN
       CALL tfp_message("<stdin>",978)
    END IF
    
    IF (ips .EQ. -1 ) THEN
       ipe = -1
       ips = 0
    END IF

    jps = -1
    i = ids
    ierr = 0
    DO j = jds, jde

       CALL task_for_point ( i, j, ids, ide, jds, jde, nest_pes_x(id), nest_pes_y(id), Px, Py, &
                             minx, miny, ierr )
       IF ( ierr .NE. 0 ) CALL wrf_error_fatal3("<stdin>",798,&
'error code returned by task_for_point in module_dm.F (d)')
       IF ( Py .EQ. mytask_y ) THEN
          jpe = j
          IF ( jps .EQ. -1 ) jps = j
       END IF
    END DO
    IF ( ierr .NE. 0 ) THEN
       CALL tfp_message("<stdin>",1000)
    END IF
    
    IF (jps .EQ. -1 ) THEN
       jpe = -1
       jps = 0
    END IF





    IF (ipe .EQ. -1 .or. jpe .EQ. -1) THEN
       ipe = -1
       ips = 0
       jpe = -1
       jps = 0
    END IF









































    kpsx = -1
    j = jds ;
    ierr = 0
    DO k = kds, kde

       CALL task_for_point ( k, j, kds, kde, jds, jde, nest_pes_x(id), nest_pes_y(id), Px, Py, &
                             minx, miny, ierr )
       IF ( ierr .NE. 0 ) CALL wrf_error_fatal3("<stdin>",872,&
'error code returned by task_for_point in module_dm.F (e)')
       IF ( Px .EQ. mytask_x ) THEN
          kpex = k
          IF ( kpsx .EQ. -1 ) kpsx = k
       END IF
    END DO
    IF ( ierr .NE. 0 ) THEN
       CALL tfp_message("<stdin>",1073)
    END IF
    


    IF (kpsx .EQ. -1 ) THEN
       kpex = -1
       kpsx = 0
    END IF

    jpsx = -1
    k = kds ;
    ierr = 0
    DO j = jds, jde

       CALL task_for_point ( k, j, kds, kde, jds, jde, nest_pes_x(id), nest_pes_y(id), Px, Py, &
                             minx, miny, ierr )
       IF ( ierr .NE. 0 ) CALL wrf_error_fatal3("<stdin>",897,&
'error code returned by task_for_point in module_dm.F (f)')
       IF ( Py .EQ. mytask_y ) THEN
          jpex = j
          IF ( jpsx .EQ. -1 ) jpsx = j
       END IF
    END DO
    IF ( ierr .NE. 0 ) THEN
       CALL tfp_message("<stdin>",1097)
    END IF
    IF (jpsx .EQ. -1 ) THEN
       jpex = -1
       jpsx = 0
    END IF





    IF (jpex .EQ. -1) THEN
       ipex = -1
       ipsx = 0
       jpex = -1
       jpsx = 0
    END IF




    kpsy = kpsx   
    kpey = kpex   

    ipsy = -1
    k = kds ;
    ierr = 0
    DO i = ids, ide

       CALL task_for_point ( i, k, ids, ide, kds, kde, nest_pes_y(id), nest_pes_x(id), Py, Px, &
                             miny, minx, ierr )
       IF ( ierr .NE. 0 ) CALL wrf_error_fatal3("<stdin>",936,&
'error code returned by task_for_point in module_dm.F (g)')
       IF ( Py .EQ. mytask_y ) THEN
          ipey = i
          IF ( ipsy .EQ. -1 ) ipsy = i
       END IF
    END DO
    IF ( ierr .NE. 0 ) THEN
       CALL tfp_message("<stdin>",1135)
    END IF
    IF (ipsy .EQ. -1 ) THEN
       ipey = -1
       ipsy = 0
    END IF



    IF ( ips < ipe .and. jps < jpe ) THEN           
       IF ( mytask_x .EQ. 0 ) THEN
          ips = ips - shw
          ipsy = ipsy - shw
       END IF

       IF ( mytask_x .EQ. nest_pes_x(id)-1 ) THEN
          ipe = ipe + shw
          ipey = ipey + shw
       END IF
       IF ( mytask_y .EQ. 0 ) THEN
          jps = jps - shw
          jpsx = jpsx - shw
       END IF

       IF ( mytask_y .EQ. nest_pes_y(id)-1 ) THEN
          jpe = jpe + shw
          jpex = jpex + shw
       END IF
    END IF                                           

    kps = 1
    kpe = kde-kds+1

    kms = 1
    kme = kpe
    kmsx = kpsx
    kmex = kpex
    kmsy = kpsy
    kmey = kpey

    
    IF ( kpsx .EQ. 0 .AND. kpex .EQ. -1 ) THEN
      kmsx = 0
      kmex = 0
    END IF
    IF ( kpsy .EQ. 0 .AND. kpey .EQ. -1 ) THEN
      kmsy = 0
      kmey = 0
    END IF

    IF ( (jps .EQ. 0 .AND. jpe .EQ. -1) .OR. (ips .EQ. 0 .AND. ipe .EQ. -1) ) THEN
      ims = 0
      ime = 0
    ELSE
      ims = max( ips - max(shw,maxhalowidth), ids - bdx ) - 1
      ime = min( ipe + max(shw,maxhalowidth), ide + bdx ) + 1

    END IF
    imsx = ids
    imex = ide
    ipsx = imsx
    ipex = imex
    
    IF ( ipsy .EQ. 0 .AND. ipey .EQ. -1 ) THEN
      imsy = 0
      imey = 0
    ELSE
      imsy = ipsy
      imey = ipey
    END IF

    IF ( (jps .EQ. 0 .AND. jpe .EQ. -1) .OR. (ips .EQ. 0 .AND. ipe .EQ. -1) ) THEN
      jms = 0
      jme = 0
    ELSE
      jms = max( jps - max(shw,maxhalowidth), jds - bdy ) - 1
      jme = min( jpe + max(shw,maxhalowidth), jde + bdy ) + 1
    END IF
    jmsx = jpsx
    jmex = jpex
    jmsy = jds
    jmey = jde
    
    IF ( jpsx .EQ. 0 .AND. jpex .EQ. -1 ) THEN
      jmsx = 0
      jmex = 0
      jpsy = 0
      jpey = -1
    ELSE
      jpsy = jmsy
      jpey = jmey
    END IF

  END SUBROUTINE compute_memory_dims_rsl_lite



   INTEGER function getrealmpitype()

      IMPLICIT NONE
      INTEGER rtypesize, dtypesize, ierr
      CALL mpi_type_size ( MPI_REAL, rtypesize, ierr )
      CALL mpi_type_size ( MPI_DOUBLE_PRECISION, dtypesize, ierr )
      IF ( 4 .EQ. rtypesize ) THEN
        getrealmpitype = MPI_REAL
      ELSE IF ( 4 .EQ. dtypesize ) THEN
        getrealmpitype = MPI_DOUBLE_PRECISION
      ELSE
        CALL wrf_error_fatal3("<stdin>",1052,&
'RWORDSIZE or DWORDSIZE does not match any MPI type' )
      END IF

      RETURN
   END FUNCTION getrealmpitype

   INTEGER FUNCTION wrf_dm_max_int ( inval )
      IMPLICIT NONE

      INTEGER, intent(in) :: inval
      INTEGER :: ierr, retval
      CALL mpi_allreduce ( inval, retval , 1, MPI_INTEGER, MPI_MAX, local_communicator, ierr )
      wrf_dm_max_int = retval

   END FUNCTION wrf_dm_max_int

   REAL FUNCTION wrf_dm_max_real ( inval )
      IMPLICIT NONE

      REAL inval, retval
      INTEGER comm,ierr
      CALL wrf_get_dm_communicator(comm)
      CALL mpi_allreduce ( inval, retval , 1, getrealmpitype(), MPI_MAX, comm, ierr )
      wrf_dm_max_real = retval

   END FUNCTION wrf_dm_max_real

   REAL FUNCTION wrf_dm_min_real ( inval )
      IMPLICIT NONE

      REAL inval, retval
      INTEGER comm,ierr
      CALL wrf_get_dm_communicator(comm)
      CALL mpi_allreduce ( inval, retval , 1, getrealmpitype(), MPI_MIN, comm, ierr )
      wrf_dm_min_real = retval

   END FUNCTION wrf_dm_min_real

   SUBROUTINE wrf_dm_min_reals ( inval, retval, n )
      IMPLICIT NONE
      INTEGER n
      REAL inval(*)
      REAL retval(*)

      INTEGER comm,ierr
      CALL wrf_get_dm_communicator(comm)
      CALL mpi_allreduce ( inval, retval , n, getrealmpitype(), MPI_MIN, comm, ierr )

   END SUBROUTINE wrf_dm_min_reals

   FUNCTION wrf_dm_sum_real8 ( inval )
      IMPLICIT NONE

      REAL*8 inval, retval, wrf_dm_sum_real8
      INTEGER comm,ierr
      CALL wrf_get_dm_communicator(comm)
      CALL mpi_allreduce ( inval, retval , 1, MPI_REAL8, MPI_SUM, comm, ierr )
      wrf_dm_sum_real8 = retval

   END FUNCTION wrf_dm_sum_real8

   REAL FUNCTION wrf_dm_sum_real ( inval )
      IMPLICIT NONE

      REAL inval, retval
      INTEGER comm,ierr
      CALL wrf_get_dm_communicator(comm)
      CALL mpi_allreduce ( inval, retval , 1, getrealmpitype(), MPI_SUM, comm, ierr )
      wrf_dm_sum_real = retval

   END FUNCTION wrf_dm_sum_real

   SUBROUTINE wrf_dm_sum_reals (inval, retval)
      IMPLICIT NONE
      REAL, INTENT(IN)  :: inval(:)
      REAL, INTENT(OUT) :: retval(:)

      INTEGER comm,ierr
      CALL wrf_get_dm_communicator(comm)
      CALL mpi_allreduce ( inval, retval, SIZE(inval), getrealmpitype(), MPI_SUM, comm, ierr )

   END SUBROUTINE wrf_dm_sum_reals

   INTEGER FUNCTION wrf_dm_sum_integer ( inval )
      IMPLICIT NONE

      INTEGER inval, retval
      INTEGER comm,ierr
      CALL wrf_get_dm_communicator(comm)
      CALL mpi_allreduce ( inval, retval , 1, MPI_INTEGER, MPI_SUM, comm, ierr )
      wrf_dm_sum_integer = retval

   END FUNCTION wrf_dm_sum_integer

   SUBROUTINE wrf_dm_sum_integers (inval, retval)
      IMPLICIT NONE
      INTEGER, INTENT(IN)  :: inval(:)
      INTEGER, INTENT(OUT) :: retval(:)

      INTEGER comm,ierr
      CALL wrf_get_dm_communicator(comm)
      CALL mpi_allreduce ( inval, retval, SIZE(inval), MPI_INTEGER, MPI_SUM, comm, ierr )

   END SUBROUTINE wrf_dm_sum_integers


   INTEGER FUNCTION wrf_dm_bxor_integer ( inval )
      IMPLICIT NONE

      INTEGER inval, retval
      INTEGER comm, ierr
      CALL wrf_get_dm_communicator(comm)
      CALL mpi_allreduce ( inval, retval , 1, MPI_INTEGER, MPI_BXOR, comm, ierr )
      wrf_dm_bxor_integer = retval

   END FUNCTION wrf_dm_bxor_integer


LOGICAL FUNCTION wrf_dm_lor_logical ( inval )
      IMPLICIT NONE

      LOGICAL inval, retval
      INTEGER comm, ierr
      CALL wrf_get_dm_communicator(comm)
      CALL mpi_allreduce ( inval, retval , 1, MPI_LOGICAL, MPI_LOR, comm, ierr )
      wrf_dm_lor_logical = retval

   END FUNCTION wrf_dm_lor_logical


LOGICAL FUNCTION wrf_dm_land_logical ( inval )
      IMPLICIT NONE

      LOGICAL inval, retval
      INTEGER comm, ierr
      CALL wrf_get_dm_communicator(comm)
      CALL mpi_allreduce ( inval, retval , 1, MPI_LOGICAL, MPI_LAND, comm, ierr )
      wrf_dm_land_logical = retval

   END FUNCTION wrf_dm_land_logical


   SUBROUTINE wrf_dm_maxval_real ( val, idex, jdex )

      IMPLICIT NONE
      REAL val
      INTEGER :: idex, jdex, i, comm
      INTEGER :: bcast(2),mrank
      REAL :: inreduce(2),outreduce(2)

      inreduce=(/ val, real(mytask) /)
      bcast=(/ idex,jdex /)
      CALL wrf_get_dm_communicator(comm)
      call MPI_Allreduce(inreduce,outreduce,1,MPI_2REAL,&
                         MPI_MAXLOC,comm,i)
      mrank=outreduce(2)
      val=outreduce(1)
      call MPI_Bcast(bcast,2,MPI_REAL,mrank,comm,i)
      idex=bcast(1)
      jdex=bcast(2)

    END SUBROUTINE wrf_dm_maxval_real

   SUBROUTINE wrf_dm_minval_real ( val, idex, jdex )

      IMPLICIT NONE
      REAL val
      INTEGER :: idex, jdex, i, comm
      INTEGER :: bcast(2),mrank
      REAL :: inreduce(2),outreduce(2)

      inreduce=(/ val, real(mytask) /)
      bcast=(/ idex,jdex /)
      CALL wrf_get_dm_communicator(comm)
      call MPI_Allreduce(inreduce,outreduce,1,MPI_2REAL,&
                         MPI_MINLOC,comm,i)
      mrank=outreduce(2)
      val=outreduce(1)
      call MPI_Bcast(bcast,2,MPI_REAL,mrank,comm,i)
      idex=bcast(1)
      jdex=bcast(2)

    END SUBROUTINE wrf_dm_minval_real


   SUBROUTINE wrf_dm_maxval_doubleprecision ( val, idex, jdex )

      IMPLICIT NONE
      DOUBLE PRECISION val
      INTEGER :: idex, jdex, i, comm
      INTEGER :: bcast(2),mrank
      DOUBLE PRECISION :: inreduce(2),outreduce(2)

      inreduce=(/ val, dble(mytask) /)
      bcast=(/ idex,jdex /)
      CALL wrf_get_dm_communicator(comm)
      call MPI_Allreduce(inreduce,outreduce,1,MPI_2DOUBLE_PRECISION,&
                         MPI_MAXLOC,comm,i)
      mrank=outreduce(2)
      val=outreduce(1)
      call MPI_Bcast(bcast,2,MPI_DOUBLE_PRECISION,mrank,comm,i)
      idex=bcast(1)
      jdex=bcast(2)

   END SUBROUTINE wrf_dm_maxval_doubleprecision

   SUBROUTINE wrf_dm_minval_doubleprecision ( val, idex, jdex )

      IMPLICIT NONE
      DOUBLE PRECISION val
      INTEGER :: idex, jdex, i, comm
      INTEGER :: bcast(2),mrank
      DOUBLE PRECISION :: inreduce(2),outreduce(2)

      inreduce=(/ val, dble(mytask) /)
      bcast=(/ idex,jdex /)
      CALL wrf_get_dm_communicator(comm)
      call MPI_Allreduce(inreduce,outreduce,1,MPI_2DOUBLE_PRECISION,&
                         MPI_MINLOC,comm,i)
      mrank=outreduce(2)
      val=outreduce(1)
      call MPI_Bcast(bcast,2,MPI_DOUBLE_PRECISION,mrank,comm,i)
      idex=bcast(1)
      jdex=bcast(2)

   END SUBROUTINE wrf_dm_minval_doubleprecision


   SUBROUTINE wrf_dm_maxval_integer ( val, idex, jdex )

      IMPLICIT NONE
      INTEGER val
      INTEGER :: idex, jdex, i, comm
      INTEGER :: bcast(2),mrank
      INTEGER :: inreduce(2),outreduce(2)

      inreduce=(/ val, mytask /)
      bcast=(/ idex,jdex /)
      CALL wrf_get_dm_communicator(comm)
      call MPI_Allreduce(inreduce,outreduce,1,MPI_2INTEGER,&
                         MPI_MAXLOC,comm,i)
      mrank=outreduce(2)
      val=outreduce(1)
      call MPI_Bcast(bcast,2,MPI_INTEGER,mrank,comm,i)
      idex=bcast(1)
      jdex=bcast(2)

    END SUBROUTINE wrf_dm_maxval_integer

   SUBROUTINE wrf_dm_minval_integer ( val, idex, jdex )

      IMPLICIT NONE
      INTEGER val
      INTEGER :: idex, jdex, i, comm
      INTEGER :: bcast(2),mrank
      INTEGER :: inreduce(2),outreduce(2)

      inreduce=(/ val, mytask /)
      bcast=(/ idex,jdex /)
      CALL wrf_get_dm_communicator(comm)
      call MPI_Allreduce(inreduce,outreduce,1,MPI_2INTEGER,&
                         MPI_MINLOC,comm,i)
      mrank=outreduce(2)
      val=outreduce(1)
      call MPI_Bcast(bcast,2,MPI_INTEGER,mrank,comm,i)
      idex=bcast(1)
      jdex=bcast(2)

    END SUBROUTINE wrf_dm_minval_integer

   SUBROUTINE hwrf_coupler_init
   END SUBROUTINE hwrf_coupler_init

   SUBROUTINE split_communicator

      IMPLICIT NONE
      LOGICAL mpi_inited

      INTEGER mpi_comm_here, mpi_comm_local, comdup, comdup2, origmytask,  ierr, io_status
      INTEGER mpi_comm_me_and_mom
      INTEGER coords(3)
      INTEGER mytask_local,ntasks_local,num_compute_tasks

      INTEGER i, j, k, x, y, n_x, n_y
      INTEGER iii
      INTEGER, ALLOCATABLE :: icolor(:),icolor2(:),idomain(:)
      INTEGER comm_id








































      INTEGER dims(3)

      INTEGER :: id
      INTEGER :: intercomm
      INTEGER :: domain_id,par_id,nest_id,kid_id
      INTEGER :: mytask_me_and_mom, ntasks_me_and_mom, remote_leader
      LOGICAL :: inthisone
      LOGICAL :: mytask_is_nest, mytask_is_par,isperiodic(3)

      LOGICAL :: quilting_is_turned_off










integer    :: first_item_in_struct
integer :: run_days
integer :: run_hours
integer :: run_minutes
integer :: run_seconds
integer , DIMENSION(max_domains) :: start_year
integer , DIMENSION(max_domains) :: start_month
integer , DIMENSION(max_domains) :: start_day
integer , DIMENSION(max_domains) :: start_hour
integer , DIMENSION(max_domains) :: start_minute
integer , DIMENSION(max_domains) :: start_second
integer , DIMENSION(max_domains) :: end_year
integer , DIMENSION(max_domains) :: end_month
integer , DIMENSION(max_domains) :: end_day
integer , DIMENSION(max_domains) :: end_hour
integer , DIMENSION(max_domains) :: end_minute
integer , DIMENSION(max_domains) :: end_second
integer :: interval_seconds
logical , DIMENSION(max_domains) :: input_from_file
integer , DIMENSION(max_domains) :: fine_input_stream
logical , DIMENSION(max_domains) :: input_from_hires
character*256 :: rsmas_data_path
logical :: all_ic_times
integer , DIMENSION(max_domains) :: julyr
integer , DIMENSION(max_domains) :: julday
real , DIMENSION(max_domains) :: gmt
character*256 :: input_inname
character*256 :: input_outname
character*256 :: bdy_inname
character*256 :: bdy_outname
character*256 :: rst_inname
character*256 :: rst_outname
logical :: write_input
logical :: write_restart_at_0h
logical :: write_hist_at_0h_rst
logical :: adjust_output_times
logical :: adjust_input_times
integer :: diag_print
logical :: nocolons
logical :: cycling
integer :: output_diagnostics
integer :: nwp_diagnostics
logical :: output_ready_flag
logical :: force_use_old_data
logical :: usepio
integer :: pioprocs
integer :: piostart
integer :: piostride
integer :: pioshift
integer :: dfi_opt
integer :: dfi_savehydmeteors
integer :: dfi_nfilter
logical :: dfi_write_filtered_input
logical :: dfi_write_dfi_history
integer :: dfi_cutoff_seconds
integer :: dfi_time_dim
integer :: dfi_fwdstop_year
integer :: dfi_fwdstop_month
integer :: dfi_fwdstop_day
integer :: dfi_fwdstop_hour
integer :: dfi_fwdstop_minute
integer :: dfi_fwdstop_second
integer :: dfi_bckstop_year
integer :: dfi_bckstop_month
integer :: dfi_bckstop_day
integer :: dfi_bckstop_hour
integer :: dfi_bckstop_minute
integer :: dfi_bckstop_second
integer :: time_step
integer :: time_step_fract_num
integer :: time_step_fract_den
integer :: time_step_dfi
real :: reasonable_time_step_ratio
integer , DIMENSION(max_domains) :: min_time_step
integer , DIMENSION(max_domains) :: min_time_step_den
integer , DIMENSION(max_domains) :: max_time_step
integer , DIMENSION(max_domains) :: max_time_step_den
real , DIMENSION(max_domains) :: target_cfl
real , DIMENSION(max_domains) :: target_hcfl
integer , DIMENSION(max_domains) :: max_step_increase_pct
integer , DIMENSION(max_domains) :: starting_time_step
integer , DIMENSION(max_domains) :: starting_time_step_den
logical :: step_to_output_time
integer :: adaptation_domain
logical :: use_adaptive_time_step
logical :: use_adaptive_time_step_dfi
integer :: max_dom
integer :: lats_to_mic
integer , DIMENSION(max_domains) :: s_we
integer , DIMENSION(max_domains) :: e_we
integer , DIMENSION(max_domains) :: s_sn
integer , DIMENSION(max_domains) :: e_sn
integer , DIMENSION(max_domains) :: s_vert
integer , DIMENSION(max_domains) :: e_vert
integer :: num_metgrid_levels
integer :: num_metgrid_soil_levels
real :: p_top_requested
logical :: interp_theta
integer :: interp_type
integer :: rebalance
integer , DIMENSION(max_domains) :: vert_refine_method
integer :: vert_refine_fact
integer :: extrap_type
integer :: t_extrap_type
integer :: hypsometric_opt
logical :: lowest_lev_from_sfc
logical :: use_levels_below_ground
logical :: use_tavg_for_tsk
logical :: use_surface
integer :: lagrange_order
integer :: linear_interp
integer :: force_sfc_in_vinterp
real :: zap_close_levels
real :: maxw_horiz_pres_diff
real :: trop_horiz_pres_diff
real :: maxw_above_this_level
integer :: use_maxw_level
integer :: use_trop_level
logical :: sfcp_to_sfcp
logical :: adjust_heights
logical :: smooth_cg_topo
integer :: nest_interp_coord
integer :: interp_method_type
logical :: aggregate_lu
logical :: rh2qv_wrt_liquid
integer :: rh2qv_method
logical :: use_sh_qv
real :: qv_max_p_safe
real :: qv_max_flag
real :: qv_max_value
real :: qv_min_p_safe
real :: qv_min_flag
real :: qv_min_value
integer :: ideal_init_method
real , DIMENSION(max_domains) :: dx
real , DIMENSION(max_domains) :: dy
integer , DIMENSION(max_domains) :: grid_id
logical , DIMENSION(max_domains) :: grid_allowed
integer , DIMENSION(max_domains) :: parent_id
integer , DIMENSION(max_domains) :: i_parent_start
integer , DIMENSION(max_domains) :: j_parent_start
integer , DIMENSION(max_domains) :: parent_grid_ratio
integer , DIMENSION(max_domains) :: parent_time_step_ratio
integer :: feedback
integer :: smooth_option
integer :: blend_width
real , DIMENSION(max_domains) :: ztop
integer , DIMENSION(max_domains) :: moad_grid_ratio
integer , DIMENSION(max_domains) :: moad_time_step_ratio
integer , DIMENSION(max_domains) :: shw
integer :: tile_sz_x
integer :: tile_sz_y
integer :: numtiles
integer :: numtiles_inc
integer :: numtiles_x
integer :: numtiles_y
integer :: tile_strategy
integer :: nproc_x
integer :: nproc_y
integer :: irand
real , DIMENSION(max_domains) :: dt
integer :: fft_used
integer :: cu_used
integer :: shcu_used
integer :: cam_used
integer :: gwd_used
integer :: gwd_diags_used
integer :: alloc_qndropsource
integer :: num_moves
integer :: ts_buf_size
integer :: max_ts_locs
logical :: tslist_ij
logical :: tslist_unstagger_winds
integer , DIMENSION(max_domains) :: vortex_interval
integer , DIMENSION(max_domains) :: max_vortex_speed
integer , DIMENSION(max_domains) :: corral_dist
integer :: track_level
real , DIMENSION(max_domains) :: time_to_move
integer , DIMENSION(max_moves) :: move_id
integer , DIMENSION(max_moves) :: move_interval
integer , DIMENSION(max_moves) :: move_cd_x
integer , DIMENSION(max_moves) :: move_cd_y
logical , DIMENSION(max_domains) :: swap_x
logical , DIMENSION(max_domains) :: swap_y
logical , DIMENSION(max_domains) :: cycle_x
logical , DIMENSION(max_domains) :: cycle_y
logical :: reorder_mesh
logical :: perturb_input
real , DIMENSION(max_eta) :: eta_levels
integer :: auto_levels_opt
real :: max_dz
real :: dzbot
real :: dzstretch_s
real :: dzstretch_u
integer :: ocean_levels
real , DIMENSION(max_ocean) :: ocean_z
real , DIMENSION(max_ocean) :: ocean_t
real , DIMENSION(max_ocean) :: ocean_s
integer :: num_traj
integer :: max_ts_level
integer :: track_loc_in
integer :: num_ext_model_couple_dom
logical :: insert_bogus_storm
logical :: remove_storm
integer :: num_storm
real , DIMENSION(max_bogus) :: latc_loc
real , DIMENSION(max_bogus) :: lonc_loc
real , DIMENSION(max_bogus) :: vmax_meters_per_second
real , DIMENSION(max_bogus) :: rmax
real , DIMENSION(max_bogus) :: vmax_ratio
real :: rankine_lid
character*256 :: physics_suite
logical :: force_read_thompson
logical :: write_thompson_tables
logical :: write_thompson_mp38table
integer , DIMENSION(max_domains) :: mp_physics
real :: nssl_cccn
real :: nssl_alphah
real :: nssl_alphahl
real :: nssl_cnoh
real :: nssl_cnohl
real :: nssl_cnor
real :: nssl_cnos
real :: nssl_rho_qh
real :: nssl_rho_qhl
real :: nssl_rho_qs
integer :: nssl_icdx
integer :: nssl_icdxhl
integer :: nssl_hail_on
integer :: nssl_ccn_on
integer :: nssl_ccn_is_ccna
integer :: nssl_2moment_on
integer :: nssl_3moment
integer :: nssl_density_on
integer :: nssl_ssat_output
integer :: ccnty
integer , DIMENSION(max_domains) :: nudge_lightning
integer , DIMENSION(max_domains) :: nudge_light_times
integer , DIMENSION(max_domains) :: nudge_light_timee
integer , DIMENSION(max_domains) :: nudge_light_int
character*256 :: path_to_files
integer :: gsfcgce_hail
integer :: gsfcgce_2ice
integer , DIMENSION(max_domains) :: progn
real :: accum_mode
real :: aitken_mode
real :: coarse_mode
integer :: do_radar_ref
integer :: compute_radar_ref
integer , DIMENSION(max_domains) :: ra_lw_physics
integer , DIMENSION(max_domains) :: ra_sw_physics
integer :: ra_sw_eclipse
integer :: ghg_input
real , DIMENSION(max_domains) :: radt
real , DIMENSION(max_domains) :: naer
integer , DIMENSION(max_domains) :: sf_sfclay_physics
integer , DIMENSION(max_domains) :: sf_surf_irr_scheme
integer :: sf_surf_irr_alloc
real , DIMENSION(max_domains) :: irr_daily_amount
integer , DIMENSION(max_domains) :: irr_start_hour
integer , DIMENSION(max_domains) :: irr_num_hours
integer , DIMENSION(max_domains) :: irr_start_julianday
integer , DIMENSION(max_domains) :: irr_end_julianday
integer , DIMENSION(max_domains) :: irr_freq
integer , DIMENSION(max_domains) :: irr_ph
integer , DIMENSION(max_domains) :: sf_surface_physics
integer , DIMENSION(max_domains) :: bl_pbl_physics
integer , DIMENSION(max_domains) :: tke_budget
integer :: ysu_topdown_pblmix
integer , DIMENSION(max_domains) :: shinhong_tke_diag
logical , DIMENSION(max_domains) :: bl_mynn_tkeadvect
integer :: bl_mynn_cloudpdf
integer :: bl_mynn_mixlength
integer , DIMENSION(max_domains) :: bl_mynn_edmf
integer , DIMENSION(max_domains) :: bl_mynn_edmf_mom
integer , DIMENSION(max_domains) :: bl_mynn_edmf_tke
integer , DIMENSION(max_domains) :: bl_mynn_mixscalars
integer , DIMENSION(max_domains) :: bl_mynn_output
integer , DIMENSION(max_domains) :: bl_mynn_cloudmix
integer , DIMENSION(max_domains) :: bl_mynn_mixqt
integer :: icloud_bl
real :: bl_mynn_closure
integer , DIMENSION(max_domains) :: mfshconv
integer , DIMENSION(max_domains) :: sf_urban_physics
real , DIMENSION(max_domains) :: bldt
integer , DIMENSION(max_domains) :: cu_physics
integer , DIMENSION(max_domains) :: shcu_physics
integer , DIMENSION(max_domains) :: cu_diag
integer , DIMENSION(max_domains) :: kf_edrates
integer :: kfeta_trigger
integer :: nsas_dx_factor
real , DIMENSION(max_domains) :: cudt
real , DIMENSION(max_domains) :: gsmdt
integer :: isfflx
integer :: ifsnow
integer :: icloud
integer :: cldovrlp
integer :: idcor
integer :: ideal_xland
real :: swrad_scat
integer :: surface_input_source
integer :: num_soil_layers
integer :: default_soiltype
integer :: num_pft_clm
logical :: input_pft
integer :: maxpatch
integer :: num_snow_layers
integer :: num_snso_layers
integer :: num_urban_ndm
integer :: num_urban_ng
integer :: num_urban_nwr
integer :: num_urban_ngb
integer :: num_urban_nf
integer :: num_urban_nz
integer :: num_urban_nbui
integer :: num_urban_ngr
integer :: urban_map_zrd
integer :: urban_map_zwd
integer :: urban_map_gd
integer :: urban_map_zd
integer :: urban_map_zdf
integer :: urban_map_bd
integer :: urban_map_wd
integer :: urban_map_gbd
integer :: urban_map_fbd
integer :: urban_map_zgrd
integer :: num_urban_hi
integer :: num_months
integer :: sf_surface_mosaic
integer :: mosaic_cat
integer :: mosaic_cat_soil
integer :: mosaic_lu
integer :: mosaic_soil
integer :: flag_sm_adj
integer :: maxiens
integer :: maxens
integer :: maxens2
integer :: maxens3
integer :: ensdim
integer :: cugd_avedx
integer :: clos_choice
integer :: imomentum
integer :: ishallow
real :: convtrans_avglen_m
integer :: num_land_cat
integer :: use_wudapt_lcz
logical :: slucm_distributed_drag
integer :: distributed_ahe_opt
integer :: num_soil_cat
integer :: mp_zero_out
integer :: mp_zero_out_all
real :: mp_zero_out_thresh
real :: seaice_threshold
logical , DIMENSION(max_domains) :: bmj_rad_feedback
integer :: sst_update
integer :: qna_update
integer :: sst_skin
integer :: tmn_update
logical :: usemonalb
logical :: rdmaxalb
logical :: rdlai2d
logical :: ua_phys
integer :: opt_thcnd
integer :: co2tf
integer :: ra_call_offset
real :: cam_abs_freq_s
integer :: levsiz
integer :: paerlev
integer :: cam_abs_dim1
integer :: cam_abs_dim2
integer :: lagday
integer :: no_src_types
integer :: alevsiz
integer :: o3input
integer :: aer_opt
integer :: swint_opt
integer , DIMENSION(max_domains) :: aer_type
integer , DIMENSION(max_domains) :: aer_aod550_opt
integer , DIMENSION(max_domains) :: aer_angexp_opt
integer , DIMENSION(max_domains) :: aer_ssa_opt
integer , DIMENSION(max_domains) :: aer_asy_opt
real , DIMENSION(max_domains) :: aer_aod550_val
real , DIMENSION(max_domains) :: aer_angexp_val
real , DIMENSION(max_domains) :: aer_ssa_val
real , DIMENSION(max_domains) :: aer_asy_val
logical , DIMENSION(max_domains) :: cu_rad_feedback
integer :: dust_emis
integer :: erosion_dim
integer :: no_src_types_cu
integer :: alevsiz_cu
integer :: aercu_opt
real :: aercu_fct
integer :: aercu_used
logical :: couple_farms
logical , DIMENSION(max_domains) :: shallowcu_forced_ra
integer , DIMENSION(max_domains) :: numbins
real , DIMENSION(max_domains) :: thbinsize
real , DIMENSION(max_domains) :: rbinsize
real , DIMENSION(max_domains) :: mindeepfreq
real , DIMENSION(max_domains) :: minshallowfreq
integer , DIMENSION(max_domains) :: shcu_aerosols_opt
integer , DIMENSION(max_domains) :: icloud_cu
integer , DIMENSION(max_domains) :: pxlsm_smois_init
integer , DIMENSION(max_domains) :: pxlsm_modis_veg
integer :: omlcall
integer :: sf_ocean_physics
integer :: traj_opt
logical , DIMENSION(max_domains) :: dm_has_traj
integer :: tracercall
integer , DIMENSION(max_domains) :: shalwater_z0
real :: shalwater_depth
real :: omdt
real :: oml_hml0
real :: oml_gamma
real :: oml_relaxation_time
integer :: isftcflx
integer :: iz0tlnd
real :: shadlen
integer , DIMENSION(max_domains) :: slope_rad
integer , DIMENSION(max_domains) :: topo_shading
integer , DIMENSION(max_domains) :: topo_wind
integer :: no_mp_heating
integer :: fractional_seaice
integer :: seaice_snowdepth_opt
real :: seaice_snowdepth_max
real :: seaice_snowdepth_min
integer :: seaice_albedo_opt
real :: seaice_albedo_default
integer :: seaice_thickness_opt
real :: seaice_thickness_default
logical :: tice2tsk_if2cold
real :: bucket_mm
real :: bucket_j
real :: mp_tend_lim
real , DIMENSION(max_domains) :: prec_acc_dt
integer :: prec_acc_opt
integer :: bucketr_opt
integer :: bucketf_opt
integer :: process_time_series
integer , DIMENSION(max_domains) :: grav_settling
real , DIMENSION(max_domains) :: sas_pgcon
integer , DIMENSION(max_domains) :: scalar_pblmix
integer , DIMENSION(max_domains) :: tracer_pblmix
logical :: use_aero_icbc
logical :: use_rap_aero_icbc
integer :: aer_init_opt
logical :: wif_fire_emit
integer :: aer_fire_emit_opt
integer , DIMENSION(max_domains) :: wif_fire_inj
integer :: use_mp_re
logical :: insert_init_cloud
real :: ccn_conc
integer :: hail_opt
integer :: morr_rimed_ice
integer :: clean_atm_diag
integer :: calc_clean_atm_diag
integer , DIMENSION(max_domains) :: acc_phy_tend
integer :: madwrf_opt
real :: madwrf_dt_relax
real :: madwrf_dt_nudge
integer :: madwrf_cldinit
integer :: dveg
integer :: opt_crs
integer :: opt_btr
integer :: opt_run
integer :: opt_sfc
integer :: opt_frz
integer :: opt_inf
integer :: opt_rad
integer :: opt_alb
integer :: opt_snf
integer :: opt_tbot
integer :: opt_stc
integer :: opt_gla
integer :: opt_rsf
integer :: opt_soil
integer :: opt_pedo
integer :: opt_crop
integer :: opt_irr
integer :: opt_irrm
integer :: opt_infdv
integer :: opt_tdrn
real :: soiltstep
real , DIMENSION(max_domains) :: wtddt
real :: noahmp_acc_dt
integer :: noahmp_output
integer :: wrf_hydro
real , DIMENSION(max_domains) :: fgdt
integer , DIMENSION(max_domains) :: fgdtzero
integer , DIMENSION(max_domains) :: grid_fdda
integer , DIMENSION(max_domains) :: grid_sfdda
integer , DIMENSION(max_domains) :: if_no_pbl_nudging_uv
integer , DIMENSION(max_domains) :: if_no_pbl_nudging_t
integer , DIMENSION(max_domains) :: if_no_pbl_nudging_ph
integer , DIMENSION(max_domains) :: if_no_pbl_nudging_q
integer , DIMENSION(max_domains) :: if_zfac_uv
integer , DIMENSION(max_domains) :: k_zfac_uv
integer , DIMENSION(max_domains) :: if_zfac_t
integer , DIMENSION(max_domains) :: k_zfac_t
integer , DIMENSION(max_domains) :: if_zfac_ph
integer , DIMENSION(max_domains) :: k_zfac_ph
integer , DIMENSION(max_domains) :: if_zfac_q
integer , DIMENSION(max_domains) :: k_zfac_q
integer , DIMENSION(max_domains) :: dk_zfac_uv
integer , DIMENSION(max_domains) :: dk_zfac_t
integer , DIMENSION(max_domains) :: dk_zfac_ph
integer , DIMENSION(max_domains) :: dk_zfac_q
integer :: ktrop
real , DIMENSION(max_domains) :: guv
real , DIMENSION(max_domains) :: guv_sfc
real , DIMENSION(max_domains) :: gt
real , DIMENSION(max_domains) :: gt_sfc
real , DIMENSION(max_domains) :: gq
real , DIMENSION(max_domains) :: gq_sfc
real , DIMENSION(max_domains) :: gph
real :: dtramp_min
integer :: if_ramping
real , DIMENSION(max_domains) :: rinblw
integer , DIMENSION(max_domains) :: xwavenum
integer , DIMENSION(max_domains) :: ywavenum
integer , DIMENSION(max_domains) :: pxlsm_soil_nudge
integer , DIMENSION(max_domains) :: fasdas
integer , DIMENSION(max_domains) :: obs_nudge_opt
integer :: max_obs
real , DIMENSION(max_domains) :: fdda_start
real , DIMENSION(max_domains) :: fdda_end
integer , DIMENSION(max_domains) :: obs_nudge_wind
real , DIMENSION(max_domains) :: obs_coef_wind
integer , DIMENSION(max_domains) :: obs_nudge_temp
real , DIMENSION(max_domains) :: obs_coef_temp
integer , DIMENSION(max_domains) :: obs_nudge_mois
real , DIMENSION(max_domains) :: obs_coef_mois
integer , DIMENSION(max_domains) :: obs_nudge_pstr
real , DIMENSION(max_domains) :: obs_coef_pstr
integer , DIMENSION(max_domains) :: obs_no_pbl_nudge_uv
integer , DIMENSION(max_domains) :: obs_no_pbl_nudge_t
integer , DIMENSION(max_domains) :: obs_no_pbl_nudge_q
integer :: obs_sfc_scheme_horiz
integer :: obs_sfc_scheme_vert
real :: obs_max_sndng_gap
real :: obs_nudgezfullr1_uv
real :: obs_nudgezrampr1_uv
real :: obs_nudgezfullr2_uv
real :: obs_nudgezrampr2_uv
real :: obs_nudgezfullr4_uv
real :: obs_nudgezrampr4_uv
real :: obs_nudgezfullr1_t
real :: obs_nudgezrampr1_t
real :: obs_nudgezfullr2_t
real :: obs_nudgezrampr2_t
real :: obs_nudgezfullr4_t
real :: obs_nudgezrampr4_t
real :: obs_nudgezfullr1_q
real :: obs_nudgezrampr1_q
real :: obs_nudgezfullr2_q
real :: obs_nudgezrampr2_q
real :: obs_nudgezfullr4_q
real :: obs_nudgezrampr4_q
real :: obs_nudgezfullmin
real :: obs_nudgezrampmin
real :: obs_nudgezmax
real :: obs_sfcfact
real :: obs_sfcfacr
real :: obs_dpsmx
real , DIMENSION(max_domains) :: obs_rinxy
real :: obs_rinsig
real , DIMENSION(max_domains) :: obs_twindo
integer :: obs_npfi
integer , DIMENSION(max_domains) :: obs_ionf
integer :: obs_idynin
real :: obs_dtramp
integer :: obs_prt_max
integer , DIMENSION(max_domains) :: obs_prt_freq
logical :: obs_ipf_in4dob
logical :: obs_ipf_errob
logical :: obs_ipf_nudob
logical :: obs_ipf_init
integer :: obs_scl_neg_qv_innov
integer :: scm_force
real :: scm_force_dx
integer :: num_force_layers
integer :: scm_lu_index
integer :: scm_isltyp
real :: scm_vegfra
real :: scm_canwat
real :: scm_lat
real :: scm_lon
logical :: scm_th_t_tend
logical :: scm_qv_t_tend
logical :: scm_th_adv
logical :: scm_wind_adv
logical :: scm_qv_adv
logical :: scm_ql_adv
logical :: scm_vert_adv
integer :: num_force_soil_layers
logical :: scm_soilt_force
logical :: scm_soilq_force
logical :: scm_force_th_largescale
logical :: scm_force_qv_largescale
logical :: scm_force_ql_largescale
logical :: scm_force_wind_largescale
integer :: scm_force_skintemp
integer :: scm_force_flux
integer :: dyn_opt
integer :: rk_ord
integer :: w_damping
real :: w_crit_cfl
integer :: zadvect_implicit
integer , DIMENSION(max_domains) :: diff_opt
integer , DIMENSION(max_domains) :: diff_opt_dfi
integer , DIMENSION(max_domains) :: km_opt
integer , DIMENSION(max_domains) :: km_opt_dfi
integer :: damp_opt
integer :: rad_nudge
integer , DIMENSION(max_domains) :: gwd_opt
integer :: gwd_diags
real , DIMENSION(max_domains) :: zdamp
real , DIMENSION(max_domains) :: dampcoef
real , DIMENSION(max_domains) :: khdif
real , DIMENSION(max_domains) :: kvdif
real , DIMENSION(max_domains) :: diff_6th_factor
integer , DIMENSION(max_domains) :: diff_6th_opt
integer , DIMENSION(max_domains) :: diff_6th_slopeopt
real , DIMENSION(max_domains) :: diff_6th_thresh
integer :: use_theta_m
integer :: use_q_diabatic
real , DIMENSION(max_domains) :: c_s
real , DIMENSION(max_domains) :: c_k
real , DIMENSION(max_domains) :: smdiv
real , DIMENSION(max_domains) :: emdiv
real , DIMENSION(max_domains) :: epssm
logical , DIMENSION(max_domains) :: non_hydrostatic
logical :: use_input_w
integer , DIMENSION(max_domains) :: time_step_sound
integer , DIMENSION(max_domains) :: h_mom_adv_order
integer , DIMENSION(max_domains) :: v_mom_adv_order
integer , DIMENSION(max_domains) :: h_sca_adv_order
integer , DIMENSION(max_domains) :: v_sca_adv_order
integer , DIMENSION(max_domains) :: momentum_adv_opt
integer , DIMENSION(max_domains) :: moist_adv_opt
integer , DIMENSION(max_domains) :: moist_adv_dfi_opt
integer , DIMENSION(max_domains) :: chem_adv_opt
integer , DIMENSION(max_domains) :: tracer_adv_opt
integer , DIMENSION(max_domains) :: scalar_adv_opt
integer , DIMENSION(max_domains) :: tke_adv_opt
integer , DIMENSION(max_domains) :: phi_adv_z
logical , DIMENSION(max_domains) :: moist_mix2_off
logical , DIMENSION(max_domains) :: chem_mix2_off
logical , DIMENSION(max_domains) :: tracer_mix2_off
logical , DIMENSION(max_domains) :: scalar_mix2_off
logical , DIMENSION(max_domains) :: tke_mix2_off
logical , DIMENSION(max_domains) :: moist_mix6_off
logical , DIMENSION(max_domains) :: chem_mix6_off
logical , DIMENSION(max_domains) :: tracer_mix6_off
logical , DIMENSION(max_domains) :: scalar_mix6_off
logical , DIMENSION(max_domains) :: tke_mix6_off
logical , DIMENSION(max_domains) :: top_radiation
integer , DIMENSION(max_domains) :: mix_isotropic
real , DIMENSION(max_domains) :: mix_upper_bound
logical , DIMENSION(max_domains) :: top_lid
real , DIMENSION(max_domains) :: tke_upper_bound
real , DIMENSION(max_domains) :: tke_drag_coefficient
real , DIMENSION(max_domains) :: tke_heat_flux
logical , DIMENSION(max_domains) :: pert_coriolis
logical , DIMENSION(max_domains) :: coriolis2d
logical , DIMENSION(max_domains) :: mix_full_fields
real :: base_pres
real :: base_temp
real :: base_lapse
real :: iso_temp
real :: base_pres_strat
real :: base_lapse_strat
logical :: use_baseparam_fr_nml
real :: fft_filter_lat
logical :: coupled_filtering
logical :: pos_def
logical :: swap_pole_with_next_j
logical :: actual_distance_average
logical :: rotated_pole
logical , DIMENSION(max_domains) :: do_coriolis
logical , DIMENSION(max_domains) :: do_curvature
logical , DIMENSION(max_domains) :: do_gradp
integer :: time_integration_scheme
integer :: sdirk3_max_newton_iter
real :: sdirk3_newton_tol
real :: sdirk3_dt_min_factor
real :: sdirk3_dt_max_factor
real :: sdirk3_safety_factor
logical :: sdirk3_adaptive_dt
logical :: sdirk3_adaptive_timestep
real :: sdirk3_error_tol
integer :: sdirk3_error_norm_type
logical :: sdirk3_use_embedding
logical :: sdirk3_use_adaptive_tol
integer , DIMENSION(max_domains) :: tracer_opt
integer , DIMENSION(max_domains) :: tenddiag
integer :: spec_bdy_width
integer :: spec_zone
integer :: relax_zone
logical , DIMENSION(max_domains) :: specified
logical :: constant_bc
logical , DIMENSION(max_domains) :: periodic_x
logical , DIMENSION(max_domains) :: symmetric_xs
logical , DIMENSION(max_domains) :: symmetric_xe
logical , DIMENSION(max_domains) :: open_xs
logical , DIMENSION(max_domains) :: open_xe
logical , DIMENSION(max_domains) :: periodic_y
logical , DIMENSION(max_domains) :: symmetric_ys
logical , DIMENSION(max_domains) :: symmetric_ye
logical , DIMENSION(max_domains) :: open_ys
logical , DIMENSION(max_domains) :: open_ye
logical , DIMENSION(max_domains) :: polar
logical , DIMENSION(max_domains) :: nested
real :: spec_exp
integer :: real_data_init_type
logical , DIMENSION(max_domains) :: have_bcs_moist
logical , DIMENSION(max_domains) :: have_bcs_scalar
logical :: multi_bdy_files
integer :: background_proc_id
integer :: forecast_proc_id
integer :: production_status
integer :: compression
integer :: nobs_ndg_vars
integer :: nobs_err_flds
real , DIMENSION(max_domains) :: cen_lat
real , DIMENSION(max_domains) :: cen_lon
real , DIMENSION(max_domains) :: truelat1
real , DIMENSION(max_domains) :: truelat2
real , DIMENSION(max_domains) :: moad_cen_lat
real , DIMENSION(max_domains) :: stand_lon
real , DIMENSION(max_domains) :: pole_lat
real , DIMENSION(max_domains) :: pole_lon
integer :: flag_metgrid
integer :: flag_snow
integer :: flag_psfc
integer :: flag_sm000010
integer :: flag_sm010040
integer :: flag_sm040100
integer :: flag_sm100200
integer :: flag_st000010
integer :: flag_st010040
integer :: flag_st040100
integer :: flag_st100200
integer :: flag_soil_layers
integer :: flag_slp
integer :: flag_soilhgt
integer :: flag_mf_xy
integer :: flag_um_soil
real , DIMENSION(max_domains) :: bdyfrq
character*256 , DIMENSION(max_domains) :: mminlu
integer , DIMENSION(max_domains) :: iswater
integer , DIMENSION(max_domains) :: islake
integer , DIMENSION(max_domains) :: isice
integer , DIMENSION(max_domains) :: isurban
integer , DIMENSION(max_domains) :: isoilwater
integer , DIMENSION(max_domains) :: map_proj
integer :: use_wps_input
integer , DIMENSION(max_domains) :: dfi_stage
integer , DIMENSION(max_domains) :: mp_physics_dfi
integer , DIMENSION(max_domains) :: bl_pbl_physics_dfi
integer , DIMENSION(max_domains) :: windfarm_opt
integer :: windfarm_ij
real :: windfarm_tke_factor
integer :: ideal_case
integer , DIMENSION(max_domains) :: hailcast_opt
real , DIMENSION(max_domains) :: haildt
integer , DIMENSION(max_domains) :: lightning_option
real , DIMENSION(max_domains) :: lightning_dt
real , DIMENSION(max_domains) :: lightning_start_seconds
real , DIMENSION(max_domains) :: flashrate_factor
integer , DIMENSION(max_domains) :: iccg_method
real , DIMENSION(max_domains) :: iccg_prescribed_num
real , DIMENSION(max_domains) :: iccg_prescribed_den
integer , DIMENSION(max_domains) :: cellcount_method
real , DIMENSION(max_domains) :: cldtop_adjustment
integer , DIMENSION(max_domains) :: sf_lake_physics
integer , DIMENSION(max_domains) :: windfarm_wake_model
integer , DIMENSION(max_domains) :: windfarm_overlap_method
real , DIMENSION(max_domains) :: windfarm_deg
logical :: use_implicit_scheme
logical :: implicit_acoustic
integer :: dynamics_option
real :: sdirk3_newton_rtol
real :: sdirk3_newton_tol_relax
integer :: sdirk3_gmres_restart
integer :: sdirk3_max_krylov_iter
real :: sdirk3_krylov_tol
integer :: sdirk3_max_gmres_iter
real :: sdirk3_gmres_tol
integer :: sdirk3_precond_type
real :: sdirk3_precond_mu_coupling_damping
logical :: sdirk3_reuse_jacobian
integer :: sdirk3_n_threads
logical :: sdirk3_use_autograd
logical :: sdirk3_use_jvp_cache
integer :: sdirk3_jvp_method
real :: sdirk3_jvp_epsilon
logical :: sdirk3_cache_graphs
integer :: sdirk3_graph_opt_level
integer :: sdirk3_debug_level
logical :: sdirk3_check_nan
logical :: sdirk3_verbose
logical :: sdirk3_check_conservation
integer :: sdirk3_print_frequency
logical :: sdirk3_print_stats
integer :: sdirk3_advection_order
integer :: sdirk3_diffusion_option
real :: sdirk3_diff_coef_h
real :: sdirk3_diff_coef_v
real :: sdirk3_diff4_coef
integer :: sdirk3_lateral_bc_option
integer :: sdirk3_top_bc_option
real :: sdirk3_rayleigh_coef
real :: sdirk3_rayleigh_depth
logical :: sdirk3_check_cfl
real :: sdirk3_cfl_target
integer :: acoustic_ad_mode
character*256 :: auxinput1_inname
integer :: io_form_auxinput1
logical :: override_restart_timers
character*256 :: auxhist1_inname
character*256 :: auxhist1_outname
integer , DIMENSION(max_domains) :: auxhist1_interval_y
integer , DIMENSION(max_domains) :: auxhist1_interval_d
integer , DIMENSION(max_domains) :: auxhist1_interval_h
integer , DIMENSION(max_domains) :: auxhist1_interval_m
integer , DIMENSION(max_domains) :: auxhist1_interval_s
integer , DIMENSION(max_domains) :: auxhist1_interval
integer , DIMENSION(max_domains) :: auxhist1_begin_y
integer , DIMENSION(max_domains) :: auxhist1_begin_d
integer , DIMENSION(max_domains) :: auxhist1_begin_h
integer , DIMENSION(max_domains) :: auxhist1_begin_m
integer , DIMENSION(max_domains) :: auxhist1_begin_s
integer , DIMENSION(max_domains) :: auxhist1_begin
integer , DIMENSION(max_domains) :: auxhist1_end_y
integer , DIMENSION(max_domains) :: auxhist1_end_d
integer , DIMENSION(max_domains) :: auxhist1_end_h
integer , DIMENSION(max_domains) :: auxhist1_end_m
integer , DIMENSION(max_domains) :: auxhist1_end_s
integer , DIMENSION(max_domains) :: auxhist1_end
integer :: io_form_auxhist1
integer , DIMENSION(max_domains) :: frames_per_auxhist1
character*256 :: auxhist2_inname
character*256 :: auxhist2_outname
integer , DIMENSION(max_domains) :: auxhist2_interval_y
integer , DIMENSION(max_domains) :: auxhist2_interval_d
integer , DIMENSION(max_domains) :: auxhist2_interval_h
integer , DIMENSION(max_domains) :: auxhist2_interval_m
integer , DIMENSION(max_domains) :: auxhist2_interval_s
integer , DIMENSION(max_domains) :: auxhist2_interval
integer , DIMENSION(max_domains) :: auxhist2_begin_y
integer , DIMENSION(max_domains) :: auxhist2_begin_d
integer , DIMENSION(max_domains) :: auxhist2_begin_h
integer , DIMENSION(max_domains) :: auxhist2_begin_m
integer , DIMENSION(max_domains) :: auxhist2_begin_s
integer , DIMENSION(max_domains) :: auxhist2_begin
integer , DIMENSION(max_domains) :: auxhist2_end_y
integer , DIMENSION(max_domains) :: auxhist2_end_d
integer , DIMENSION(max_domains) :: auxhist2_end_h
integer , DIMENSION(max_domains) :: auxhist2_end_m
integer , DIMENSION(max_domains) :: auxhist2_end_s
integer , DIMENSION(max_domains) :: auxhist2_end
integer :: io_form_auxhist2
integer , DIMENSION(max_domains) :: frames_per_auxhist2
character*256 :: auxhist3_inname
character*256 :: auxhist3_outname
integer , DIMENSION(max_domains) :: auxhist3_interval_y
integer , DIMENSION(max_domains) :: auxhist3_interval_d
integer , DIMENSION(max_domains) :: auxhist3_interval_h
integer , DIMENSION(max_domains) :: auxhist3_interval_m
integer , DIMENSION(max_domains) :: auxhist3_interval_s
integer , DIMENSION(max_domains) :: auxhist3_interval
integer , DIMENSION(max_domains) :: auxhist3_begin_y
integer , DIMENSION(max_domains) :: auxhist3_begin_d
integer , DIMENSION(max_domains) :: auxhist3_begin_h
integer , DIMENSION(max_domains) :: auxhist3_begin_m
integer , DIMENSION(max_domains) :: auxhist3_begin_s
integer , DIMENSION(max_domains) :: auxhist3_begin
integer , DIMENSION(max_domains) :: auxhist3_end_y
integer , DIMENSION(max_domains) :: auxhist3_end_d
integer , DIMENSION(max_domains) :: auxhist3_end_h
integer , DIMENSION(max_domains) :: auxhist3_end_m
integer , DIMENSION(max_domains) :: auxhist3_end_s
integer , DIMENSION(max_domains) :: auxhist3_end
integer :: io_form_auxhist3
integer , DIMENSION(max_domains) :: frames_per_auxhist3
character*256 :: auxhist4_inname
character*256 :: auxhist4_outname
integer , DIMENSION(max_domains) :: auxhist4_interval_y
integer , DIMENSION(max_domains) :: auxhist4_interval_d
integer , DIMENSION(max_domains) :: auxhist4_interval_h
integer , DIMENSION(max_domains) :: auxhist4_interval_m
integer , DIMENSION(max_domains) :: auxhist4_interval_s
integer , DIMENSION(max_domains) :: auxhist4_interval
integer , DIMENSION(max_domains) :: auxhist4_begin_y
integer , DIMENSION(max_domains) :: auxhist4_begin_d
integer , DIMENSION(max_domains) :: auxhist4_begin_h
integer , DIMENSION(max_domains) :: auxhist4_begin_m
integer , DIMENSION(max_domains) :: auxhist4_begin_s
integer , DIMENSION(max_domains) :: auxhist4_begin
integer , DIMENSION(max_domains) :: auxhist4_end_y
integer , DIMENSION(max_domains) :: auxhist4_end_d
integer , DIMENSION(max_domains) :: auxhist4_end_h
integer , DIMENSION(max_domains) :: auxhist4_end_m
integer , DIMENSION(max_domains) :: auxhist4_end_s
integer , DIMENSION(max_domains) :: auxhist4_end
integer :: io_form_auxhist4
integer , DIMENSION(max_domains) :: frames_per_auxhist4
character*256 :: auxhist5_inname
character*256 :: auxhist5_outname
integer , DIMENSION(max_domains) :: auxhist5_interval_y
integer , DIMENSION(max_domains) :: auxhist5_interval_d
integer , DIMENSION(max_domains) :: auxhist5_interval_h
integer , DIMENSION(max_domains) :: auxhist5_interval_m
integer , DIMENSION(max_domains) :: auxhist5_interval_s
integer , DIMENSION(max_domains) :: auxhist5_interval
integer , DIMENSION(max_domains) :: auxhist5_begin_y
integer , DIMENSION(max_domains) :: auxhist5_begin_d
integer , DIMENSION(max_domains) :: auxhist5_begin_h
integer , DIMENSION(max_domains) :: auxhist5_begin_m
integer , DIMENSION(max_domains) :: auxhist5_begin_s
integer , DIMENSION(max_domains) :: auxhist5_begin
integer , DIMENSION(max_domains) :: auxhist5_end_y
integer , DIMENSION(max_domains) :: auxhist5_end_d
integer , DIMENSION(max_domains) :: auxhist5_end_h
integer , DIMENSION(max_domains) :: auxhist5_end_m
integer , DIMENSION(max_domains) :: auxhist5_end_s
integer , DIMENSION(max_domains) :: auxhist5_end
integer :: io_form_auxhist5
integer , DIMENSION(max_domains) :: frames_per_auxhist5
character*256 :: auxhist6_inname
character*256 :: auxhist6_outname
integer , DIMENSION(max_domains) :: auxhist6_interval_y
integer , DIMENSION(max_domains) :: auxhist6_interval_d
integer , DIMENSION(max_domains) :: auxhist6_interval_h
integer , DIMENSION(max_domains) :: auxhist6_interval_m
integer , DIMENSION(max_domains) :: auxhist6_interval_s
integer , DIMENSION(max_domains) :: auxhist6_interval
integer , DIMENSION(max_domains) :: auxhist6_begin_y
integer , DIMENSION(max_domains) :: auxhist6_begin_d
integer , DIMENSION(max_domains) :: auxhist6_begin_h
integer , DIMENSION(max_domains) :: auxhist6_begin_m
integer , DIMENSION(max_domains) :: auxhist6_begin_s
integer , DIMENSION(max_domains) :: auxhist6_begin
integer , DIMENSION(max_domains) :: auxhist6_end_y
integer , DIMENSION(max_domains) :: auxhist6_end_d
integer , DIMENSION(max_domains) :: auxhist6_end_h
integer , DIMENSION(max_domains) :: auxhist6_end_m
integer , DIMENSION(max_domains) :: auxhist6_end_s
integer , DIMENSION(max_domains) :: auxhist6_end
integer :: io_form_auxhist6
integer , DIMENSION(max_domains) :: frames_per_auxhist6
character*256 :: auxhist7_inname
character*256 :: auxhist7_outname
integer , DIMENSION(max_domains) :: auxhist7_interval_y
integer , DIMENSION(max_domains) :: auxhist7_interval_d
integer , DIMENSION(max_domains) :: auxhist7_interval_h
integer , DIMENSION(max_domains) :: auxhist7_interval_m
integer , DIMENSION(max_domains) :: auxhist7_interval_s
integer , DIMENSION(max_domains) :: auxhist7_interval
integer , DIMENSION(max_domains) :: auxhist7_begin_y
integer , DIMENSION(max_domains) :: auxhist7_begin_d
integer , DIMENSION(max_domains) :: auxhist7_begin_h
integer , DIMENSION(max_domains) :: auxhist7_begin_m
integer , DIMENSION(max_domains) :: auxhist7_begin_s
integer , DIMENSION(max_domains) :: auxhist7_begin
integer , DIMENSION(max_domains) :: auxhist7_end_y
integer , DIMENSION(max_domains) :: auxhist7_end_d
integer , DIMENSION(max_domains) :: auxhist7_end_h
integer , DIMENSION(max_domains) :: auxhist7_end_m
integer , DIMENSION(max_domains) :: auxhist7_end_s
integer , DIMENSION(max_domains) :: auxhist7_end
integer :: io_form_auxhist7
integer , DIMENSION(max_domains) :: frames_per_auxhist7
character*256 :: auxhist8_inname
character*256 :: auxhist8_outname
integer , DIMENSION(max_domains) :: auxhist8_interval_y
integer , DIMENSION(max_domains) :: auxhist8_interval_d
integer , DIMENSION(max_domains) :: auxhist8_interval_h
integer , DIMENSION(max_domains) :: auxhist8_interval_m
integer , DIMENSION(max_domains) :: auxhist8_interval_s
integer , DIMENSION(max_domains) :: auxhist8_interval
integer , DIMENSION(max_domains) :: auxhist8_begin_y
integer , DIMENSION(max_domains) :: auxhist8_begin_d
integer , DIMENSION(max_domains) :: auxhist8_begin_h
integer , DIMENSION(max_domains) :: auxhist8_begin_m
integer , DIMENSION(max_domains) :: auxhist8_begin_s
integer , DIMENSION(max_domains) :: auxhist8_begin
integer , DIMENSION(max_domains) :: auxhist8_end_y
integer , DIMENSION(max_domains) :: auxhist8_end_d
integer , DIMENSION(max_domains) :: auxhist8_end_h
integer , DIMENSION(max_domains) :: auxhist8_end_m
integer , DIMENSION(max_domains) :: auxhist8_end_s
integer , DIMENSION(max_domains) :: auxhist8_end
integer :: io_form_auxhist8
integer , DIMENSION(max_domains) :: frames_per_auxhist8
character*256 :: auxhist9_inname
character*256 :: auxhist9_outname
integer , DIMENSION(max_domains) :: auxhist9_interval_y
integer , DIMENSION(max_domains) :: auxhist9_interval_d
integer , DIMENSION(max_domains) :: auxhist9_interval_h
integer , DIMENSION(max_domains) :: auxhist9_interval_m
integer , DIMENSION(max_domains) :: auxhist9_interval_s
integer , DIMENSION(max_domains) :: auxhist9_interval
integer , DIMENSION(max_domains) :: auxhist9_begin_y
integer , DIMENSION(max_domains) :: auxhist9_begin_d
integer , DIMENSION(max_domains) :: auxhist9_begin_h
integer , DIMENSION(max_domains) :: auxhist9_begin_m
integer , DIMENSION(max_domains) :: auxhist9_begin_s
integer , DIMENSION(max_domains) :: auxhist9_begin
integer , DIMENSION(max_domains) :: auxhist9_end_y
integer , DIMENSION(max_domains) :: auxhist9_end_d
integer , DIMENSION(max_domains) :: auxhist9_end_h
integer , DIMENSION(max_domains) :: auxhist9_end_m
integer , DIMENSION(max_domains) :: auxhist9_end_s
integer , DIMENSION(max_domains) :: auxhist9_end
integer :: io_form_auxhist9
integer , DIMENSION(max_domains) :: frames_per_auxhist9
character*256 :: auxhist10_inname
character*256 :: auxhist10_outname
integer , DIMENSION(max_domains) :: auxhist10_interval_y
integer , DIMENSION(max_domains) :: auxhist10_interval_d
integer , DIMENSION(max_domains) :: auxhist10_interval_h
integer , DIMENSION(max_domains) :: auxhist10_interval_m
integer , DIMENSION(max_domains) :: auxhist10_interval_s
integer , DIMENSION(max_domains) :: auxhist10_interval
integer , DIMENSION(max_domains) :: auxhist10_begin_y
integer , DIMENSION(max_domains) :: auxhist10_begin_d
integer , DIMENSION(max_domains) :: auxhist10_begin_h
integer , DIMENSION(max_domains) :: auxhist10_begin_m
integer , DIMENSION(max_domains) :: auxhist10_begin_s
integer , DIMENSION(max_domains) :: auxhist10_begin
integer , DIMENSION(max_domains) :: auxhist10_end_y
integer , DIMENSION(max_domains) :: auxhist10_end_d
integer , DIMENSION(max_domains) :: auxhist10_end_h
integer , DIMENSION(max_domains) :: auxhist10_end_m
integer , DIMENSION(max_domains) :: auxhist10_end_s
integer , DIMENSION(max_domains) :: auxhist10_end
integer :: io_form_auxhist10
integer , DIMENSION(max_domains) :: frames_per_auxhist10
character*256 :: auxhist11_inname
character*256 :: auxhist11_outname
integer , DIMENSION(max_domains) :: auxhist11_interval_y
integer , DIMENSION(max_domains) :: auxhist11_interval_d
integer , DIMENSION(max_domains) :: auxhist11_interval_h
integer , DIMENSION(max_domains) :: auxhist11_interval_m
integer , DIMENSION(max_domains) :: auxhist11_interval_s
integer , DIMENSION(max_domains) :: auxhist11_interval
integer , DIMENSION(max_domains) :: auxhist11_begin_y
integer , DIMENSION(max_domains) :: auxhist11_begin_d
integer , DIMENSION(max_domains) :: auxhist11_begin_h
integer , DIMENSION(max_domains) :: auxhist11_begin_m
integer , DIMENSION(max_domains) :: auxhist11_begin_s
integer , DIMENSION(max_domains) :: auxhist11_begin
integer , DIMENSION(max_domains) :: auxhist11_end_y
integer , DIMENSION(max_domains) :: auxhist11_end_d
integer , DIMENSION(max_domains) :: auxhist11_end_h
integer , DIMENSION(max_domains) :: auxhist11_end_m
integer , DIMENSION(max_domains) :: auxhist11_end_s
integer , DIMENSION(max_domains) :: auxhist11_end
integer :: io_form_auxhist11
integer , DIMENSION(max_domains) :: frames_per_auxhist11
character*256 :: auxhist12_inname
character*256 :: auxhist12_outname
integer , DIMENSION(max_domains) :: auxhist12_interval_y
integer , DIMENSION(max_domains) :: auxhist12_interval_d
integer , DIMENSION(max_domains) :: auxhist12_interval_h
integer , DIMENSION(max_domains) :: auxhist12_interval_m
integer , DIMENSION(max_domains) :: auxhist12_interval_s
integer , DIMENSION(max_domains) :: auxhist12_interval
integer , DIMENSION(max_domains) :: auxhist12_begin_y
integer , DIMENSION(max_domains) :: auxhist12_begin_d
integer , DIMENSION(max_domains) :: auxhist12_begin_h
integer , DIMENSION(max_domains) :: auxhist12_begin_m
integer , DIMENSION(max_domains) :: auxhist12_begin_s
integer , DIMENSION(max_domains) :: auxhist12_begin
integer , DIMENSION(max_domains) :: auxhist12_end_y
integer , DIMENSION(max_domains) :: auxhist12_end_d
integer , DIMENSION(max_domains) :: auxhist12_end_h
integer , DIMENSION(max_domains) :: auxhist12_end_m
integer , DIMENSION(max_domains) :: auxhist12_end_s
integer , DIMENSION(max_domains) :: auxhist12_end
integer :: io_form_auxhist12
integer , DIMENSION(max_domains) :: frames_per_auxhist12
character*256 :: auxhist13_inname
character*256 :: auxhist13_outname
integer , DIMENSION(max_domains) :: auxhist13_interval_y
integer , DIMENSION(max_domains) :: auxhist13_interval_d
integer , DIMENSION(max_domains) :: auxhist13_interval_h
integer , DIMENSION(max_domains) :: auxhist13_interval_m
integer , DIMENSION(max_domains) :: auxhist13_interval_s
integer , DIMENSION(max_domains) :: auxhist13_interval
integer , DIMENSION(max_domains) :: auxhist13_begin_y
integer , DIMENSION(max_domains) :: auxhist13_begin_d
integer , DIMENSION(max_domains) :: auxhist13_begin_h
integer , DIMENSION(max_domains) :: auxhist13_begin_m
integer , DIMENSION(max_domains) :: auxhist13_begin_s
integer , DIMENSION(max_domains) :: auxhist13_begin
integer , DIMENSION(max_domains) :: auxhist13_end_y
integer , DIMENSION(max_domains) :: auxhist13_end_d
integer , DIMENSION(max_domains) :: auxhist13_end_h
integer , DIMENSION(max_domains) :: auxhist13_end_m
integer , DIMENSION(max_domains) :: auxhist13_end_s
integer , DIMENSION(max_domains) :: auxhist13_end
integer :: io_form_auxhist13
integer , DIMENSION(max_domains) :: frames_per_auxhist13
character*256 :: auxhist14_inname
character*256 :: auxhist14_outname
integer , DIMENSION(max_domains) :: auxhist14_interval_y
integer , DIMENSION(max_domains) :: auxhist14_interval_d
integer , DIMENSION(max_domains) :: auxhist14_interval_h
integer , DIMENSION(max_domains) :: auxhist14_interval_m
integer , DIMENSION(max_domains) :: auxhist14_interval_s
integer , DIMENSION(max_domains) :: auxhist14_interval
integer , DIMENSION(max_domains) :: auxhist14_begin_y
integer , DIMENSION(max_domains) :: auxhist14_begin_d
integer , DIMENSION(max_domains) :: auxhist14_begin_h
integer , DIMENSION(max_domains) :: auxhist14_begin_m
integer , DIMENSION(max_domains) :: auxhist14_begin_s
integer , DIMENSION(max_domains) :: auxhist14_begin
integer , DIMENSION(max_domains) :: auxhist14_end_y
integer , DIMENSION(max_domains) :: auxhist14_end_d
integer , DIMENSION(max_domains) :: auxhist14_end_h
integer , DIMENSION(max_domains) :: auxhist14_end_m
integer , DIMENSION(max_domains) :: auxhist14_end_s
integer , DIMENSION(max_domains) :: auxhist14_end
integer :: io_form_auxhist14
integer , DIMENSION(max_domains) :: frames_per_auxhist14
character*256 :: auxhist15_inname
character*256 :: auxhist15_outname
integer , DIMENSION(max_domains) :: auxhist15_interval_y
integer , DIMENSION(max_domains) :: auxhist15_interval_d
integer , DIMENSION(max_domains) :: auxhist15_interval_h
integer , DIMENSION(max_domains) :: auxhist15_interval_m
integer , DIMENSION(max_domains) :: auxhist15_interval_s
integer , DIMENSION(max_domains) :: auxhist15_interval
integer , DIMENSION(max_domains) :: auxhist15_begin_y
integer , DIMENSION(max_domains) :: auxhist15_begin_d
integer , DIMENSION(max_domains) :: auxhist15_begin_h
integer , DIMENSION(max_domains) :: auxhist15_begin_m
integer , DIMENSION(max_domains) :: auxhist15_begin_s
integer , DIMENSION(max_domains) :: auxhist15_begin
integer , DIMENSION(max_domains) :: auxhist15_end_y
integer , DIMENSION(max_domains) :: auxhist15_end_d
integer , DIMENSION(max_domains) :: auxhist15_end_h
integer , DIMENSION(max_domains) :: auxhist15_end_m
integer , DIMENSION(max_domains) :: auxhist15_end_s
integer , DIMENSION(max_domains) :: auxhist15_end
integer :: io_form_auxhist15
integer , DIMENSION(max_domains) :: frames_per_auxhist15
character*256 :: auxhist16_inname
character*256 :: auxhist16_outname
integer , DIMENSION(max_domains) :: auxhist16_interval_y
integer , DIMENSION(max_domains) :: auxhist16_interval_d
integer , DIMENSION(max_domains) :: auxhist16_interval_h
integer , DIMENSION(max_domains) :: auxhist16_interval_m
integer , DIMENSION(max_domains) :: auxhist16_interval_s
integer , DIMENSION(max_domains) :: auxhist16_interval
integer , DIMENSION(max_domains) :: auxhist16_begin_y
integer , DIMENSION(max_domains) :: auxhist16_begin_d
integer , DIMENSION(max_domains) :: auxhist16_begin_h
integer , DIMENSION(max_domains) :: auxhist16_begin_m
integer , DIMENSION(max_domains) :: auxhist16_begin_s
integer , DIMENSION(max_domains) :: auxhist16_begin
integer , DIMENSION(max_domains) :: auxhist16_end_y
integer , DIMENSION(max_domains) :: auxhist16_end_d
integer , DIMENSION(max_domains) :: auxhist16_end_h
integer , DIMENSION(max_domains) :: auxhist16_end_m
integer , DIMENSION(max_domains) :: auxhist16_end_s
integer , DIMENSION(max_domains) :: auxhist16_end
integer :: io_form_auxhist16
integer , DIMENSION(max_domains) :: frames_per_auxhist16
character*256 :: auxhist17_inname
character*256 :: auxhist17_outname
integer , DIMENSION(max_domains) :: auxhist17_interval_y
integer , DIMENSION(max_domains) :: auxhist17_interval_d
integer , DIMENSION(max_domains) :: auxhist17_interval_h
integer , DIMENSION(max_domains) :: auxhist17_interval_m
integer , DIMENSION(max_domains) :: auxhist17_interval_s
integer , DIMENSION(max_domains) :: auxhist17_interval
integer , DIMENSION(max_domains) :: auxhist17_begin_y
integer , DIMENSION(max_domains) :: auxhist17_begin_d
integer , DIMENSION(max_domains) :: auxhist17_begin_h
integer , DIMENSION(max_domains) :: auxhist17_begin_m
integer , DIMENSION(max_domains) :: auxhist17_begin_s
integer , DIMENSION(max_domains) :: auxhist17_begin
integer , DIMENSION(max_domains) :: auxhist17_end_y
integer , DIMENSION(max_domains) :: auxhist17_end_d
integer , DIMENSION(max_domains) :: auxhist17_end_h
integer , DIMENSION(max_domains) :: auxhist17_end_m
integer , DIMENSION(max_domains) :: auxhist17_end_s
integer , DIMENSION(max_domains) :: auxhist17_end
integer :: io_form_auxhist17
integer , DIMENSION(max_domains) :: frames_per_auxhist17
character*256 :: auxhist18_inname
character*256 :: auxhist18_outname
integer , DIMENSION(max_domains) :: auxhist18_interval_y
integer , DIMENSION(max_domains) :: auxhist18_interval_d
integer , DIMENSION(max_domains) :: auxhist18_interval_h
integer , DIMENSION(max_domains) :: auxhist18_interval_m
integer , DIMENSION(max_domains) :: auxhist18_interval_s
integer , DIMENSION(max_domains) :: auxhist18_interval
integer , DIMENSION(max_domains) :: auxhist18_begin_y
integer , DIMENSION(max_domains) :: auxhist18_begin_d
integer , DIMENSION(max_domains) :: auxhist18_begin_h
integer , DIMENSION(max_domains) :: auxhist18_begin_m
integer , DIMENSION(max_domains) :: auxhist18_begin_s
integer , DIMENSION(max_domains) :: auxhist18_begin
integer , DIMENSION(max_domains) :: auxhist18_end_y
integer , DIMENSION(max_domains) :: auxhist18_end_d
integer , DIMENSION(max_domains) :: auxhist18_end_h
integer , DIMENSION(max_domains) :: auxhist18_end_m
integer , DIMENSION(max_domains) :: auxhist18_end_s
integer , DIMENSION(max_domains) :: auxhist18_end
integer :: io_form_auxhist18
integer , DIMENSION(max_domains) :: frames_per_auxhist18
character*256 :: auxhist19_inname
character*256 :: auxhist19_outname
integer , DIMENSION(max_domains) :: auxhist19_interval_y
integer , DIMENSION(max_domains) :: auxhist19_interval_d
integer , DIMENSION(max_domains) :: auxhist19_interval_h
integer , DIMENSION(max_domains) :: auxhist19_interval_m
integer , DIMENSION(max_domains) :: auxhist19_interval_s
integer , DIMENSION(max_domains) :: auxhist19_interval
integer , DIMENSION(max_domains) :: auxhist19_begin_y
integer , DIMENSION(max_domains) :: auxhist19_begin_d
integer , DIMENSION(max_domains) :: auxhist19_begin_h
integer , DIMENSION(max_domains) :: auxhist19_begin_m
integer , DIMENSION(max_domains) :: auxhist19_begin_s
integer , DIMENSION(max_domains) :: auxhist19_begin
integer , DIMENSION(max_domains) :: auxhist19_end_y
integer , DIMENSION(max_domains) :: auxhist19_end_d
integer , DIMENSION(max_domains) :: auxhist19_end_h
integer , DIMENSION(max_domains) :: auxhist19_end_m
integer , DIMENSION(max_domains) :: auxhist19_end_s
integer , DIMENSION(max_domains) :: auxhist19_end
integer :: io_form_auxhist19
integer , DIMENSION(max_domains) :: frames_per_auxhist19
character*256 :: auxhist20_inname
character*256 :: auxhist20_outname
integer , DIMENSION(max_domains) :: auxhist20_interval_y
integer , DIMENSION(max_domains) :: auxhist20_interval_d
integer , DIMENSION(max_domains) :: auxhist20_interval_h
integer , DIMENSION(max_domains) :: auxhist20_interval_m
integer , DIMENSION(max_domains) :: auxhist20_interval_s
integer , DIMENSION(max_domains) :: auxhist20_interval
integer , DIMENSION(max_domains) :: auxhist20_begin_y
integer , DIMENSION(max_domains) :: auxhist20_begin_d
integer , DIMENSION(max_domains) :: auxhist20_begin_h
integer , DIMENSION(max_domains) :: auxhist20_begin_m
integer , DIMENSION(max_domains) :: auxhist20_begin_s
integer , DIMENSION(max_domains) :: auxhist20_begin
integer , DIMENSION(max_domains) :: auxhist20_end_y
integer , DIMENSION(max_domains) :: auxhist20_end_d
integer , DIMENSION(max_domains) :: auxhist20_end_h
integer , DIMENSION(max_domains) :: auxhist20_end_m
integer , DIMENSION(max_domains) :: auxhist20_end_s
integer , DIMENSION(max_domains) :: auxhist20_end
integer :: io_form_auxhist20
integer , DIMENSION(max_domains) :: frames_per_auxhist20
character*256 :: auxhist21_inname
character*256 :: auxhist21_outname
integer , DIMENSION(max_domains) :: auxhist21_interval_y
integer , DIMENSION(max_domains) :: auxhist21_interval_d
integer , DIMENSION(max_domains) :: auxhist21_interval_h
integer , DIMENSION(max_domains) :: auxhist21_interval_m
integer , DIMENSION(max_domains) :: auxhist21_interval_s
integer , DIMENSION(max_domains) :: auxhist21_interval
integer , DIMENSION(max_domains) :: auxhist21_begin_y
integer , DIMENSION(max_domains) :: auxhist21_begin_d
integer , DIMENSION(max_domains) :: auxhist21_begin_h
integer , DIMENSION(max_domains) :: auxhist21_begin_m
integer , DIMENSION(max_domains) :: auxhist21_begin_s
integer , DIMENSION(max_domains) :: auxhist21_begin
integer , DIMENSION(max_domains) :: auxhist21_end_y
integer , DIMENSION(max_domains) :: auxhist21_end_d
integer , DIMENSION(max_domains) :: auxhist21_end_h
integer , DIMENSION(max_domains) :: auxhist21_end_m
integer , DIMENSION(max_domains) :: auxhist21_end_s
integer , DIMENSION(max_domains) :: auxhist21_end
integer :: io_form_auxhist21
integer , DIMENSION(max_domains) :: frames_per_auxhist21
character*256 :: auxhist22_inname
character*256 :: auxhist22_outname
integer , DIMENSION(max_domains) :: auxhist22_interval_y
integer , DIMENSION(max_domains) :: auxhist22_interval_d
integer , DIMENSION(max_domains) :: auxhist22_interval_h
integer , DIMENSION(max_domains) :: auxhist22_interval_m
integer , DIMENSION(max_domains) :: auxhist22_interval_s
integer , DIMENSION(max_domains) :: auxhist22_interval
integer , DIMENSION(max_domains) :: auxhist22_begin_y
integer , DIMENSION(max_domains) :: auxhist22_begin_d
integer , DIMENSION(max_domains) :: auxhist22_begin_h
integer , DIMENSION(max_domains) :: auxhist22_begin_m
integer , DIMENSION(max_domains) :: auxhist22_begin_s
integer , DIMENSION(max_domains) :: auxhist22_begin
integer , DIMENSION(max_domains) :: auxhist22_end_y
integer , DIMENSION(max_domains) :: auxhist22_end_d
integer , DIMENSION(max_domains) :: auxhist22_end_h
integer , DIMENSION(max_domains) :: auxhist22_end_m
integer , DIMENSION(max_domains) :: auxhist22_end_s
integer , DIMENSION(max_domains) :: auxhist22_end
integer :: io_form_auxhist22
integer , DIMENSION(max_domains) :: frames_per_auxhist22
character*256 :: auxhist23_inname
character*256 :: auxhist23_outname
integer , DIMENSION(max_domains) :: auxhist23_interval_y
integer , DIMENSION(max_domains) :: auxhist23_interval_d
integer , DIMENSION(max_domains) :: auxhist23_interval_h
integer , DIMENSION(max_domains) :: auxhist23_interval_m
integer , DIMENSION(max_domains) :: auxhist23_interval_s
integer , DIMENSION(max_domains) :: auxhist23_interval
integer , DIMENSION(max_domains) :: auxhist23_begin_y
integer , DIMENSION(max_domains) :: auxhist23_begin_d
integer , DIMENSION(max_domains) :: auxhist23_begin_h
integer , DIMENSION(max_domains) :: auxhist23_begin_m
integer , DIMENSION(max_domains) :: auxhist23_begin_s
integer , DIMENSION(max_domains) :: auxhist23_begin
integer , DIMENSION(max_domains) :: auxhist23_end_y
integer , DIMENSION(max_domains) :: auxhist23_end_d
integer , DIMENSION(max_domains) :: auxhist23_end_h
integer , DIMENSION(max_domains) :: auxhist23_end_m
integer , DIMENSION(max_domains) :: auxhist23_end_s
integer , DIMENSION(max_domains) :: auxhist23_end
integer :: io_form_auxhist23
integer , DIMENSION(max_domains) :: frames_per_auxhist23
character*256 :: auxhist24_inname
character*256 :: auxhist24_outname
integer , DIMENSION(max_domains) :: auxhist24_interval_y
integer , DIMENSION(max_domains) :: auxhist24_interval_d
integer , DIMENSION(max_domains) :: auxhist24_interval_h
integer , DIMENSION(max_domains) :: auxhist24_interval_m
integer , DIMENSION(max_domains) :: auxhist24_interval_s
integer , DIMENSION(max_domains) :: auxhist24_interval
integer , DIMENSION(max_domains) :: auxhist24_begin_y
integer , DIMENSION(max_domains) :: auxhist24_begin_d
integer , DIMENSION(max_domains) :: auxhist24_begin_h
integer , DIMENSION(max_domains) :: auxhist24_begin_m
integer , DIMENSION(max_domains) :: auxhist24_begin_s
integer , DIMENSION(max_domains) :: auxhist24_begin
integer , DIMENSION(max_domains) :: auxhist24_end_y
integer , DIMENSION(max_domains) :: auxhist24_end_d
integer , DIMENSION(max_domains) :: auxhist24_end_h
integer , DIMENSION(max_domains) :: auxhist24_end_m
integer , DIMENSION(max_domains) :: auxhist24_end_s
integer , DIMENSION(max_domains) :: auxhist24_end
integer :: io_form_auxhist24
integer , DIMENSION(max_domains) :: frames_per_auxhist24
character*256 :: auxinput1_outname
integer , DIMENSION(max_domains) :: auxinput1_interval_y
integer , DIMENSION(max_domains) :: auxinput1_interval_d
integer , DIMENSION(max_domains) :: auxinput1_interval_h
integer , DIMENSION(max_domains) :: auxinput1_interval_m
integer , DIMENSION(max_domains) :: auxinput1_interval_s
integer , DIMENSION(max_domains) :: auxinput1_interval
integer , DIMENSION(max_domains) :: auxinput1_begin_y
integer , DIMENSION(max_domains) :: auxinput1_begin_d
integer , DIMENSION(max_domains) :: auxinput1_begin_h
integer , DIMENSION(max_domains) :: auxinput1_begin_m
integer , DIMENSION(max_domains) :: auxinput1_begin_s
integer , DIMENSION(max_domains) :: auxinput1_begin
integer , DIMENSION(max_domains) :: auxinput1_end_y
integer , DIMENSION(max_domains) :: auxinput1_end_d
integer , DIMENSION(max_domains) :: auxinput1_end_h
integer , DIMENSION(max_domains) :: auxinput1_end_m
integer , DIMENSION(max_domains) :: auxinput1_end_s
integer , DIMENSION(max_domains) :: auxinput1_end
integer , DIMENSION(max_domains) :: frames_per_auxinput1
character*256 :: auxinput2_inname
character*256 :: auxinput2_outname
integer , DIMENSION(max_domains) :: auxinput2_interval_y
integer , DIMENSION(max_domains) :: auxinput2_interval_d
integer , DIMENSION(max_domains) :: auxinput2_interval_h
integer , DIMENSION(max_domains) :: auxinput2_interval_m
integer , DIMENSION(max_domains) :: auxinput2_interval_s
integer , DIMENSION(max_domains) :: auxinput2_interval
integer , DIMENSION(max_domains) :: auxinput2_begin_y
integer , DIMENSION(max_domains) :: auxinput2_begin_d
integer , DIMENSION(max_domains) :: auxinput2_begin_h
integer , DIMENSION(max_domains) :: auxinput2_begin_m
integer , DIMENSION(max_domains) :: auxinput2_begin_s
integer , DIMENSION(max_domains) :: auxinput2_begin
integer , DIMENSION(max_domains) :: auxinput2_end_y
integer , DIMENSION(max_domains) :: auxinput2_end_d
integer , DIMENSION(max_domains) :: auxinput2_end_h
integer , DIMENSION(max_domains) :: auxinput2_end_m
integer , DIMENSION(max_domains) :: auxinput2_end_s
integer , DIMENSION(max_domains) :: auxinput2_end
integer :: io_form_auxinput2
integer , DIMENSION(max_domains) :: frames_per_auxinput2
character*256 :: auxinput3_inname
character*256 :: auxinput3_outname
integer , DIMENSION(max_domains) :: auxinput3_interval_y
integer , DIMENSION(max_domains) :: auxinput3_interval_d
integer , DIMENSION(max_domains) :: auxinput3_interval_h
integer , DIMENSION(max_domains) :: auxinput3_interval_m
integer , DIMENSION(max_domains) :: auxinput3_interval_s
integer , DIMENSION(max_domains) :: auxinput3_interval
integer , DIMENSION(max_domains) :: auxinput3_begin_y
integer , DIMENSION(max_domains) :: auxinput3_begin_d
integer , DIMENSION(max_domains) :: auxinput3_begin_h
integer , DIMENSION(max_domains) :: auxinput3_begin_m
integer , DIMENSION(max_domains) :: auxinput3_begin_s
integer , DIMENSION(max_domains) :: auxinput3_begin
integer , DIMENSION(max_domains) :: auxinput3_end_y
integer , DIMENSION(max_domains) :: auxinput3_end_d
integer , DIMENSION(max_domains) :: auxinput3_end_h
integer , DIMENSION(max_domains) :: auxinput3_end_m
integer , DIMENSION(max_domains) :: auxinput3_end_s
integer , DIMENSION(max_domains) :: auxinput3_end
integer :: io_form_auxinput3
integer , DIMENSION(max_domains) :: frames_per_auxinput3
character*256 :: auxinput4_inname
character*256 :: auxinput4_outname
integer , DIMENSION(max_domains) :: auxinput4_interval_y
integer , DIMENSION(max_domains) :: auxinput4_interval_d
integer , DIMENSION(max_domains) :: auxinput4_interval_h
integer , DIMENSION(max_domains) :: auxinput4_interval_m
integer , DIMENSION(max_domains) :: auxinput4_interval_s
integer , DIMENSION(max_domains) :: auxinput4_interval
integer , DIMENSION(max_domains) :: auxinput4_begin_y
integer , DIMENSION(max_domains) :: auxinput4_begin_d
integer , DIMENSION(max_domains) :: auxinput4_begin_h
integer , DIMENSION(max_domains) :: auxinput4_begin_m
integer , DIMENSION(max_domains) :: auxinput4_begin_s
integer , DIMENSION(max_domains) :: auxinput4_begin
integer , DIMENSION(max_domains) :: auxinput4_end_y
integer , DIMENSION(max_domains) :: auxinput4_end_d
integer , DIMENSION(max_domains) :: auxinput4_end_h
integer , DIMENSION(max_domains) :: auxinput4_end_m
integer , DIMENSION(max_domains) :: auxinput4_end_s
integer , DIMENSION(max_domains) :: auxinput4_end
integer :: io_form_auxinput4
integer , DIMENSION(max_domains) :: frames_per_auxinput4
character*256 :: auxinput5_inname
character*256 :: auxinput5_outname
integer , DIMENSION(max_domains) :: auxinput5_interval_y
integer , DIMENSION(max_domains) :: auxinput5_interval_d
integer , DIMENSION(max_domains) :: auxinput5_interval_h
integer , DIMENSION(max_domains) :: auxinput5_interval_m
integer , DIMENSION(max_domains) :: auxinput5_interval_s
integer , DIMENSION(max_domains) :: auxinput5_interval
integer , DIMENSION(max_domains) :: auxinput5_begin_y
integer , DIMENSION(max_domains) :: auxinput5_begin_d
integer , DIMENSION(max_domains) :: auxinput5_begin_h
integer , DIMENSION(max_domains) :: auxinput5_begin_m
integer , DIMENSION(max_domains) :: auxinput5_begin_s
integer , DIMENSION(max_domains) :: auxinput5_begin
integer , DIMENSION(max_domains) :: auxinput5_end_y
integer , DIMENSION(max_domains) :: auxinput5_end_d
integer , DIMENSION(max_domains) :: auxinput5_end_h
integer , DIMENSION(max_domains) :: auxinput5_end_m
integer , DIMENSION(max_domains) :: auxinput5_end_s
integer , DIMENSION(max_domains) :: auxinput5_end
integer :: io_form_auxinput5
integer , DIMENSION(max_domains) :: frames_per_auxinput5
character*256 :: auxinput6_inname
character*256 :: auxinput6_outname
integer , DIMENSION(max_domains) :: auxinput6_interval_y
integer , DIMENSION(max_domains) :: auxinput6_interval_d
integer , DIMENSION(max_domains) :: auxinput6_interval_h
integer , DIMENSION(max_domains) :: auxinput6_interval_m
integer , DIMENSION(max_domains) :: auxinput6_interval_s
integer , DIMENSION(max_domains) :: auxinput6_interval
integer , DIMENSION(max_domains) :: auxinput6_begin_y
integer , DIMENSION(max_domains) :: auxinput6_begin_d
integer , DIMENSION(max_domains) :: auxinput6_begin_h
integer , DIMENSION(max_domains) :: auxinput6_begin_m
integer , DIMENSION(max_domains) :: auxinput6_begin_s
integer , DIMENSION(max_domains) :: auxinput6_begin
integer , DIMENSION(max_domains) :: auxinput6_end_y
integer , DIMENSION(max_domains) :: auxinput6_end_d
integer , DIMENSION(max_domains) :: auxinput6_end_h
integer , DIMENSION(max_domains) :: auxinput6_end_m
integer , DIMENSION(max_domains) :: auxinput6_end_s
integer , DIMENSION(max_domains) :: auxinput6_end
integer :: io_form_auxinput6
integer , DIMENSION(max_domains) :: frames_per_auxinput6
character*256 :: auxinput7_inname
character*256 :: auxinput7_outname
integer , DIMENSION(max_domains) :: auxinput7_interval_y
integer , DIMENSION(max_domains) :: auxinput7_interval_d
integer , DIMENSION(max_domains) :: auxinput7_interval_h
integer , DIMENSION(max_domains) :: auxinput7_interval_m
integer , DIMENSION(max_domains) :: auxinput7_interval_s
integer , DIMENSION(max_domains) :: auxinput7_interval
integer , DIMENSION(max_domains) :: auxinput7_begin_y
integer , DIMENSION(max_domains) :: auxinput7_begin_d
integer , DIMENSION(max_domains) :: auxinput7_begin_h
integer , DIMENSION(max_domains) :: auxinput7_begin_m
integer , DIMENSION(max_domains) :: auxinput7_begin_s
integer , DIMENSION(max_domains) :: auxinput7_begin
integer , DIMENSION(max_domains) :: auxinput7_end_y
integer , DIMENSION(max_domains) :: auxinput7_end_d
integer , DIMENSION(max_domains) :: auxinput7_end_h
integer , DIMENSION(max_domains) :: auxinput7_end_m
integer , DIMENSION(max_domains) :: auxinput7_end_s
integer , DIMENSION(max_domains) :: auxinput7_end
integer :: io_form_auxinput7
integer , DIMENSION(max_domains) :: frames_per_auxinput7
character*256 :: auxinput8_inname
character*256 :: auxinput8_outname
integer , DIMENSION(max_domains) :: auxinput8_interval_y
integer , DIMENSION(max_domains) :: auxinput8_interval_d
integer , DIMENSION(max_domains) :: auxinput8_interval_h
integer , DIMENSION(max_domains) :: auxinput8_interval_m
integer , DIMENSION(max_domains) :: auxinput8_interval_s
integer , DIMENSION(max_domains) :: auxinput8_interval
integer , DIMENSION(max_domains) :: auxinput8_begin_y
integer , DIMENSION(max_domains) :: auxinput8_begin_d
integer , DIMENSION(max_domains) :: auxinput8_begin_h
integer , DIMENSION(max_domains) :: auxinput8_begin_m
integer , DIMENSION(max_domains) :: auxinput8_begin_s
integer , DIMENSION(max_domains) :: auxinput8_begin
integer , DIMENSION(max_domains) :: auxinput8_end_y
integer , DIMENSION(max_domains) :: auxinput8_end_d
integer , DIMENSION(max_domains) :: auxinput8_end_h
integer , DIMENSION(max_domains) :: auxinput8_end_m
integer , DIMENSION(max_domains) :: auxinput8_end_s
integer , DIMENSION(max_domains) :: auxinput8_end
integer :: io_form_auxinput8
integer , DIMENSION(max_domains) :: frames_per_auxinput8
character*256 :: auxinput9_inname
character*256 :: auxinput9_outname
integer , DIMENSION(max_domains) :: auxinput9_interval_y
integer , DIMENSION(max_domains) :: auxinput9_interval_d
integer , DIMENSION(max_domains) :: auxinput9_interval_h
integer , DIMENSION(max_domains) :: auxinput9_interval_m
integer , DIMENSION(max_domains) :: auxinput9_interval_s
integer , DIMENSION(max_domains) :: auxinput9_interval
integer , DIMENSION(max_domains) :: auxinput9_begin_y
integer , DIMENSION(max_domains) :: auxinput9_begin_d
integer , DIMENSION(max_domains) :: auxinput9_begin_h
integer , DIMENSION(max_domains) :: auxinput9_begin_m
integer , DIMENSION(max_domains) :: auxinput9_begin_s
integer , DIMENSION(max_domains) :: auxinput9_begin
integer , DIMENSION(max_domains) :: auxinput9_end_y
integer , DIMENSION(max_domains) :: auxinput9_end_d
integer , DIMENSION(max_domains) :: auxinput9_end_h
integer , DIMENSION(max_domains) :: auxinput9_end_m
integer , DIMENSION(max_domains) :: auxinput9_end_s
integer , DIMENSION(max_domains) :: auxinput9_end
integer :: io_form_auxinput9
integer , DIMENSION(max_domains) :: frames_per_auxinput9
character*256 :: auxinput10_inname
character*256 :: auxinput10_outname
integer , DIMENSION(max_domains) :: auxinput10_interval_y
integer , DIMENSION(max_domains) :: auxinput10_interval_d
integer , DIMENSION(max_domains) :: auxinput10_interval_h
integer , DIMENSION(max_domains) :: auxinput10_interval_m
integer , DIMENSION(max_domains) :: auxinput10_interval_s
integer , DIMENSION(max_domains) :: auxinput10_interval
integer , DIMENSION(max_domains) :: auxinput10_begin_y
integer , DIMENSION(max_domains) :: auxinput10_begin_d
integer , DIMENSION(max_domains) :: auxinput10_begin_h
integer , DIMENSION(max_domains) :: auxinput10_begin_m
integer , DIMENSION(max_domains) :: auxinput10_begin_s
integer , DIMENSION(max_domains) :: auxinput10_begin
integer , DIMENSION(max_domains) :: auxinput10_end_y
integer , DIMENSION(max_domains) :: auxinput10_end_d
integer , DIMENSION(max_domains) :: auxinput10_end_h
integer , DIMENSION(max_domains) :: auxinput10_end_m
integer , DIMENSION(max_domains) :: auxinput10_end_s
integer , DIMENSION(max_domains) :: auxinput10_end
integer :: io_form_auxinput10
integer , DIMENSION(max_domains) :: frames_per_auxinput10
character*256 :: auxinput11_inname
character*256 :: auxinput11_outname
integer , DIMENSION(max_domains) :: auxinput11_interval_y
integer , DIMENSION(max_domains) :: auxinput11_interval_d
integer , DIMENSION(max_domains) :: auxinput11_interval_h
integer , DIMENSION(max_domains) :: auxinput11_interval_m
integer , DIMENSION(max_domains) :: auxinput11_interval_s
integer , DIMENSION(max_domains) :: auxinput11_interval
integer , DIMENSION(max_domains) :: auxinput11_begin_y
integer , DIMENSION(max_domains) :: auxinput11_begin_d
integer , DIMENSION(max_domains) :: auxinput11_begin_h
integer , DIMENSION(max_domains) :: auxinput11_begin_m
integer , DIMENSION(max_domains) :: auxinput11_begin_s
integer , DIMENSION(max_domains) :: auxinput11_begin
integer , DIMENSION(max_domains) :: auxinput11_end_y
integer , DIMENSION(max_domains) :: auxinput11_end_d
integer , DIMENSION(max_domains) :: auxinput11_end_h
integer , DIMENSION(max_domains) :: auxinput11_end_m
integer , DIMENSION(max_domains) :: auxinput11_end_s
integer , DIMENSION(max_domains) :: auxinput11_end
integer :: io_form_auxinput11
integer , DIMENSION(max_domains) :: frames_per_auxinput11
character*256 :: auxinput12_inname
character*256 :: auxinput12_outname
integer , DIMENSION(max_domains) :: auxinput12_interval_y
integer , DIMENSION(max_domains) :: auxinput12_interval_d
integer , DIMENSION(max_domains) :: auxinput12_interval_h
integer , DIMENSION(max_domains) :: auxinput12_interval_m
integer , DIMENSION(max_domains) :: auxinput12_interval_s
integer , DIMENSION(max_domains) :: auxinput12_interval
integer , DIMENSION(max_domains) :: auxinput12_begin_y
integer , DIMENSION(max_domains) :: auxinput12_begin_d
integer , DIMENSION(max_domains) :: auxinput12_begin_h
integer , DIMENSION(max_domains) :: auxinput12_begin_m
integer , DIMENSION(max_domains) :: auxinput12_begin_s
integer , DIMENSION(max_domains) :: auxinput12_begin
integer , DIMENSION(max_domains) :: auxinput12_end_y
integer , DIMENSION(max_domains) :: auxinput12_end_d
integer , DIMENSION(max_domains) :: auxinput12_end_h
integer , DIMENSION(max_domains) :: auxinput12_end_m
integer , DIMENSION(max_domains) :: auxinput12_end_s
integer , DIMENSION(max_domains) :: auxinput12_end
integer :: io_form_auxinput12
integer , DIMENSION(max_domains) :: frames_per_auxinput12
character*256 :: auxinput13_inname
character*256 :: auxinput13_outname
integer , DIMENSION(max_domains) :: auxinput13_interval_y
integer , DIMENSION(max_domains) :: auxinput13_interval_d
integer , DIMENSION(max_domains) :: auxinput13_interval_h
integer , DIMENSION(max_domains) :: auxinput13_interval_m
integer , DIMENSION(max_domains) :: auxinput13_interval_s
integer , DIMENSION(max_domains) :: auxinput13_interval
integer , DIMENSION(max_domains) :: auxinput13_begin_y
integer , DIMENSION(max_domains) :: auxinput13_begin_d
integer , DIMENSION(max_domains) :: auxinput13_begin_h
integer , DIMENSION(max_domains) :: auxinput13_begin_m
integer , DIMENSION(max_domains) :: auxinput13_begin_s
integer , DIMENSION(max_domains) :: auxinput13_begin
integer , DIMENSION(max_domains) :: auxinput13_end_y
integer , DIMENSION(max_domains) :: auxinput13_end_d
integer , DIMENSION(max_domains) :: auxinput13_end_h
integer , DIMENSION(max_domains) :: auxinput13_end_m
integer , DIMENSION(max_domains) :: auxinput13_end_s
integer , DIMENSION(max_domains) :: auxinput13_end
integer :: io_form_auxinput13
integer , DIMENSION(max_domains) :: frames_per_auxinput13
character*256 :: auxinput14_inname
character*256 :: auxinput14_outname
integer , DIMENSION(max_domains) :: auxinput14_interval_y
integer , DIMENSION(max_domains) :: auxinput14_interval_d
integer , DIMENSION(max_domains) :: auxinput14_interval_h
integer , DIMENSION(max_domains) :: auxinput14_interval_m
integer , DIMENSION(max_domains) :: auxinput14_interval_s
integer , DIMENSION(max_domains) :: auxinput14_interval
integer , DIMENSION(max_domains) :: auxinput14_begin_y
integer , DIMENSION(max_domains) :: auxinput14_begin_d
integer , DIMENSION(max_domains) :: auxinput14_begin_h
integer , DIMENSION(max_domains) :: auxinput14_begin_m
integer , DIMENSION(max_domains) :: auxinput14_begin_s
integer , DIMENSION(max_domains) :: auxinput14_begin
integer , DIMENSION(max_domains) :: auxinput14_end_y
integer , DIMENSION(max_domains) :: auxinput14_end_d
integer , DIMENSION(max_domains) :: auxinput14_end_h
integer , DIMENSION(max_domains) :: auxinput14_end_m
integer , DIMENSION(max_domains) :: auxinput14_end_s
integer , DIMENSION(max_domains) :: auxinput14_end
integer :: io_form_auxinput14
integer , DIMENSION(max_domains) :: frames_per_auxinput14
character*256 :: auxinput15_inname
character*256 :: auxinput15_outname
integer , DIMENSION(max_domains) :: auxinput15_interval_y
integer , DIMENSION(max_domains) :: auxinput15_interval_d
integer , DIMENSION(max_domains) :: auxinput15_interval_h
integer , DIMENSION(max_domains) :: auxinput15_interval_m
integer , DIMENSION(max_domains) :: auxinput15_interval_s
integer , DIMENSION(max_domains) :: auxinput15_interval
integer , DIMENSION(max_domains) :: auxinput15_begin_y
integer , DIMENSION(max_domains) :: auxinput15_begin_d
integer , DIMENSION(max_domains) :: auxinput15_begin_h
integer , DIMENSION(max_domains) :: auxinput15_begin_m
integer , DIMENSION(max_domains) :: auxinput15_begin_s
integer , DIMENSION(max_domains) :: auxinput15_begin
integer , DIMENSION(max_domains) :: auxinput15_end_y
integer , DIMENSION(max_domains) :: auxinput15_end_d
integer , DIMENSION(max_domains) :: auxinput15_end_h
integer , DIMENSION(max_domains) :: auxinput15_end_m
integer , DIMENSION(max_domains) :: auxinput15_end_s
integer , DIMENSION(max_domains) :: auxinput15_end
integer :: io_form_auxinput15
integer , DIMENSION(max_domains) :: frames_per_auxinput15
character*256 :: auxinput16_inname
character*256 :: auxinput16_outname
integer , DIMENSION(max_domains) :: auxinput16_interval_y
integer , DIMENSION(max_domains) :: auxinput16_interval_d
integer , DIMENSION(max_domains) :: auxinput16_interval_h
integer , DIMENSION(max_domains) :: auxinput16_interval_m
integer , DIMENSION(max_domains) :: auxinput16_interval_s
integer , DIMENSION(max_domains) :: auxinput16_interval
integer , DIMENSION(max_domains) :: auxinput16_begin_y
integer , DIMENSION(max_domains) :: auxinput16_begin_d
integer , DIMENSION(max_domains) :: auxinput16_begin_h
integer , DIMENSION(max_domains) :: auxinput16_begin_m
integer , DIMENSION(max_domains) :: auxinput16_begin_s
integer , DIMENSION(max_domains) :: auxinput16_begin
integer , DIMENSION(max_domains) :: auxinput16_end_y
integer , DIMENSION(max_domains) :: auxinput16_end_d
integer , DIMENSION(max_domains) :: auxinput16_end_h
integer , DIMENSION(max_domains) :: auxinput16_end_m
integer , DIMENSION(max_domains) :: auxinput16_end_s
integer , DIMENSION(max_domains) :: auxinput16_end
integer :: io_form_auxinput16
integer , DIMENSION(max_domains) :: frames_per_auxinput16
character*256 :: auxinput17_inname
character*256 :: auxinput17_outname
integer , DIMENSION(max_domains) :: auxinput17_interval_y
integer , DIMENSION(max_domains) :: auxinput17_interval_d
integer , DIMENSION(max_domains) :: auxinput17_interval_h
integer , DIMENSION(max_domains) :: auxinput17_interval_m
integer , DIMENSION(max_domains) :: auxinput17_interval_s
integer , DIMENSION(max_domains) :: auxinput17_interval
integer , DIMENSION(max_domains) :: auxinput17_begin_y
integer , DIMENSION(max_domains) :: auxinput17_begin_d
integer , DIMENSION(max_domains) :: auxinput17_begin_h
integer , DIMENSION(max_domains) :: auxinput17_begin_m
integer , DIMENSION(max_domains) :: auxinput17_begin_s
integer , DIMENSION(max_domains) :: auxinput17_begin
integer , DIMENSION(max_domains) :: auxinput17_end_y
integer , DIMENSION(max_domains) :: auxinput17_end_d
integer , DIMENSION(max_domains) :: auxinput17_end_h
integer , DIMENSION(max_domains) :: auxinput17_end_m
integer , DIMENSION(max_domains) :: auxinput17_end_s
integer , DIMENSION(max_domains) :: auxinput17_end
integer :: io_form_auxinput17
integer , DIMENSION(max_domains) :: frames_per_auxinput17
character*256 :: auxinput18_inname
character*256 :: auxinput18_outname
integer , DIMENSION(max_domains) :: auxinput18_interval_y
integer , DIMENSION(max_domains) :: auxinput18_interval_d
integer , DIMENSION(max_domains) :: auxinput18_interval_h
integer , DIMENSION(max_domains) :: auxinput18_interval_m
integer , DIMENSION(max_domains) :: auxinput18_interval_s
integer , DIMENSION(max_domains) :: auxinput18_interval
integer , DIMENSION(max_domains) :: auxinput18_begin_y
integer , DIMENSION(max_domains) :: auxinput18_begin_d
integer , DIMENSION(max_domains) :: auxinput18_begin_h
integer , DIMENSION(max_domains) :: auxinput18_begin_m
integer , DIMENSION(max_domains) :: auxinput18_begin_s
integer , DIMENSION(max_domains) :: auxinput18_begin
integer , DIMENSION(max_domains) :: auxinput18_end_y
integer , DIMENSION(max_domains) :: auxinput18_end_d
integer , DIMENSION(max_domains) :: auxinput18_end_h
integer , DIMENSION(max_domains) :: auxinput18_end_m
integer , DIMENSION(max_domains) :: auxinput18_end_s
integer , DIMENSION(max_domains) :: auxinput18_end
integer :: io_form_auxinput18
integer , DIMENSION(max_domains) :: frames_per_auxinput18
character*256 :: auxinput19_inname
character*256 :: auxinput19_outname
integer , DIMENSION(max_domains) :: auxinput19_interval_y
integer , DIMENSION(max_domains) :: auxinput19_interval_d
integer , DIMENSION(max_domains) :: auxinput19_interval_h
integer , DIMENSION(max_domains) :: auxinput19_interval_m
integer , DIMENSION(max_domains) :: auxinput19_interval_s
integer , DIMENSION(max_domains) :: auxinput19_interval
integer , DIMENSION(max_domains) :: auxinput19_begin_y
integer , DIMENSION(max_domains) :: auxinput19_begin_d
integer , DIMENSION(max_domains) :: auxinput19_begin_h
integer , DIMENSION(max_domains) :: auxinput19_begin_m
integer , DIMENSION(max_domains) :: auxinput19_begin_s
integer , DIMENSION(max_domains) :: auxinput19_begin
integer , DIMENSION(max_domains) :: auxinput19_end_y
integer , DIMENSION(max_domains) :: auxinput19_end_d
integer , DIMENSION(max_domains) :: auxinput19_end_h
integer , DIMENSION(max_domains) :: auxinput19_end_m
integer , DIMENSION(max_domains) :: auxinput19_end_s
integer , DIMENSION(max_domains) :: auxinput19_end
integer :: io_form_auxinput19
integer , DIMENSION(max_domains) :: frames_per_auxinput19
character*256 :: auxinput20_inname
character*256 :: auxinput20_outname
integer , DIMENSION(max_domains) :: auxinput20_interval_y
integer , DIMENSION(max_domains) :: auxinput20_interval_d
integer , DIMENSION(max_domains) :: auxinput20_interval_h
integer , DIMENSION(max_domains) :: auxinput20_interval_m
integer , DIMENSION(max_domains) :: auxinput20_interval_s
integer , DIMENSION(max_domains) :: auxinput20_interval
integer , DIMENSION(max_domains) :: auxinput20_begin_y
integer , DIMENSION(max_domains) :: auxinput20_begin_d
integer , DIMENSION(max_domains) :: auxinput20_begin_h
integer , DIMENSION(max_domains) :: auxinput20_begin_m
integer , DIMENSION(max_domains) :: auxinput20_begin_s
integer , DIMENSION(max_domains) :: auxinput20_begin
integer , DIMENSION(max_domains) :: auxinput20_end_y
integer , DIMENSION(max_domains) :: auxinput20_end_d
integer , DIMENSION(max_domains) :: auxinput20_end_h
integer , DIMENSION(max_domains) :: auxinput20_end_m
integer , DIMENSION(max_domains) :: auxinput20_end_s
integer , DIMENSION(max_domains) :: auxinput20_end
integer :: io_form_auxinput20
integer , DIMENSION(max_domains) :: frames_per_auxinput20
character*256 :: auxinput21_inname
character*256 :: auxinput21_outname
integer , DIMENSION(max_domains) :: auxinput21_interval_y
integer , DIMENSION(max_domains) :: auxinput21_interval_d
integer , DIMENSION(max_domains) :: auxinput21_interval_h
integer , DIMENSION(max_domains) :: auxinput21_interval_m
integer , DIMENSION(max_domains) :: auxinput21_interval_s
integer , DIMENSION(max_domains) :: auxinput21_interval
integer , DIMENSION(max_domains) :: auxinput21_begin_y
integer , DIMENSION(max_domains) :: auxinput21_begin_d
integer , DIMENSION(max_domains) :: auxinput21_begin_h
integer , DIMENSION(max_domains) :: auxinput21_begin_m
integer , DIMENSION(max_domains) :: auxinput21_begin_s
integer , DIMENSION(max_domains) :: auxinput21_begin
integer , DIMENSION(max_domains) :: auxinput21_end_y
integer , DIMENSION(max_domains) :: auxinput21_end_d
integer , DIMENSION(max_domains) :: auxinput21_end_h
integer , DIMENSION(max_domains) :: auxinput21_end_m
integer , DIMENSION(max_domains) :: auxinput21_end_s
integer , DIMENSION(max_domains) :: auxinput21_end
integer :: io_form_auxinput21
integer , DIMENSION(max_domains) :: frames_per_auxinput21
character*256 :: auxinput22_inname
character*256 :: auxinput22_outname
integer , DIMENSION(max_domains) :: auxinput22_interval_y
integer , DIMENSION(max_domains) :: auxinput22_interval_d
integer , DIMENSION(max_domains) :: auxinput22_interval_h
integer , DIMENSION(max_domains) :: auxinput22_interval_m
integer , DIMENSION(max_domains) :: auxinput22_interval_s
integer , DIMENSION(max_domains) :: auxinput22_interval
integer , DIMENSION(max_domains) :: auxinput22_begin_y
integer , DIMENSION(max_domains) :: auxinput22_begin_d
integer , DIMENSION(max_domains) :: auxinput22_begin_h
integer , DIMENSION(max_domains) :: auxinput22_begin_m
integer , DIMENSION(max_domains) :: auxinput22_begin_s
integer , DIMENSION(max_domains) :: auxinput22_begin
integer , DIMENSION(max_domains) :: auxinput22_end_y
integer , DIMENSION(max_domains) :: auxinput22_end_d
integer , DIMENSION(max_domains) :: auxinput22_end_h
integer , DIMENSION(max_domains) :: auxinput22_end_m
integer , DIMENSION(max_domains) :: auxinput22_end_s
integer , DIMENSION(max_domains) :: auxinput22_end
integer :: io_form_auxinput22
integer , DIMENSION(max_domains) :: frames_per_auxinput22
character*256 :: auxinput23_inname
character*256 :: auxinput23_outname
integer , DIMENSION(max_domains) :: auxinput23_interval_y
integer , DIMENSION(max_domains) :: auxinput23_interval_d
integer , DIMENSION(max_domains) :: auxinput23_interval_h
integer , DIMENSION(max_domains) :: auxinput23_interval_m
integer , DIMENSION(max_domains) :: auxinput23_interval_s
integer , DIMENSION(max_domains) :: auxinput23_interval
integer , DIMENSION(max_domains) :: auxinput23_begin_y
integer , DIMENSION(max_domains) :: auxinput23_begin_d
integer , DIMENSION(max_domains) :: auxinput23_begin_h
integer , DIMENSION(max_domains) :: auxinput23_begin_m
integer , DIMENSION(max_domains) :: auxinput23_begin_s
integer , DIMENSION(max_domains) :: auxinput23_begin
integer , DIMENSION(max_domains) :: auxinput23_end_y
integer , DIMENSION(max_domains) :: auxinput23_end_d
integer , DIMENSION(max_domains) :: auxinput23_end_h
integer , DIMENSION(max_domains) :: auxinput23_end_m
integer , DIMENSION(max_domains) :: auxinput23_end_s
integer , DIMENSION(max_domains) :: auxinput23_end
integer :: io_form_auxinput23
integer , DIMENSION(max_domains) :: frames_per_auxinput23
character*256 :: auxinput24_inname
character*256 :: auxinput24_outname
integer , DIMENSION(max_domains) :: auxinput24_interval_y
integer , DIMENSION(max_domains) :: auxinput24_interval_d
integer , DIMENSION(max_domains) :: auxinput24_interval_h
integer , DIMENSION(max_domains) :: auxinput24_interval_m
integer , DIMENSION(max_domains) :: auxinput24_interval_s
integer , DIMENSION(max_domains) :: auxinput24_interval
integer , DIMENSION(max_domains) :: auxinput24_begin_y
integer , DIMENSION(max_domains) :: auxinput24_begin_d
integer , DIMENSION(max_domains) :: auxinput24_begin_h
integer , DIMENSION(max_domains) :: auxinput24_begin_m
integer , DIMENSION(max_domains) :: auxinput24_begin_s
integer , DIMENSION(max_domains) :: auxinput24_begin
integer , DIMENSION(max_domains) :: auxinput24_end_y
integer , DIMENSION(max_domains) :: auxinput24_end_d
integer , DIMENSION(max_domains) :: auxinput24_end_h
integer , DIMENSION(max_domains) :: auxinput24_end_m
integer , DIMENSION(max_domains) :: auxinput24_end_s
integer , DIMENSION(max_domains) :: auxinput24_end
integer :: io_form_auxinput24
integer , DIMENSION(max_domains) :: frames_per_auxinput24
integer , DIMENSION(max_domains) :: history_interval
integer , DIMENSION(max_domains) :: frames_per_outfile
logical :: restart
integer :: restart_interval
integer :: io_form_input
integer :: io_form_history
integer :: io_form_restart
integer :: io_form_boundary
integer :: debug_level
logical :: self_test_domain
character*256 :: history_outname
character*256 :: history_inname
logical :: use_netcdf_classic
integer , DIMENSION(max_domains) :: history_interval_d
integer , DIMENSION(max_domains) :: history_interval_h
integer , DIMENSION(max_domains) :: history_interval_m
integer , DIMENSION(max_domains) :: history_interval_s
integer , DIMENSION(max_domains) :: inputout_interval_d
integer , DIMENSION(max_domains) :: inputout_interval_h
integer , DIMENSION(max_domains) :: inputout_interval_m
integer , DIMENSION(max_domains) :: inputout_interval_s
integer , DIMENSION(max_domains) :: inputout_interval
integer :: restart_interval_d
integer :: restart_interval_h
integer :: restart_interval_m
integer :: restart_interval_s
integer , DIMENSION(max_domains) :: history_begin_y
integer , DIMENSION(max_domains) :: history_begin_d
integer , DIMENSION(max_domains) :: history_begin_h
integer , DIMENSION(max_domains) :: history_begin_m
integer , DIMENSION(max_domains) :: history_begin_s
integer , DIMENSION(max_domains) :: history_begin
integer , DIMENSION(max_domains) :: inputout_begin_y
integer , DIMENSION(max_domains) :: inputout_begin_d
integer , DIMENSION(max_domains) :: inputout_begin_h
integer , DIMENSION(max_domains) :: inputout_begin_m
integer , DIMENSION(max_domains) :: inputout_begin_s
integer :: restart_begin_y
integer :: restart_begin_d
integer :: restart_begin_h
integer :: restart_begin_m
integer :: restart_begin_s
integer :: restart_begin
integer , DIMENSION(max_domains) :: history_end_y
integer , DIMENSION(max_domains) :: history_end_d
integer , DIMENSION(max_domains) :: history_end_h
integer , DIMENSION(max_domains) :: history_end_m
integer , DIMENSION(max_domains) :: history_end_s
integer , DIMENSION(max_domains) :: history_end
integer , DIMENSION(max_domains) :: inputout_end_y
integer , DIMENSION(max_domains) :: inputout_end_d
integer , DIMENSION(max_domains) :: inputout_end_h
integer , DIMENSION(max_domains) :: inputout_end_m
integer , DIMENSION(max_domains) :: inputout_end_s
integer :: simulation_start_year
integer :: simulation_start_month
integer :: simulation_start_day
integer :: simulation_start_hour
integer :: simulation_start_minute
integer :: simulation_start_second
logical :: reset_simulation_start
integer , DIMENSION(max_domains) :: sr_x
integer , DIMENSION(max_domains) :: sr_y
character*256 :: sgfdda_inname
character*256 :: gfdda_inname
integer , DIMENSION(max_domains) :: sgfdda_interval_d
integer , DIMENSION(max_domains) :: sgfdda_interval_h
integer , DIMENSION(max_domains) :: sgfdda_interval_m
integer , DIMENSION(max_domains) :: sgfdda_interval_s
integer , DIMENSION(max_domains) :: sgfdda_interval_y
integer , DIMENSION(max_domains) :: sgfdda_interval
integer , DIMENSION(max_domains) :: gfdda_interval_d
integer , DIMENSION(max_domains) :: gfdda_interval_h
integer , DIMENSION(max_domains) :: gfdda_interval_m
integer , DIMENSION(max_domains) :: gfdda_interval_s
integer , DIMENSION(max_domains) :: gfdda_interval_y
integer , DIMENSION(max_domains) :: gfdda_interval
integer , DIMENSION(max_domains) :: sgfdda_begin_y
integer , DIMENSION(max_domains) :: sgfdda_begin_d
integer , DIMENSION(max_domains) :: sgfdda_begin_h
integer , DIMENSION(max_domains) :: sgfdda_begin_m
integer , DIMENSION(max_domains) :: sgfdda_begin_s
integer , DIMENSION(max_domains) :: gfdda_begin_y
integer , DIMENSION(max_domains) :: gfdda_begin_d
integer , DIMENSION(max_domains) :: gfdda_begin_h
integer , DIMENSION(max_domains) :: gfdda_begin_m
integer , DIMENSION(max_domains) :: gfdda_begin_s
integer , DIMENSION(max_domains) :: sgfdda_end_y
integer , DIMENSION(max_domains) :: sgfdda_end_d
integer , DIMENSION(max_domains) :: sgfdda_end_h
integer , DIMENSION(max_domains) :: sgfdda_end_m
integer , DIMENSION(max_domains) :: sgfdda_end_s
integer , DIMENSION(max_domains) :: gfdda_end_y
integer , DIMENSION(max_domains) :: gfdda_end_d
integer , DIMENSION(max_domains) :: gfdda_end_h
integer , DIMENSION(max_domains) :: gfdda_end_m
integer , DIMENSION(max_domains) :: gfdda_end_s
integer :: io_form_sgfdda
integer :: io_form_gfdda
character*256 , DIMENSION(max_domains) :: iofields_filename
logical :: ignore_iofields_warning
logical :: ncd_nofill
logical :: adios2_compression_enable
character*256 :: adios2_blosc_compressor
integer :: adios2_numaggregators
integer :: nfmc
logical , DIMENSION(max_domains) :: fmoist_run
logical , DIMENSION(max_domains) :: fmoist_interp
integer , DIMENSION(max_domains) :: fmoisti_run
integer , DIMENSION(max_domains) :: fmoisti_interp
logical , DIMENSION(max_domains) :: fmoist_only
integer , DIMENSION(max_domains) :: fmoist_freq
real , DIMENSION(max_domains) :: fmoist_dt
real :: fmep_decay_tlag
integer , DIMENSION(max_domains) :: ifire
integer , DIMENSION(max_domains) :: fire_boundary_guard
integer , DIMENSION(max_domains) :: fire_num_ignitions
real , DIMENSION(max_domains) :: fire_ignition_ros1
real , DIMENSION(max_domains) :: fire_ignition_start_lon1
real , DIMENSION(max_domains) :: fire_ignition_start_lat1
real , DIMENSION(max_domains) :: fire_ignition_end_lon1
real , DIMENSION(max_domains) :: fire_ignition_end_lat1
real , DIMENSION(max_domains) :: fire_ignition_radius1
real , DIMENSION(max_domains) :: fire_ignition_start_time1
real , DIMENSION(max_domains) :: fire_ignition_end_time1
real , DIMENSION(max_domains) :: fire_ignition_ros2
real , DIMENSION(max_domains) :: fire_ignition_start_lon2
real , DIMENSION(max_domains) :: fire_ignition_start_lat2
real , DIMENSION(max_domains) :: fire_ignition_end_lon2
real , DIMENSION(max_domains) :: fire_ignition_end_lat2
real , DIMENSION(max_domains) :: fire_ignition_radius2
real , DIMENSION(max_domains) :: fire_ignition_start_time2
real , DIMENSION(max_domains) :: fire_ignition_end_time2
real , DIMENSION(max_domains) :: fire_ignition_ros3
real , DIMENSION(max_domains) :: fire_ignition_start_lon3
real , DIMENSION(max_domains) :: fire_ignition_start_lat3
real , DIMENSION(max_domains) :: fire_ignition_end_lon3
real , DIMENSION(max_domains) :: fire_ignition_end_lat3
real , DIMENSION(max_domains) :: fire_ignition_radius3
real , DIMENSION(max_domains) :: fire_ignition_start_time3
real , DIMENSION(max_domains) :: fire_ignition_end_time3
real , DIMENSION(max_domains) :: fire_ignition_ros4
real , DIMENSION(max_domains) :: fire_ignition_start_lon4
real , DIMENSION(max_domains) :: fire_ignition_start_lat4
real , DIMENSION(max_domains) :: fire_ignition_end_lon4
real , DIMENSION(max_domains) :: fire_ignition_end_lat4
real , DIMENSION(max_domains) :: fire_ignition_radius4
real , DIMENSION(max_domains) :: fire_ignition_start_time4
real , DIMENSION(max_domains) :: fire_ignition_end_time4
real , DIMENSION(max_domains) :: fire_ignition_ros5
real , DIMENSION(max_domains) :: fire_ignition_start_lon5
real , DIMENSION(max_domains) :: fire_ignition_start_lat5
real , DIMENSION(max_domains) :: fire_ignition_end_lon5
real , DIMENSION(max_domains) :: fire_ignition_end_lat5
real , DIMENSION(max_domains) :: fire_ignition_radius5
real , DIMENSION(max_domains) :: fire_ignition_start_time5
real , DIMENSION(max_domains) :: fire_ignition_end_time5
real , DIMENSION(max_domains) :: fire_ignition_start_x1
real , DIMENSION(max_domains) :: fire_ignition_start_y1
real , DIMENSION(max_domains) :: fire_ignition_end_x1
real , DIMENSION(max_domains) :: fire_ignition_end_y1
real , DIMENSION(max_domains) :: fire_ignition_start_x2
real , DIMENSION(max_domains) :: fire_ignition_start_y2
real , DIMENSION(max_domains) :: fire_ignition_end_x2
real , DIMENSION(max_domains) :: fire_ignition_end_y2
real , DIMENSION(max_domains) :: fire_ignition_start_x3
real , DIMENSION(max_domains) :: fire_ignition_start_y3
real , DIMENSION(max_domains) :: fire_ignition_end_x3
real , DIMENSION(max_domains) :: fire_ignition_end_y3
real , DIMENSION(max_domains) :: fire_ignition_start_x4
real , DIMENSION(max_domains) :: fire_ignition_start_y4
real , DIMENSION(max_domains) :: fire_ignition_end_x4
real , DIMENSION(max_domains) :: fire_ignition_end_y4
real , DIMENSION(max_domains) :: fire_ignition_start_x5
real , DIMENSION(max_domains) :: fire_ignition_start_y5
real , DIMENSION(max_domains) :: fire_ignition_end_x5
real , DIMENSION(max_domains) :: fire_ignition_end_y5
real , DIMENSION(max_domains) :: fire_lat_init
real , DIMENSION(max_domains) :: fire_lon_init
real , DIMENSION(max_domains) :: fire_ign_time
integer , DIMENSION(max_domains) :: fire_shape
integer , DIMENSION(max_domains) :: fire_sprd_mdl
real , DIMENSION(max_domains) :: fire_crwn_hgt
real , DIMENSION(max_domains) :: fire_ext_grnd
real , DIMENSION(max_domains) :: fire_ext_crwn
integer , DIMENSION(max_domains) :: fire_sfc_flx
real , DIMENSION(max_domains) :: fire_heat_peak
real , DIMENSION(max_domains) :: fire_tg_ub
integer , DIMENSION(max_domains) :: fire_smk_scheme
real , DIMENSION(max_domains) :: fire_smk_peak
real , DIMENSION(max_domains) :: fire_smk_ext
real , DIMENSION(max_domains) :: fire_wind_height
integer , DIMENSION(max_domains) :: fire_fuel_read
integer , DIMENSION(max_domains) :: fire_fuel_cat
integer , DIMENSION(max_domains) :: fire_fmc_read
integer , DIMENSION(max_domains) :: fire_print_msg
integer , DIMENSION(max_domains) :: fire_print_file
integer , DIMENSION(max_domains) :: fire_fuel_left_method
integer , DIMENSION(max_domains) :: fire_fuel_left_irl
integer , DIMENSION(max_domains) :: fire_fuel_left_jrl
integer , DIMENSION(max_domains) :: fire_grows_only
integer , DIMENSION(max_domains) :: fire_upwinding
integer , DIMENSION(max_domains) :: fire_upwind_split
real , DIMENSION(max_domains) :: fire_viscosity
real , DIMENSION(max_domains) :: fire_lfn_ext_up
integer , DIMENSION(max_domains) :: fire_topo_from_atm
integer , DIMENSION(max_domains) :: fire_advection
integer , DIMENSION(max_domains) :: fire_test_steps
real , DIMENSION(max_domains) :: fire_const_time
real , DIMENSION(max_domains) :: fire_const_grnhfx
real , DIMENSION(max_domains) :: fire_const_grnqfx
real , DIMENSION(max_domains) :: fire_atm_feedback
integer , DIMENSION(max_domains) :: fire_mountain_type
real , DIMENSION(max_domains) :: fire_mountain_height
real , DIMENSION(max_domains) :: fire_mountain_start_x
real , DIMENSION(max_domains) :: fire_mountain_start_y
real , DIMENSION(max_domains) :: fire_mountain_end_x
real , DIMENSION(max_domains) :: fire_mountain_end_y
real , DIMENSION(max_domains) :: delt_perturbation
real , DIMENSION(max_domains) :: xrad_perturbation
real , DIMENSION(max_domains) :: yrad_perturbation
real , DIMENSION(max_domains) :: zrad_perturbation
real , DIMENSION(max_domains) :: hght_perturbation
logical , DIMENSION(max_domains) :: stretch_grd
logical , DIMENSION(max_domains) :: stretch_hyp
real , DIMENSION(max_domains) :: z_grd_scale
logical , DIMENSION(max_domains) :: sfc_full_init
integer , DIMENSION(max_domains) :: sfc_lu_index
real , DIMENSION(max_domains) :: sfc_tsk
real , DIMENSION(max_domains) :: sfc_tmn
logical , DIMENSION(max_domains) :: fire_read_lu
logical , DIMENSION(max_domains) :: fire_read_tsk
logical , DIMENSION(max_domains) :: fire_read_tmn
logical , DIMENSION(max_domains) :: fire_read_atm_ht
logical , DIMENSION(max_domains) :: fire_read_fire_ht
logical , DIMENSION(max_domains) :: fire_read_atm_grad
logical , DIMENSION(max_domains) :: fire_read_fire_grad
real , DIMENSION(max_domains) :: sfc_vegfra
real , DIMENSION(max_domains) :: sfc_canwat
integer , DIMENSION(max_domains) :: sfc_ivgtyp
integer , DIMENSION(max_domains) :: sfc_isltyp
logical , DIMENSION(max_domains) :: fire_lsm_reinit
integer , DIMENSION(max_domains) :: fire_lsm_reinit_iter
integer , DIMENSION(max_domains) :: fire_upwinding_reinit
logical , DIMENSION(max_domains) :: fire_is_real_perim
integer , DIMENSION(max_domains) :: fire_lsm_band_ngp
logical , DIMENSION(max_domains) :: fire_lsm_zcoupling
real , DIMENSION(max_domains) :: fire_lsm_zcoupling_ref
real , DIMENSION(max_domains) :: fire_tracer_smoke
real , DIMENSION(max_domains) :: fire_viscosity_bg
real , DIMENSION(max_domains) :: fire_viscosity_band
integer , DIMENSION(max_domains) :: fire_viscosity_ngp
real , DIMENSION(max_domains) :: fire_slope_factor
integer :: fs_array_maxsize
integer :: fs_firebrand_gen_lim
integer :: fs_firebrand_gen_dt
integer :: fs_firebrand_gen_levels
integer :: fs_firebrand_gen_maxhgt
logical :: fs_firebrand_gen_levrand
integer :: fs_firebrand_gen_levrand_seed
integer :: fs_firebrand_gen_mom3d_dt
real :: fs_firebrand_gen_prop_diam
real :: fs_firebrand_gen_prop_effd
real :: fs_firebrand_gen_prop_temp
real :: fs_firebrand_gen_prop_tvel
real :: fs_firebrand_dens
real :: fs_firebrand_dens_char
integer :: fs_firebrand_max_life_dt
real :: fs_firebrand_land_hgt
logical , DIMENSION(max_domains) :: fuel_crosswalk
logical :: trackember
integer , DIMENSION(max_domains) :: do_avgflx_em
integer , DIMENSION(max_domains) :: do_avgflx_cugd
integer , DIMENSION(max_domains) :: multi_perturb
logical , DIMENSION(max_domains) :: pert_farms
real , DIMENSION(max_domains) :: pert_farms_albedo
real , DIMENSION(max_domains) :: pert_farms_aod
real , DIMENSION(max_domains) :: pert_farms_angexp
real , DIMENSION(max_domains) :: pert_farms_aerasy
real , DIMENSION(max_domains) :: pert_farms_qv
real , DIMENSION(max_domains) :: pert_farms_qc
real , DIMENSION(max_domains) :: pert_farms_qs
logical , DIMENSION(max_domains) :: pert_deng
real , DIMENSION(max_domains) :: pert_deng_qv
real , DIMENSION(max_domains) :: pert_deng_qc
real , DIMENSION(max_domains) :: pert_deng_t
real , DIMENSION(max_domains) :: pert_deng_w
logical , DIMENSION(max_domains) :: pert_mynn
real , DIMENSION(max_domains) :: pert_mynn_qv
real , DIMENSION(max_domains) :: pert_mynn_qc
real , DIMENSION(max_domains) :: pert_mynn_t
real , DIMENSION(max_domains) :: pert_mynn_qke
logical , DIMENSION(max_domains) :: pert_noah
real , DIMENSION(max_domains) :: pert_noah_qv
real , DIMENSION(max_domains) :: pert_noah_t
real , DIMENSION(max_domains) :: pert_noah_smois
real , DIMENSION(max_domains) :: pert_noah_tslb
logical , DIMENSION(max_domains) :: pert_thom
real , DIMENSION(max_domains) :: pert_thom_qv
real , DIMENSION(max_domains) :: pert_thom_qc
real , DIMENSION(max_domains) :: pert_thom_qi
real , DIMENSION(max_domains) :: pert_thom_qs
real , DIMENSION(max_domains) :: pert_thom_ni
logical , DIMENSION(max_domains) :: pert_cld3
real , DIMENSION(max_domains) :: pert_cld3_qv
real , DIMENSION(max_domains) :: pert_cld3_t
real , DIMENSION(max_domains) :: spdt
integer :: num_pert_3d
integer :: nens
integer , DIMENSION(max_domains) :: skebs
integer , DIMENSION(max_domains) :: stoch_force_opt
integer :: skebs_vertstruc
integer , DIMENSION(max_domains) :: stoch_vertstruc_opt
real , DIMENSION(max_domains) :: tot_backscat_psi
real , DIMENSION(max_domains) :: tot_backscat_t
real :: ztau_psi
real :: ztau_t
real :: rexponent_psi
real :: rexponent_t
real :: zsigma2_eps
real :: zsigma2_eta
integer :: kminforc
integer :: lminforc
integer :: kminforct
integer :: lminforct
integer :: kmaxforc
integer :: lmaxforc
integer :: kmaxforct
integer :: lmaxforct
integer :: iseed_skebs
integer :: kmaxforch
integer :: lmaxforch
integer :: kmaxforcth
integer :: lmaxforcth
integer , DIMENSION(max_domains) :: sppt
real , DIMENSION(max_domains) :: gridpt_stddev_sppt
real , DIMENSION(max_domains) :: stddev_cutoff_sppt
real , DIMENSION(max_domains) :: lengthscale_sppt
real , DIMENSION(max_domains) :: timescale_sppt
integer :: sppt_vertstruc
integer :: iseed_sppt
integer , DIMENSION(max_domains) :: rand_perturb
real , DIMENSION(max_domains) :: gridpt_stddev_rand_pert
real , DIMENSION(max_domains) :: stddev_cutoff_rand_pert
real , DIMENSION(max_domains) :: lengthscale_rand_pert
real , DIMENSION(max_domains) :: timescale_rand_pert
integer :: rand_pert_vertstruc
integer :: iseed_rand_pert
integer , DIMENSION(max_domains) :: spp
logical :: hrrr_cycling
integer , DIMENSION(max_domains) :: spp_conv
real , DIMENSION(max_domains) :: gridpt_stddev_spp_conv
real , DIMENSION(max_domains) :: stddev_cutoff_spp_conv
real , DIMENSION(max_domains) :: lengthscale_spp_conv
real , DIMENSION(max_domains) :: timescale_spp_conv
integer :: vertstruc_spp_conv
integer :: iseed_spp_conv
integer , DIMENSION(max_domains) :: spp_pbl
real , DIMENSION(max_domains) :: gridpt_stddev_spp_pbl
real , DIMENSION(max_domains) :: stddev_cutoff_spp_pbl
real , DIMENSION(max_domains) :: lengthscale_spp_pbl
real , DIMENSION(max_domains) :: timescale_spp_pbl
integer :: vertstruc_spp_pbl
integer :: iseed_spp_pbl
integer , DIMENSION(max_domains) :: spp_lsm
real , DIMENSION(max_domains) :: gridpt_stddev_spp_lsm
real , DIMENSION(max_domains) :: stddev_cutoff_spp_lsm
real , DIMENSION(max_domains) :: lengthscale_spp_lsm
real , DIMENSION(max_domains) :: timescale_spp_lsm
integer :: vertstruc_spp_lsm
integer :: iseed_spp_lsm
integer :: skebs_on
integer :: sppt_on
integer :: spp_on
integer :: rand_perturb_on
integer :: num_stoch_levels
integer :: seed_dim
integer , DIMENSION(max_domains) :: sfs_opt
integer , DIMENSION(max_domains) :: m_opt
real , DIMENSION(max_domains) :: lakedepth_default
real , DIMENSION(max_domains) :: lake_min_elev
integer , DIMENSION(max_domains) :: use_lakedepth
integer , DIMENSION(max_domains) :: sbm_diagnostics
integer , DIMENSION(max_domains) :: afwa_diag_opt
integer , DIMENSION(max_domains) :: afwa_ptype_opt
integer , DIMENSION(max_domains) :: afwa_vil_opt
integer , DIMENSION(max_domains) :: afwa_radar_opt
integer , DIMENSION(max_domains) :: afwa_severe_opt
integer , DIMENSION(max_domains) :: afwa_icing_opt
integer , DIMENSION(max_domains) :: afwa_vis_opt
integer , DIMENSION(max_domains) :: afwa_cloud_opt
integer , DIMENSION(max_domains) :: afwa_therm_opt
integer , DIMENSION(max_domains) :: afwa_turb_opt
integer , DIMENSION(max_domains) :: afwa_buoy_opt
real :: afwa_ptype_ccn_tmp
real :: afwa_ptype_tot_melt
integer :: afwa_bad_data_check
integer :: mean_diag
integer :: mean_freq
integer , DIMENSION(max_domains) :: mean_interval
integer , DIMENSION(max_domains) :: mean_diag_interval
integer , DIMENSION(max_domains) :: mean_diag_interval_s
integer , DIMENSION(max_domains) :: mean_diag_interval_m
integer , DIMENSION(max_domains) :: mean_diag_interval_h
integer , DIMENSION(max_domains) :: mean_diag_interval_d
integer , DIMENSION(max_domains) :: mean_diag_interval_mo
integer :: diurnal_diag
integer , DIMENSION(max_domains) :: nssl_ipelec
integer :: nssl_isaund
integer :: nssl_iscreen
real :: nssl_lightrad
integer :: nssl_idischarge
integer :: nssl_ibrkd
real :: nssl_ecrit
real :: nssl_disfrac
integer :: elec_physics
integer :: perturb_bdy
integer :: perturb_chem_bdy
integer :: hybrid_opt
real :: etac
integer :: num_wif_levels
integer :: wif_input_opt
integer :: diag_nwp2
integer :: solar_diagnostics
integer :: p_lev_diags
integer :: p_lev_diags_dfi
integer :: num_press_levels
real , DIMENSION(max_plevs) :: press_levels
integer :: use_tot_or_hyd_p
integer :: extrap_below_grnd
real :: p_lev_missing
real , DIMENSION(max_domains) :: p_lev_interval
integer :: z_lev_diags
integer :: z_lev_diags_dfi
integer :: num_z_levels
real , DIMENSION(max_zlevs) :: z_levels
real :: z_lev_missing
real , DIMENSION(max_domains) :: z_lev_interval
integer , DIMENSION(max_domains) :: iau
real , DIMENSION(max_domains) :: iau_time_window_sec
integer :: wrf_cmaq_option
integer :: wrf_cmaq_freq
integer :: met_file_tstep
logical :: direct_sw_feedback
logical :: feedback_restart
logical :: sdirk3_jvp_checkpointing
integer :: sdirk3_jvp_checkpoint_segments
logical :: sdirk3_jvp_graph_caching
logical :: sdirk3_jvp_batched
integer :: sdirk3_jvp_batch_size
logical :: sdirk3_jvp_mixed_precision
logical :: sdirk3_implicit_acoustic
logical :: sdirk3_implicit_gravity
logical :: sdirk3_implicit_rayleigh
logical :: sdirk3_implicit_wdamp
logical :: sdirk3_implicit_vdiff
logical :: sdirk3_implicit_hdiff
logical :: sdirk3_implicit_divergence
logical :: sdirk3_precond_diagonal_only
logical :: sdirk3_precond_block_jacobi
integer :: sdirk3_precond_block_size
logical :: sdirk3_precond_ilu
integer :: sdirk3_precond_ilu_level
logical :: sdirk3_precond_multigrid
integer :: sdirk3_precond_mg_levels
logical :: sdirk3_nk_adaptive_tol
real :: sdirk3_nk_forcing_eta_max
real :: sdirk3_nk_forcing_eta_min
real :: sdirk3_nk_forcing_gamma
real :: sdirk3_nk_forcing_alpha
logical :: sdirk3_nk_line_search
logical :: sdirk3_nk_trust_region
real :: sdirk3_nk_trust_radius
integer :: sdirk3_stage2_gmres_restart
integer :: sdirk3_stage2_max_krylov_restarts
real :: sdirk3_stage2_krylov_tol
real :: sdirk3_stage2_ew_eta_min
real :: sdirk3_stage2_ew_eta_max
integer :: sdirk3_stage3_gmres_restart
integer :: sdirk3_stage3_max_krylov_restarts
real :: sdirk3_stage3_krylov_tol
real :: sdirk3_stage3_ew_eta_min
real :: sdirk3_stage3_ew_eta_max
integer :: sdirk3_imex_split_mode
logical :: sdirk3_imex_slow_in_tangent
logical :: sdirk3_imex_phys_in_tangent
logical :: sdirk3_stage1_explicit
logical :: sdirk3_stage3_warmstart
integer :: sdirk3_stage_fail_action
integer :: sdirk3_gate_metric_mode
real :: sdirk3_stage3_gate_rel_threshold
logical :: sdirk3_hopeless_relax
logical :: sdirk3_precond_ratio_guard_warn_only
logical :: sdirk3_retain_graph_for_adjoint
logical :: sdirk3_enable_ad_halo_exchange
logical :: sdirk3_stage_boundary_sync
integer :: sdirk3_jvp_auto_bench_calls
logical :: sdirk3_jvp_auto_bench_quality_gate
integer :: sdirk3_jvp_auto_bench_warmup
integer :: sdirk3_jvp_auto_bench_seed
logical :: sdirk3_jvp_auto_bench_lock_reset_stage1
logical :: sdirk3_solver_telemetry
logical :: sdirk3_progress_invariant_enable
real :: sdirk3_progress_invariant_min_ratio
integer :: sdirk3_progress_invariant_streak_limit
logical :: sdirk3_variable_pc_event_log
logical :: sdirk3_gmres_warmstart
real :: sdirk3_gmres_warmstart_quality_gate
logical :: sdirk3_inn_warmstart_enable
logical :: sdirk3_inn_residual_gate_enable
real :: sdirk3_inn_q_min
real :: sdirk3_inn_gate_rel_tol
real :: sdirk3_inn_gate_q_noise
real :: sdirk3_inn_beta_max
logical :: sdirk3_inn_ood_guard_enable
logical :: sdirk3_inn_tol_ramp_enable
real :: sdirk3_inn_tol_ramp_gamma
logical :: sdirk3_rhs_bc_parity
logical :: sdirk3_mass_pgf_bc_guard
real :: sdirk3_precond_horizontal_smooth_alpha
integer :: sdirk3_precond_horizontal_smooth_iters
real :: sdirk3_precond_vertical_smooth_alpha
logical :: sdirk3_precond_smooth_boundary_guard
real :: sdirk3_trust_region_block_aware_thresh
logical :: sdirk3_trust_fallback_relax
real :: sdirk3_trust_fallback_ratio
real :: sdirk3_direct_u_solve_thresh
logical :: sdirk3_memory_pool
integer :: sdirk3_memory_pool_size_mb
logical :: sdirk3_tensor_checkpointing
logical :: sdirk3_gradient_checkpointing
real :: sdirk3_kdamp
real :: sdirk3_khdif
real :: sdirk3_kvdif
real :: sdirk3_w_damp_alpha
real :: sdirk3_w_crit_cfl
integer :: sdirk3_validation_level
logical :: sdirk3_check_staggered
logical :: sdirk3_collect_stats
real :: sdirk3_conserv_tol_mass
real :: sdirk3_conserv_tol_energy
real :: sdirk3_conserv_tol_momentum
logical :: sdirk3_enable_benchmarking
integer :: sdirk3_benchmark_warmup
integer :: sdirk3_benchmark_measure
logical :: sdirk3_save_trajectory
integer :: sdirk3_checkpoint_interval
logical :: sdirk3_obs_aware_4dvar
integer :: sdirk3_obs_source_mode
integer :: sdirk3_obs_window_sync_mode
integer , DIMENSION(max_domains) :: chem_opt
integer    :: last_item_in_struct











NAMELIST /time_control/ run_days
NAMELIST /time_control/ run_hours
NAMELIST /time_control/ run_minutes
NAMELIST /time_control/ run_seconds
NAMELIST /time_control/ start_year
NAMELIST /time_control/ start_month
NAMELIST /time_control/ start_day
NAMELIST /time_control/ start_hour
NAMELIST /time_control/ start_minute
NAMELIST /time_control/ start_second
NAMELIST /time_control/ end_year
NAMELIST /time_control/ end_month
NAMELIST /time_control/ end_day
NAMELIST /time_control/ end_hour
NAMELIST /time_control/ end_minute
NAMELIST /time_control/ end_second
NAMELIST /time_control/ interval_seconds
NAMELIST /time_control/ input_from_file
NAMELIST /time_control/ fine_input_stream
NAMELIST /time_control/ input_from_hires
NAMELIST /time_control/ rsmas_data_path
NAMELIST /time_control/ all_ic_times
NAMELIST /time_control/ julyr
NAMELIST /time_control/ julday
NAMELIST /time_control/ gmt
NAMELIST /time_control/ input_inname
NAMELIST /time_control/ input_outname
NAMELIST /time_control/ bdy_inname
NAMELIST /time_control/ bdy_outname
NAMELIST /time_control/ rst_inname
NAMELIST /time_control/ rst_outname
NAMELIST /time_control/ write_input
NAMELIST /time_control/ write_restart_at_0h
NAMELIST /time_control/ write_hist_at_0h_rst
NAMELIST /time_control/ adjust_output_times
NAMELIST /time_control/ adjust_input_times
NAMELIST /time_control/ diag_print
NAMELIST /time_control/ nocolons
NAMELIST /time_control/ cycling
NAMELIST /time_control/ output_diagnostics
NAMELIST /time_control/ nwp_diagnostics
NAMELIST /time_control/ output_ready_flag
NAMELIST /time_control/ force_use_old_data
NAMELIST /pio_control/ usepio
NAMELIST /pio_control/ pioprocs
NAMELIST /pio_control/ piostart
NAMELIST /pio_control/ piostride
NAMELIST /pio_control/ pioshift
NAMELIST /dfi_control/ dfi_opt
NAMELIST /dfi_control/ dfi_savehydmeteors
NAMELIST /dfi_control/ dfi_nfilter
NAMELIST /dfi_control/ dfi_write_filtered_input
NAMELIST /dfi_control/ dfi_write_dfi_history
NAMELIST /dfi_control/ dfi_cutoff_seconds
NAMELIST /dfi_control/ dfi_time_dim
NAMELIST /dfi_control/ dfi_fwdstop_year
NAMELIST /dfi_control/ dfi_fwdstop_month
NAMELIST /dfi_control/ dfi_fwdstop_day
NAMELIST /dfi_control/ dfi_fwdstop_hour
NAMELIST /dfi_control/ dfi_fwdstop_minute
NAMELIST /dfi_control/ dfi_fwdstop_second
NAMELIST /dfi_control/ dfi_bckstop_year
NAMELIST /dfi_control/ dfi_bckstop_month
NAMELIST /dfi_control/ dfi_bckstop_day
NAMELIST /dfi_control/ dfi_bckstop_hour
NAMELIST /dfi_control/ dfi_bckstop_minute
NAMELIST /dfi_control/ dfi_bckstop_second
NAMELIST /domains/ time_step
NAMELIST /domains/ time_step_fract_num
NAMELIST /domains/ time_step_fract_den
NAMELIST /domains/ time_step_dfi
NAMELIST /domains/ reasonable_time_step_ratio
NAMELIST /domains/ min_time_step
NAMELIST /domains/ min_time_step_den
NAMELIST /domains/ max_time_step
NAMELIST /domains/ max_time_step_den
NAMELIST /domains/ target_cfl
NAMELIST /domains/ target_hcfl
NAMELIST /domains/ max_step_increase_pct
NAMELIST /domains/ starting_time_step
NAMELIST /domains/ starting_time_step_den
NAMELIST /domains/ step_to_output_time
NAMELIST /domains/ adaptation_domain
NAMELIST /domains/ use_adaptive_time_step
NAMELIST /domains/ use_adaptive_time_step_dfi
NAMELIST /domains/ max_dom
NAMELIST /domains/ lats_to_mic
NAMELIST /domains/ s_we
NAMELIST /domains/ e_we
NAMELIST /domains/ s_sn
NAMELIST /domains/ e_sn
NAMELIST /domains/ s_vert
NAMELIST /domains/ e_vert
NAMELIST /domains/ num_metgrid_levels
NAMELIST /domains/ num_metgrid_soil_levels
NAMELIST /domains/ p_top_requested
NAMELIST /domains/ interp_theta
NAMELIST /domains/ interp_type
NAMELIST /domains/ rebalance
NAMELIST /domains/ vert_refine_method
NAMELIST /domains/ vert_refine_fact
NAMELIST /domains/ extrap_type
NAMELIST /domains/ t_extrap_type
NAMELIST /domains/ hypsometric_opt
NAMELIST /domains/ lowest_lev_from_sfc
NAMELIST /domains/ use_levels_below_ground
NAMELIST /domains/ use_tavg_for_tsk
NAMELIST /domains/ use_surface
NAMELIST /domains/ lagrange_order
NAMELIST /domains/ linear_interp
NAMELIST /domains/ force_sfc_in_vinterp
NAMELIST /domains/ zap_close_levels
NAMELIST /domains/ maxw_horiz_pres_diff
NAMELIST /domains/ trop_horiz_pres_diff
NAMELIST /domains/ maxw_above_this_level
NAMELIST /domains/ use_maxw_level
NAMELIST /domains/ use_trop_level
NAMELIST /domains/ sfcp_to_sfcp
NAMELIST /domains/ adjust_heights
NAMELIST /domains/ smooth_cg_topo
NAMELIST /domains/ nest_interp_coord
NAMELIST /domains/ interp_method_type
NAMELIST /domains/ aggregate_lu
NAMELIST /domains/ rh2qv_wrt_liquid
NAMELIST /domains/ rh2qv_method
NAMELIST /domains/ use_sh_qv
NAMELIST /domains/ qv_max_p_safe
NAMELIST /domains/ qv_max_flag
NAMELIST /domains/ qv_max_value
NAMELIST /domains/ qv_min_p_safe
NAMELIST /domains/ qv_min_flag
NAMELIST /domains/ qv_min_value
NAMELIST /domains/ ideal_init_method
NAMELIST /domains/ dx
NAMELIST /domains/ dy
NAMELIST /domains/ grid_id
NAMELIST /domains/ grid_allowed
NAMELIST /domains/ parent_id
NAMELIST /domains/ i_parent_start
NAMELIST /domains/ j_parent_start
NAMELIST /domains/ parent_grid_ratio
NAMELIST /domains/ parent_time_step_ratio
NAMELIST /domains/ feedback
NAMELIST /domains/ smooth_option
NAMELIST /domains/ blend_width
NAMELIST /domains/ ztop
NAMELIST /domains/ moad_grid_ratio
NAMELIST /domains/ moad_time_step_ratio
NAMELIST /domains/ shw
NAMELIST /domains/ tile_sz_x
NAMELIST /domains/ tile_sz_y
NAMELIST /domains/ numtiles
NAMELIST /domains/ numtiles_inc
NAMELIST /domains/ numtiles_x
NAMELIST /domains/ numtiles_y
NAMELIST /domains/ tile_strategy
NAMELIST /domains/ nproc_x
NAMELIST /domains/ nproc_y
NAMELIST /domains/ irand
NAMELIST /domains/ num_moves
NAMELIST /domains/ ts_buf_size
NAMELIST /domains/ max_ts_locs
NAMELIST /domains/ tslist_unstagger_winds
NAMELIST /domains/ vortex_interval
NAMELIST /domains/ max_vortex_speed
NAMELIST /domains/ corral_dist
NAMELIST /domains/ track_level
NAMELIST /domains/ time_to_move
NAMELIST /domains/ move_id
NAMELIST /domains/ move_interval
NAMELIST /domains/ move_cd_x
NAMELIST /domains/ move_cd_y
NAMELIST /domains/ swap_x
NAMELIST /domains/ swap_y
NAMELIST /domains/ cycle_x
NAMELIST /domains/ cycle_y
NAMELIST /domains/ reorder_mesh
NAMELIST /domains/ perturb_input
NAMELIST /domains/ eta_levels
NAMELIST /domains/ auto_levels_opt
NAMELIST /domains/ max_dz
NAMELIST /domains/ dzbot
NAMELIST /domains/ dzstretch_s
NAMELIST /domains/ dzstretch_u
NAMELIST /domains/ ocean_levels
NAMELIST /domains/ ocean_z
NAMELIST /domains/ ocean_t
NAMELIST /domains/ ocean_s
NAMELIST /domains/ num_traj
NAMELIST /domains/ max_ts_level
NAMELIST /domains/ track_loc_in
NAMELIST /domains/ num_ext_model_couple_dom
NAMELIST /tc/ insert_bogus_storm
NAMELIST /tc/ remove_storm
NAMELIST /tc/ num_storm
NAMELIST /tc/ latc_loc
NAMELIST /tc/ lonc_loc
NAMELIST /tc/ vmax_meters_per_second
NAMELIST /tc/ rmax
NAMELIST /tc/ vmax_ratio
NAMELIST /tc/ rankine_lid
NAMELIST /physics/ physics_suite
NAMELIST /physics/ force_read_thompson
NAMELIST /physics/ write_thompson_tables
NAMELIST /physics/ write_thompson_mp38table
NAMELIST /physics/ mp_physics
NAMELIST /physics/ nssl_cccn
NAMELIST /physics/ nssl_alphah
NAMELIST /physics/ nssl_alphahl
NAMELIST /physics/ nssl_cnoh
NAMELIST /physics/ nssl_cnohl
NAMELIST /physics/ nssl_cnor
NAMELIST /physics/ nssl_cnos
NAMELIST /physics/ nssl_rho_qh
NAMELIST /physics/ nssl_rho_qhl
NAMELIST /physics/ nssl_rho_qs
NAMELIST /physics/ nssl_icdx
NAMELIST /physics/ nssl_icdxhl
NAMELIST /physics/ nssl_hail_on
NAMELIST /physics/ nssl_ccn_on
NAMELIST /physics/ nssl_ccn_is_ccna
NAMELIST /physics/ nssl_2moment_on
NAMELIST /physics/ nssl_3moment
NAMELIST /physics/ nssl_density_on
NAMELIST /physics/ nssl_ssat_output
NAMELIST /physics/ ccnty
NAMELIST /physics/ nudge_lightning
NAMELIST /physics/ nudge_light_times
NAMELIST /physics/ nudge_light_timee
NAMELIST /physics/ nudge_light_int
NAMELIST /physics/ path_to_files
NAMELIST /physics/ gsfcgce_hail
NAMELIST /physics/ gsfcgce_2ice
NAMELIST /physics/ progn
NAMELIST /physics/ accum_mode
NAMELIST /physics/ aitken_mode
NAMELIST /physics/ coarse_mode
NAMELIST /physics/ do_radar_ref
NAMELIST /physics/ ra_lw_physics
NAMELIST /physics/ ra_sw_physics
NAMELIST /physics/ ra_sw_eclipse
NAMELIST /physics/ ghg_input
NAMELIST /physics/ radt
NAMELIST /physics/ naer
NAMELIST /physics/ sf_sfclay_physics
NAMELIST /physics/ sf_surf_irr_scheme
NAMELIST /physics/ irr_daily_amount
NAMELIST /physics/ irr_start_hour
NAMELIST /physics/ irr_num_hours
NAMELIST /physics/ irr_start_julianday
NAMELIST /physics/ irr_end_julianday
NAMELIST /physics/ irr_freq
NAMELIST /physics/ irr_ph
NAMELIST /physics/ sf_surface_physics
NAMELIST /physics/ bl_pbl_physics
NAMELIST /physics/ tke_budget
NAMELIST /physics/ ysu_topdown_pblmix
NAMELIST /physics/ shinhong_tke_diag
NAMELIST /physics/ bl_mynn_tkeadvect
NAMELIST /physics/ bl_mynn_cloudpdf
NAMELIST /physics/ bl_mynn_mixlength
NAMELIST /physics/ bl_mynn_edmf
NAMELIST /physics/ bl_mynn_edmf_mom
NAMELIST /physics/ bl_mynn_edmf_tke
NAMELIST /physics/ bl_mynn_mixscalars
NAMELIST /physics/ bl_mynn_output
NAMELIST /physics/ bl_mynn_cloudmix
NAMELIST /physics/ bl_mynn_mixqt
NAMELIST /physics/ icloud_bl
NAMELIST /physics/ bl_mynn_closure
NAMELIST /physics/ mfshconv
NAMELIST /physics/ sf_urban_physics
NAMELIST /physics/ bldt
NAMELIST /physics/ cu_physics
NAMELIST /physics/ shcu_physics
NAMELIST /physics/ cu_diag
NAMELIST /physics/ kf_edrates
NAMELIST /physics/ kfeta_trigger
NAMELIST /physics/ nsas_dx_factor
NAMELIST /physics/ cudt
NAMELIST /physics/ gsmdt
NAMELIST /physics/ isfflx
NAMELIST /physics/ ifsnow
NAMELIST /physics/ icloud
NAMELIST /physics/ cldovrlp
NAMELIST /physics/ idcor
NAMELIST /physics/ ideal_xland
NAMELIST /physics/ swrad_scat
NAMELIST /physics/ surface_input_source
NAMELIST /physics/ num_soil_layers
NAMELIST /physics/ default_soiltype
NAMELIST /physics/ num_pft_clm
NAMELIST /physics/ input_pft
NAMELIST /physics/ maxpatch
NAMELIST /physics/ num_snow_layers
NAMELIST /physics/ num_snso_layers
NAMELIST /physics/ num_urban_hi
NAMELIST /physics/ num_months
NAMELIST /physics/ sf_surface_mosaic
NAMELIST /physics/ mosaic_cat
NAMELIST /physics/ mosaic_lu
NAMELIST /physics/ mosaic_soil
NAMELIST /physics/ flag_sm_adj
NAMELIST /physics/ maxiens
NAMELIST /physics/ maxens
NAMELIST /physics/ maxens2
NAMELIST /physics/ maxens3
NAMELIST /physics/ ensdim
NAMELIST /physics/ cugd_avedx
NAMELIST /physics/ clos_choice
NAMELIST /physics/ imomentum
NAMELIST /physics/ ishallow
NAMELIST /physics/ convtrans_avglen_m
NAMELIST /physics/ num_land_cat
NAMELIST /physics/ use_wudapt_lcz
NAMELIST /physics/ slucm_distributed_drag
NAMELIST /physics/ distributed_ahe_opt
NAMELIST /physics/ num_soil_cat
NAMELIST /physics/ mp_zero_out
NAMELIST /physics/ mp_zero_out_all
NAMELIST /physics/ mp_zero_out_thresh
NAMELIST /physics/ seaice_threshold
NAMELIST /physics/ bmj_rad_feedback
NAMELIST /physics/ sst_update
NAMELIST /physics/ qna_update
NAMELIST /physics/ sst_skin
NAMELIST /physics/ tmn_update
NAMELIST /physics/ usemonalb
NAMELIST /physics/ rdmaxalb
NAMELIST /physics/ rdlai2d
NAMELIST /physics/ ua_phys
NAMELIST /physics/ opt_thcnd
NAMELIST /physics/ co2tf
NAMELIST /physics/ ra_call_offset
NAMELIST /physics/ cam_abs_freq_s
NAMELIST /physics/ levsiz
NAMELIST /physics/ paerlev
NAMELIST /physics/ cam_abs_dim1
NAMELIST /physics/ cam_abs_dim2
NAMELIST /physics/ lagday
NAMELIST /physics/ no_src_types
NAMELIST /physics/ alevsiz
NAMELIST /physics/ o3input
NAMELIST /physics/ aer_opt
NAMELIST /physics/ swint_opt
NAMELIST /physics/ aer_type
NAMELIST /physics/ aer_aod550_opt
NAMELIST /physics/ aer_angexp_opt
NAMELIST /physics/ aer_ssa_opt
NAMELIST /physics/ aer_asy_opt
NAMELIST /physics/ aer_aod550_val
NAMELIST /physics/ aer_angexp_val
NAMELIST /physics/ aer_ssa_val
NAMELIST /physics/ aer_asy_val
NAMELIST /physics/ cu_rad_feedback
NAMELIST /physics/ dust_emis
NAMELIST /physics/ erosion_dim
NAMELIST /physics/ no_src_types_cu
NAMELIST /physics/ alevsiz_cu
NAMELIST /physics/ aercu_opt
NAMELIST /physics/ aercu_fct
NAMELIST /physics/ couple_farms
NAMELIST /physics/ shallowcu_forced_ra
NAMELIST /physics/ numbins
NAMELIST /physics/ thbinsize
NAMELIST /physics/ rbinsize
NAMELIST /physics/ mindeepfreq
NAMELIST /physics/ minshallowfreq
NAMELIST /physics/ shcu_aerosols_opt
NAMELIST /physics/ pxlsm_smois_init
NAMELIST /physics/ pxlsm_modis_veg
NAMELIST /physics/ omlcall
NAMELIST /physics/ sf_ocean_physics
NAMELIST /physics/ traj_opt
NAMELIST /physics/ dm_has_traj
NAMELIST /physics/ tracercall
NAMELIST /physics/ shalwater_z0
NAMELIST /physics/ shalwater_depth
NAMELIST /physics/ omdt
NAMELIST /physics/ oml_hml0
NAMELIST /physics/ oml_gamma
NAMELIST /physics/ oml_relaxation_time
NAMELIST /physics/ isftcflx
NAMELIST /physics/ iz0tlnd
NAMELIST /physics/ shadlen
NAMELIST /physics/ slope_rad
NAMELIST /physics/ topo_shading
NAMELIST /physics/ topo_wind
NAMELIST /physics/ no_mp_heating
NAMELIST /physics/ fractional_seaice
NAMELIST /physics/ seaice_snowdepth_opt
NAMELIST /physics/ seaice_snowdepth_max
NAMELIST /physics/ seaice_snowdepth_min
NAMELIST /physics/ seaice_albedo_opt
NAMELIST /physics/ seaice_albedo_default
NAMELIST /physics/ seaice_thickness_opt
NAMELIST /physics/ seaice_thickness_default
NAMELIST /physics/ tice2tsk_if2cold
NAMELIST /physics/ bucket_mm
NAMELIST /physics/ bucket_j
NAMELIST /physics/ mp_tend_lim
NAMELIST /physics/ prec_acc_dt
NAMELIST /physics/ grav_settling
NAMELIST /physics/ sas_pgcon
NAMELIST /physics/ scalar_pblmix
NAMELIST /physics/ tracer_pblmix
NAMELIST /physics/ use_aero_icbc
NAMELIST /physics/ use_rap_aero_icbc
NAMELIST /physics/ wif_fire_emit
NAMELIST /physics/ wif_fire_inj
NAMELIST /physics/ use_mp_re
NAMELIST /physics/ insert_init_cloud
NAMELIST /physics/ ccn_conc
NAMELIST /physics/ hail_opt
NAMELIST /physics/ morr_rimed_ice
NAMELIST /physics/ clean_atm_diag
NAMELIST /physics/ acc_phy_tend
NAMELIST /physics/ madwrf_opt
NAMELIST /physics/ madwrf_dt_relax
NAMELIST /physics/ madwrf_dt_nudge
NAMELIST /physics/ madwrf_cldinit
NAMELIST /noah_mp/ dveg
NAMELIST /noah_mp/ opt_crs
NAMELIST /noah_mp/ opt_btr
NAMELIST /noah_mp/ opt_run
NAMELIST /noah_mp/ opt_sfc
NAMELIST /noah_mp/ opt_frz
NAMELIST /noah_mp/ opt_inf
NAMELIST /noah_mp/ opt_rad
NAMELIST /noah_mp/ opt_alb
NAMELIST /noah_mp/ opt_snf
NAMELIST /noah_mp/ opt_tbot
NAMELIST /noah_mp/ opt_stc
NAMELIST /noah_mp/ opt_gla
NAMELIST /noah_mp/ opt_rsf
NAMELIST /noah_mp/ opt_soil
NAMELIST /noah_mp/ opt_pedo
NAMELIST /noah_mp/ opt_crop
NAMELIST /noah_mp/ opt_irr
NAMELIST /noah_mp/ opt_irrm
NAMELIST /noah_mp/ opt_infdv
NAMELIST /noah_mp/ opt_tdrn
NAMELIST /noah_mp/ soiltstep
NAMELIST /physics/ wtddt
NAMELIST /noah_mp/ noahmp_acc_dt
NAMELIST /noah_mp/ noahmp_output
NAMELIST /fdda/ fgdt
NAMELIST /fdda/ fgdtzero
NAMELIST /fdda/ grid_fdda
NAMELIST /fdda/ grid_sfdda
NAMELIST /fdda/ if_no_pbl_nudging_uv
NAMELIST /fdda/ if_no_pbl_nudging_t
NAMELIST /fdda/ if_no_pbl_nudging_ph
NAMELIST /fdda/ if_no_pbl_nudging_q
NAMELIST /fdda/ if_zfac_uv
NAMELIST /fdda/ k_zfac_uv
NAMELIST /fdda/ if_zfac_t
NAMELIST /fdda/ k_zfac_t
NAMELIST /fdda/ if_zfac_ph
NAMELIST /fdda/ k_zfac_ph
NAMELIST /fdda/ if_zfac_q
NAMELIST /fdda/ k_zfac_q
NAMELIST /fdda/ dk_zfac_uv
NAMELIST /fdda/ dk_zfac_t
NAMELIST /fdda/ dk_zfac_ph
NAMELIST /fdda/ dk_zfac_q
NAMELIST /fdda/ ktrop
NAMELIST /fdda/ guv
NAMELIST /fdda/ guv_sfc
NAMELIST /fdda/ gt
NAMELIST /fdda/ gt_sfc
NAMELIST /fdda/ gq
NAMELIST /fdda/ gq_sfc
NAMELIST /fdda/ gph
NAMELIST /fdda/ dtramp_min
NAMELIST /fdda/ if_ramping
NAMELIST /fdda/ rinblw
NAMELIST /fdda/ xwavenum
NAMELIST /fdda/ ywavenum
NAMELIST /fdda/ pxlsm_soil_nudge
NAMELIST /fdda/ obs_nudge_opt
NAMELIST /fdda/ max_obs
NAMELIST /fdda/ fdda_start
NAMELIST /fdda/ fdda_end
NAMELIST /fdda/ obs_nudge_wind
NAMELIST /fdda/ obs_coef_wind
NAMELIST /fdda/ obs_nudge_temp
NAMELIST /fdda/ obs_coef_temp
NAMELIST /fdda/ obs_nudge_mois
NAMELIST /fdda/ obs_coef_mois
NAMELIST /fdda/ obs_nudge_pstr
NAMELIST /fdda/ obs_coef_pstr
NAMELIST /fdda/ obs_no_pbl_nudge_uv
NAMELIST /fdda/ obs_no_pbl_nudge_t
NAMELIST /fdda/ obs_no_pbl_nudge_q
NAMELIST /fdda/ obs_sfc_scheme_horiz
NAMELIST /fdda/ obs_sfc_scheme_vert
NAMELIST /fdda/ obs_max_sndng_gap
NAMELIST /fdda/ obs_nudgezfullr1_uv
NAMELIST /fdda/ obs_nudgezrampr1_uv
NAMELIST /fdda/ obs_nudgezfullr2_uv
NAMELIST /fdda/ obs_nudgezrampr2_uv
NAMELIST /fdda/ obs_nudgezfullr4_uv
NAMELIST /fdda/ obs_nudgezrampr4_uv
NAMELIST /fdda/ obs_nudgezfullr1_t
NAMELIST /fdda/ obs_nudgezrampr1_t
NAMELIST /fdda/ obs_nudgezfullr2_t
NAMELIST /fdda/ obs_nudgezrampr2_t
NAMELIST /fdda/ obs_nudgezfullr4_t
NAMELIST /fdda/ obs_nudgezrampr4_t
NAMELIST /fdda/ obs_nudgezfullr1_q
NAMELIST /fdda/ obs_nudgezrampr1_q
NAMELIST /fdda/ obs_nudgezfullr2_q
NAMELIST /fdda/ obs_nudgezrampr2_q
NAMELIST /fdda/ obs_nudgezfullr4_q
NAMELIST /fdda/ obs_nudgezrampr4_q
NAMELIST /fdda/ obs_nudgezfullmin
NAMELIST /fdda/ obs_nudgezrampmin
NAMELIST /fdda/ obs_nudgezmax
NAMELIST /fdda/ obs_sfcfact
NAMELIST /fdda/ obs_sfcfacr
NAMELIST /fdda/ obs_dpsmx
NAMELIST /fdda/ obs_rinxy
NAMELIST /fdda/ obs_rinsig
NAMELIST /fdda/ obs_twindo
NAMELIST /fdda/ obs_npfi
NAMELIST /fdda/ obs_ionf
NAMELIST /fdda/ obs_idynin
NAMELIST /fdda/ obs_dtramp
NAMELIST /fdda/ obs_prt_max
NAMELIST /fdda/ obs_prt_freq
NAMELIST /fdda/ obs_ipf_in4dob
NAMELIST /fdda/ obs_ipf_errob
NAMELIST /fdda/ obs_ipf_nudob
NAMELIST /fdda/ obs_ipf_init
NAMELIST /fdda/ obs_scl_neg_qv_innov
NAMELIST /scm/ scm_force
NAMELIST /scm/ scm_force_dx
NAMELIST /scm/ num_force_layers
NAMELIST /scm/ scm_lu_index
NAMELIST /scm/ scm_isltyp
NAMELIST /scm/ scm_vegfra
NAMELIST /scm/ scm_canwat
NAMELIST /scm/ scm_lat
NAMELIST /scm/ scm_lon
NAMELIST /scm/ scm_th_t_tend
NAMELIST /scm/ scm_qv_t_tend
NAMELIST /scm/ scm_th_adv
NAMELIST /scm/ scm_wind_adv
NAMELIST /scm/ scm_qv_adv
NAMELIST /scm/ scm_ql_adv
NAMELIST /scm/ scm_vert_adv
NAMELIST /scm/ num_force_soil_layers
NAMELIST /scm/ scm_soilt_force
NAMELIST /scm/ scm_soilq_force
NAMELIST /scm/ scm_force_th_largescale
NAMELIST /scm/ scm_force_qv_largescale
NAMELIST /scm/ scm_force_ql_largescale
NAMELIST /scm/ scm_force_wind_largescale
NAMELIST /scm/ scm_force_skintemp
NAMELIST /scm/ scm_force_flux
NAMELIST /dynamics/ dyn_opt
NAMELIST /dynamics/ rk_ord
NAMELIST /dynamics/ w_damping
NAMELIST /dynamics/ w_crit_cfl
NAMELIST /dynamics/ zadvect_implicit
NAMELIST /dynamics/ diff_opt
NAMELIST /dynamics/ diff_opt_dfi
NAMELIST /dynamics/ km_opt
NAMELIST /dynamics/ km_opt_dfi
NAMELIST /dynamics/ damp_opt
NAMELIST /dynamics/ rad_nudge
NAMELIST /dynamics/ gwd_opt
NAMELIST /dynamics/ gwd_diags
NAMELIST /dynamics/ zdamp
NAMELIST /dynamics/ dampcoef
NAMELIST /dynamics/ khdif
NAMELIST /dynamics/ kvdif
NAMELIST /dynamics/ diff_6th_factor
NAMELIST /dynamics/ diff_6th_opt
NAMELIST /dynamics/ diff_6th_slopeopt
NAMELIST /dynamics/ diff_6th_thresh
NAMELIST /dynamics/ use_theta_m
NAMELIST /dynamics/ use_q_diabatic
NAMELIST /dynamics/ c_s
NAMELIST /dynamics/ c_k
NAMELIST /dynamics/ smdiv
NAMELIST /dynamics/ emdiv
NAMELIST /dynamics/ epssm
NAMELIST /dynamics/ non_hydrostatic
NAMELIST /dynamics/ use_input_w
NAMELIST /dynamics/ time_step_sound
NAMELIST /dynamics/ h_mom_adv_order
NAMELIST /dynamics/ v_mom_adv_order
NAMELIST /dynamics/ h_sca_adv_order
NAMELIST /dynamics/ v_sca_adv_order
NAMELIST /dynamics/ momentum_adv_opt
NAMELIST /dynamics/ moist_adv_opt
NAMELIST /dynamics/ moist_adv_dfi_opt
NAMELIST /dynamics/ chem_adv_opt
NAMELIST /dynamics/ tracer_adv_opt
NAMELIST /dynamics/ scalar_adv_opt
NAMELIST /dynamics/ tke_adv_opt
NAMELIST /dynamics/ phi_adv_z
NAMELIST /dynamics/ moist_mix2_off
NAMELIST /dynamics/ chem_mix2_off
NAMELIST /dynamics/ tracer_mix2_off
NAMELIST /dynamics/ scalar_mix2_off
NAMELIST /dynamics/ tke_mix2_off
NAMELIST /dynamics/ moist_mix6_off
NAMELIST /dynamics/ chem_mix6_off
NAMELIST /dynamics/ tracer_mix6_off
NAMELIST /dynamics/ scalar_mix6_off
NAMELIST /dynamics/ tke_mix6_off
NAMELIST /dynamics/ top_radiation
NAMELIST /dynamics/ mix_isotropic
NAMELIST /dynamics/ mix_upper_bound
NAMELIST /dynamics/ top_lid
NAMELIST /dynamics/ tke_upper_bound
NAMELIST /dynamics/ tke_drag_coefficient
NAMELIST /dynamics/ tke_heat_flux
NAMELIST /dynamics/ pert_coriolis
NAMELIST /dynamics/ coriolis2d
NAMELIST /dynamics/ mix_full_fields
NAMELIST /dynamics/ base_pres
NAMELIST /dynamics/ base_temp
NAMELIST /dynamics/ base_lapse
NAMELIST /dynamics/ iso_temp
NAMELIST /dynamics/ base_pres_strat
NAMELIST /dynamics/ base_lapse_strat
NAMELIST /dynamics/ use_baseparam_fr_nml
NAMELIST /dynamics/ fft_filter_lat
NAMELIST /dynamics/ coupled_filtering
NAMELIST /dynamics/ pos_def
NAMELIST /dynamics/ swap_pole_with_next_j
NAMELIST /dynamics/ actual_distance_average
NAMELIST /dynamics/ rotated_pole
NAMELIST /dynamics/ do_coriolis
NAMELIST /dynamics/ do_curvature
NAMELIST /dynamics/ do_gradp
NAMELIST /dynamics/ time_integration_scheme
NAMELIST /dynamics/ sdirk3_max_newton_iter
NAMELIST /dynamics/ sdirk3_newton_tol
NAMELIST /dynamics/ sdirk3_dt_min_factor
NAMELIST /dynamics/ sdirk3_dt_max_factor
NAMELIST /dynamics/ sdirk3_safety_factor
NAMELIST /dynamics/ sdirk3_adaptive_dt
NAMELIST /dynamics/ sdirk3_adaptive_timestep
NAMELIST /dynamics/ sdirk3_error_tol
NAMELIST /dynamics/ sdirk3_error_norm_type
NAMELIST /dynamics/ sdirk3_use_embedding
NAMELIST /dynamics/ sdirk3_use_adaptive_tol
NAMELIST /dynamics/ tracer_opt
NAMELIST /dynamics/ tenddiag
NAMELIST /bdy_control/ spec_bdy_width
NAMELIST /bdy_control/ spec_zone
NAMELIST /bdy_control/ relax_zone
NAMELIST /bdy_control/ specified
NAMELIST /bdy_control/ constant_bc
NAMELIST /bdy_control/ periodic_x
NAMELIST /bdy_control/ symmetric_xs
NAMELIST /bdy_control/ symmetric_xe
NAMELIST /bdy_control/ open_xs
NAMELIST /bdy_control/ open_xe
NAMELIST /bdy_control/ periodic_y
NAMELIST /bdy_control/ symmetric_ys
NAMELIST /bdy_control/ symmetric_ye
NAMELIST /bdy_control/ open_ys
NAMELIST /bdy_control/ open_ye
NAMELIST /bdy_control/ polar
NAMELIST /bdy_control/ nested
NAMELIST /bdy_control/ spec_exp
NAMELIST /bdy_control/ real_data_init_type
NAMELIST /bdy_control/ have_bcs_moist
NAMELIST /bdy_control/ have_bcs_scalar
NAMELIST /bdy_control/ multi_bdy_files
NAMELIST /grib2/ background_proc_id
NAMELIST /grib2/ forecast_proc_id
NAMELIST /grib2/ production_status
NAMELIST /grib2/ compression
NAMELIST /physics/ windfarm_opt
NAMELIST /physics/ windfarm_ij
NAMELIST /physics/ windfarm_tke_factor
NAMELIST /ideal/ ideal_case
NAMELIST /physics/ hailcast_opt
NAMELIST /physics/ haildt
NAMELIST /physics/ lightning_option
NAMELIST /physics/ lightning_dt
NAMELIST /physics/ lightning_start_seconds
NAMELIST /physics/ flashrate_factor
NAMELIST /physics/ iccg_method
NAMELIST /physics/ iccg_prescribed_num
NAMELIST /physics/ iccg_prescribed_den
NAMELIST /physics/ cellcount_method
NAMELIST /physics/ cldtop_adjustment
NAMELIST /physics/ sf_lake_physics
NAMELIST /physics/ windfarm_wake_model
NAMELIST /physics/ windfarm_overlap_method
NAMELIST /physics/ windfarm_deg
NAMELIST /dynamics/ use_implicit_scheme
NAMELIST /dynamics/ implicit_acoustic
NAMELIST /dynamics/ dynamics_option
NAMELIST /dynamics/ sdirk3_newton_rtol
NAMELIST /dynamics/ sdirk3_newton_tol_relax
NAMELIST /dynamics/ sdirk3_gmres_restart
NAMELIST /dynamics/ sdirk3_max_krylov_iter
NAMELIST /dynamics/ sdirk3_krylov_tol
NAMELIST /dynamics/ sdirk3_max_gmres_iter
NAMELIST /dynamics/ sdirk3_gmres_tol
NAMELIST /dynamics/ sdirk3_precond_type
NAMELIST /dynamics/ sdirk3_precond_mu_coupling_damping
NAMELIST /dynamics/ sdirk3_reuse_jacobian
NAMELIST /dynamics/ sdirk3_n_threads
NAMELIST /dynamics/ sdirk3_use_autograd
NAMELIST /dynamics/ sdirk3_use_jvp_cache
NAMELIST /dynamics/ sdirk3_jvp_method
NAMELIST /dynamics/ sdirk3_jvp_epsilon
NAMELIST /dynamics/ sdirk3_cache_graphs
NAMELIST /dynamics/ sdirk3_graph_opt_level
NAMELIST /dynamics/ sdirk3_debug_level
NAMELIST /dynamics/ sdirk3_check_nan
NAMELIST /dynamics/ sdirk3_verbose
NAMELIST /dynamics/ sdirk3_check_conservation
NAMELIST /dynamics/ sdirk3_print_frequency
NAMELIST /dynamics/ sdirk3_print_stats
NAMELIST /dynamics/ sdirk3_advection_order
NAMELIST /dynamics/ sdirk3_diffusion_option
NAMELIST /dynamics/ sdirk3_diff_coef_h
NAMELIST /dynamics/ sdirk3_diff_coef_v
NAMELIST /dynamics/ sdirk3_diff4_coef
NAMELIST /dynamics/ sdirk3_lateral_bc_option
NAMELIST /dynamics/ sdirk3_top_bc_option
NAMELIST /dynamics/ sdirk3_rayleigh_coef
NAMELIST /dynamics/ sdirk3_rayleigh_depth
NAMELIST /dynamics/ sdirk3_check_cfl
NAMELIST /dynamics/ sdirk3_cfl_target
NAMELIST /dynamics/ acoustic_ad_mode
NAMELIST /time_control/ auxinput1_inname
NAMELIST /time_control/ io_form_auxinput1
NAMELIST /time_control/ override_restart_timers
NAMELIST /time_control/ auxhist1_inname
NAMELIST /time_control/ auxhist1_outname
NAMELIST /time_control/ auxhist1_interval_y
NAMELIST /time_control/ auxhist1_interval_d
NAMELIST /time_control/ auxhist1_interval_h
NAMELIST /time_control/ auxhist1_interval_m
NAMELIST /time_control/ auxhist1_interval_s
NAMELIST /time_control/ auxhist1_interval
NAMELIST /time_control/ auxhist1_begin_y
NAMELIST /time_control/ auxhist1_begin_d
NAMELIST /time_control/ auxhist1_begin_h
NAMELIST /time_control/ auxhist1_begin_m
NAMELIST /time_control/ auxhist1_begin_s
NAMELIST /time_control/ auxhist1_begin
NAMELIST /time_control/ auxhist1_end_y
NAMELIST /time_control/ auxhist1_end_d
NAMELIST /time_control/ auxhist1_end_h
NAMELIST /time_control/ auxhist1_end_m
NAMELIST /time_control/ auxhist1_end_s
NAMELIST /time_control/ auxhist1_end
NAMELIST /time_control/ io_form_auxhist1
NAMELIST /time_control/ frames_per_auxhist1
NAMELIST /time_control/ auxhist2_inname
NAMELIST /time_control/ auxhist2_outname
NAMELIST /time_control/ auxhist2_interval_y
NAMELIST /time_control/ auxhist2_interval_d
NAMELIST /time_control/ auxhist2_interval_h
NAMELIST /time_control/ auxhist2_interval_m
NAMELIST /time_control/ auxhist2_interval_s
NAMELIST /time_control/ auxhist2_interval
NAMELIST /time_control/ auxhist2_begin_y
NAMELIST /time_control/ auxhist2_begin_d
NAMELIST /time_control/ auxhist2_begin_h
NAMELIST /time_control/ auxhist2_begin_m
NAMELIST /time_control/ auxhist2_begin_s
NAMELIST /time_control/ auxhist2_begin
NAMELIST /time_control/ auxhist2_end_y
NAMELIST /time_control/ auxhist2_end_d
NAMELIST /time_control/ auxhist2_end_h
NAMELIST /time_control/ auxhist2_end_m
NAMELIST /time_control/ auxhist2_end_s
NAMELIST /time_control/ auxhist2_end
NAMELIST /time_control/ io_form_auxhist2
NAMELIST /time_control/ frames_per_auxhist2
NAMELIST /time_control/ auxhist3_inname
NAMELIST /time_control/ auxhist3_outname
NAMELIST /time_control/ auxhist3_interval_y
NAMELIST /time_control/ auxhist3_interval_d
NAMELIST /time_control/ auxhist3_interval_h
NAMELIST /time_control/ auxhist3_interval_m
NAMELIST /time_control/ auxhist3_interval_s
NAMELIST /time_control/ auxhist3_interval
NAMELIST /time_control/ auxhist3_begin_y
NAMELIST /time_control/ auxhist3_begin_d
NAMELIST /time_control/ auxhist3_begin_h
NAMELIST /time_control/ auxhist3_begin_m
NAMELIST /time_control/ auxhist3_begin_s
NAMELIST /time_control/ auxhist3_begin
NAMELIST /time_control/ auxhist3_end_y
NAMELIST /time_control/ auxhist3_end_d
NAMELIST /time_control/ auxhist3_end_h
NAMELIST /time_control/ auxhist3_end_m
NAMELIST /time_control/ auxhist3_end_s
NAMELIST /time_control/ auxhist3_end
NAMELIST /time_control/ io_form_auxhist3
NAMELIST /time_control/ frames_per_auxhist3
NAMELIST /time_control/ auxhist4_inname
NAMELIST /time_control/ auxhist4_outname
NAMELIST /time_control/ auxhist4_interval_y
NAMELIST /time_control/ auxhist4_interval_d
NAMELIST /time_control/ auxhist4_interval_h
NAMELIST /time_control/ auxhist4_interval_m
NAMELIST /time_control/ auxhist4_interval_s
NAMELIST /time_control/ auxhist4_interval
NAMELIST /time_control/ auxhist4_begin_y
NAMELIST /time_control/ auxhist4_begin_d
NAMELIST /time_control/ auxhist4_begin_h
NAMELIST /time_control/ auxhist4_begin_m
NAMELIST /time_control/ auxhist4_begin_s
NAMELIST /time_control/ auxhist4_begin
NAMELIST /time_control/ auxhist4_end_y
NAMELIST /time_control/ auxhist4_end_d
NAMELIST /time_control/ auxhist4_end_h
NAMELIST /time_control/ auxhist4_end_m
NAMELIST /time_control/ auxhist4_end_s
NAMELIST /time_control/ auxhist4_end
NAMELIST /time_control/ io_form_auxhist4
NAMELIST /time_control/ frames_per_auxhist4
NAMELIST /time_control/ auxhist5_inname
NAMELIST /time_control/ auxhist5_outname
NAMELIST /time_control/ auxhist5_interval_y
NAMELIST /time_control/ auxhist5_interval_d
NAMELIST /time_control/ auxhist5_interval_h
NAMELIST /time_control/ auxhist5_interval_m
NAMELIST /time_control/ auxhist5_interval_s
NAMELIST /time_control/ auxhist5_interval
NAMELIST /time_control/ auxhist5_begin_y
NAMELIST /time_control/ auxhist5_begin_d
NAMELIST /time_control/ auxhist5_begin_h
NAMELIST /time_control/ auxhist5_begin_m
NAMELIST /time_control/ auxhist5_begin_s
NAMELIST /time_control/ auxhist5_begin
NAMELIST /time_control/ auxhist5_end_y
NAMELIST /time_control/ auxhist5_end_d
NAMELIST /time_control/ auxhist5_end_h
NAMELIST /time_control/ auxhist5_end_m
NAMELIST /time_control/ auxhist5_end_s
NAMELIST /time_control/ auxhist5_end
NAMELIST /time_control/ io_form_auxhist5
NAMELIST /time_control/ frames_per_auxhist5
NAMELIST /time_control/ auxhist6_inname
NAMELIST /time_control/ auxhist6_outname
NAMELIST /time_control/ auxhist6_interval_y
NAMELIST /time_control/ auxhist6_interval_d
NAMELIST /time_control/ auxhist6_interval_h
NAMELIST /time_control/ auxhist6_interval_m
NAMELIST /time_control/ auxhist6_interval_s
NAMELIST /time_control/ auxhist6_interval
NAMELIST /time_control/ auxhist6_begin_y
NAMELIST /time_control/ auxhist6_begin_d
NAMELIST /time_control/ auxhist6_begin_h
NAMELIST /time_control/ auxhist6_begin_m
NAMELIST /time_control/ auxhist6_begin_s
NAMELIST /time_control/ auxhist6_begin
NAMELIST /time_control/ auxhist6_end_y
NAMELIST /time_control/ auxhist6_end_d
NAMELIST /time_control/ auxhist6_end_h
NAMELIST /time_control/ auxhist6_end_m
NAMELIST /time_control/ auxhist6_end_s
NAMELIST /time_control/ auxhist6_end
NAMELIST /time_control/ io_form_auxhist6
NAMELIST /time_control/ frames_per_auxhist6
NAMELIST /time_control/ auxhist7_inname
NAMELIST /time_control/ auxhist7_outname
NAMELIST /time_control/ auxhist7_interval_y
NAMELIST /time_control/ auxhist7_interval_d
NAMELIST /time_control/ auxhist7_interval_h
NAMELIST /time_control/ auxhist7_interval_m
NAMELIST /time_control/ auxhist7_interval_s
NAMELIST /time_control/ auxhist7_interval
NAMELIST /time_control/ auxhist7_begin_y
NAMELIST /time_control/ auxhist7_begin_d
NAMELIST /time_control/ auxhist7_begin_h
NAMELIST /time_control/ auxhist7_begin_m
NAMELIST /time_control/ auxhist7_begin_s
NAMELIST /time_control/ auxhist7_begin
NAMELIST /time_control/ auxhist7_end_y
NAMELIST /time_control/ auxhist7_end_d
NAMELIST /time_control/ auxhist7_end_h
NAMELIST /time_control/ auxhist7_end_m
NAMELIST /time_control/ auxhist7_end_s
NAMELIST /time_control/ auxhist7_end
NAMELIST /time_control/ io_form_auxhist7
NAMELIST /time_control/ frames_per_auxhist7
NAMELIST /time_control/ auxhist8_inname
NAMELIST /time_control/ auxhist8_outname
NAMELIST /time_control/ auxhist8_interval_y
NAMELIST /time_control/ auxhist8_interval_d
NAMELIST /time_control/ auxhist8_interval_h
NAMELIST /time_control/ auxhist8_interval_m
NAMELIST /time_control/ auxhist8_interval_s
NAMELIST /time_control/ auxhist8_interval
NAMELIST /time_control/ auxhist8_begin_y
NAMELIST /time_control/ auxhist8_begin_d
NAMELIST /time_control/ auxhist8_begin_h
NAMELIST /time_control/ auxhist8_begin_m
NAMELIST /time_control/ auxhist8_begin_s
NAMELIST /time_control/ auxhist8_begin
NAMELIST /time_control/ auxhist8_end_y
NAMELIST /time_control/ auxhist8_end_d
NAMELIST /time_control/ auxhist8_end_h
NAMELIST /time_control/ auxhist8_end_m
NAMELIST /time_control/ auxhist8_end_s
NAMELIST /time_control/ auxhist8_end
NAMELIST /time_control/ io_form_auxhist8
NAMELIST /time_control/ frames_per_auxhist8
NAMELIST /time_control/ auxhist9_inname
NAMELIST /time_control/ auxhist9_outname
NAMELIST /time_control/ auxhist9_interval_y
NAMELIST /time_control/ auxhist9_interval_d
NAMELIST /time_control/ auxhist9_interval_h
NAMELIST /time_control/ auxhist9_interval_m
NAMELIST /time_control/ auxhist9_interval_s
NAMELIST /time_control/ auxhist9_interval
NAMELIST /time_control/ auxhist9_begin_y
NAMELIST /time_control/ auxhist9_begin_d
NAMELIST /time_control/ auxhist9_begin_h
NAMELIST /time_control/ auxhist9_begin_m
NAMELIST /time_control/ auxhist9_begin_s
NAMELIST /time_control/ auxhist9_begin
NAMELIST /time_control/ auxhist9_end_y
NAMELIST /time_control/ auxhist9_end_d
NAMELIST /time_control/ auxhist9_end_h
NAMELIST /time_control/ auxhist9_end_m
NAMELIST /time_control/ auxhist9_end_s
NAMELIST /time_control/ auxhist9_end
NAMELIST /time_control/ io_form_auxhist9
NAMELIST /time_control/ frames_per_auxhist9
NAMELIST /time_control/ auxhist10_inname
NAMELIST /time_control/ auxhist10_outname
NAMELIST /time_control/ auxhist10_interval_y
NAMELIST /time_control/ auxhist10_interval_d
NAMELIST /time_control/ auxhist10_interval_h
NAMELIST /time_control/ auxhist10_interval_m
NAMELIST /time_control/ auxhist10_interval_s
NAMELIST /time_control/ auxhist10_interval
NAMELIST /time_control/ auxhist10_begin_y
NAMELIST /time_control/ auxhist10_begin_d
NAMELIST /time_control/ auxhist10_begin_h
NAMELIST /time_control/ auxhist10_begin_m
NAMELIST /time_control/ auxhist10_begin_s
NAMELIST /time_control/ auxhist10_begin
NAMELIST /time_control/ auxhist10_end_y
NAMELIST /time_control/ auxhist10_end_d
NAMELIST /time_control/ auxhist10_end_h
NAMELIST /time_control/ auxhist10_end_m
NAMELIST /time_control/ auxhist10_end_s
NAMELIST /time_control/ auxhist10_end
NAMELIST /time_control/ io_form_auxhist10
NAMELIST /time_control/ frames_per_auxhist10
NAMELIST /time_control/ auxhist11_inname
NAMELIST /time_control/ auxhist11_outname
NAMELIST /time_control/ auxhist11_interval_y
NAMELIST /time_control/ auxhist11_interval_d
NAMELIST /time_control/ auxhist11_interval_h
NAMELIST /time_control/ auxhist11_interval_m
NAMELIST /time_control/ auxhist11_interval_s
NAMELIST /time_control/ auxhist11_interval
NAMELIST /time_control/ auxhist11_begin_y
NAMELIST /time_control/ auxhist11_begin_d
NAMELIST /time_control/ auxhist11_begin_h
NAMELIST /time_control/ auxhist11_begin_m
NAMELIST /time_control/ auxhist11_begin_s
NAMELIST /time_control/ auxhist11_begin
NAMELIST /time_control/ auxhist11_end_y
NAMELIST /time_control/ auxhist11_end_d
NAMELIST /time_control/ auxhist11_end_h
NAMELIST /time_control/ auxhist11_end_m
NAMELIST /time_control/ auxhist11_end_s
NAMELIST /time_control/ auxhist11_end
NAMELIST /time_control/ io_form_auxhist11
NAMELIST /time_control/ frames_per_auxhist11
NAMELIST /time_control/ auxhist12_inname
NAMELIST /time_control/ auxhist12_outname
NAMELIST /time_control/ auxhist12_interval_y
NAMELIST /time_control/ auxhist12_interval_d
NAMELIST /time_control/ auxhist12_interval_h
NAMELIST /time_control/ auxhist12_interval_m
NAMELIST /time_control/ auxhist12_interval_s
NAMELIST /time_control/ auxhist12_interval
NAMELIST /time_control/ auxhist12_begin_y
NAMELIST /time_control/ auxhist12_begin_d
NAMELIST /time_control/ auxhist12_begin_h
NAMELIST /time_control/ auxhist12_begin_m
NAMELIST /time_control/ auxhist12_begin_s
NAMELIST /time_control/ auxhist12_begin
NAMELIST /time_control/ auxhist12_end_y
NAMELIST /time_control/ auxhist12_end_d
NAMELIST /time_control/ auxhist12_end_h
NAMELIST /time_control/ auxhist12_end_m
NAMELIST /time_control/ auxhist12_end_s
NAMELIST /time_control/ auxhist12_end
NAMELIST /time_control/ io_form_auxhist12
NAMELIST /time_control/ frames_per_auxhist12
NAMELIST /time_control/ auxhist13_inname
NAMELIST /time_control/ auxhist13_outname
NAMELIST /time_control/ auxhist13_interval_y
NAMELIST /time_control/ auxhist13_interval_d
NAMELIST /time_control/ auxhist13_interval_h
NAMELIST /time_control/ auxhist13_interval_m
NAMELIST /time_control/ auxhist13_interval_s
NAMELIST /time_control/ auxhist13_interval
NAMELIST /time_control/ auxhist13_begin_y
NAMELIST /time_control/ auxhist13_begin_d
NAMELIST /time_control/ auxhist13_begin_h
NAMELIST /time_control/ auxhist13_begin_m
NAMELIST /time_control/ auxhist13_begin_s
NAMELIST /time_control/ auxhist13_begin
NAMELIST /time_control/ auxhist13_end_y
NAMELIST /time_control/ auxhist13_end_d
NAMELIST /time_control/ auxhist13_end_h
NAMELIST /time_control/ auxhist13_end_m
NAMELIST /time_control/ auxhist13_end_s
NAMELIST /time_control/ auxhist13_end
NAMELIST /time_control/ io_form_auxhist13
NAMELIST /time_control/ frames_per_auxhist13
NAMELIST /time_control/ auxhist14_inname
NAMELIST /time_control/ auxhist14_outname
NAMELIST /time_control/ auxhist14_interval_y
NAMELIST /time_control/ auxhist14_interval_d
NAMELIST /time_control/ auxhist14_interval_h
NAMELIST /time_control/ auxhist14_interval_m
NAMELIST /time_control/ auxhist14_interval_s
NAMELIST /time_control/ auxhist14_interval
NAMELIST /time_control/ auxhist14_begin_y
NAMELIST /time_control/ auxhist14_begin_d
NAMELIST /time_control/ auxhist14_begin_h
NAMELIST /time_control/ auxhist14_begin_m
NAMELIST /time_control/ auxhist14_begin_s
NAMELIST /time_control/ auxhist14_begin
NAMELIST /time_control/ auxhist14_end_y
NAMELIST /time_control/ auxhist14_end_d
NAMELIST /time_control/ auxhist14_end_h
NAMELIST /time_control/ auxhist14_end_m
NAMELIST /time_control/ auxhist14_end_s
NAMELIST /time_control/ auxhist14_end
NAMELIST /time_control/ io_form_auxhist14
NAMELIST /time_control/ frames_per_auxhist14
NAMELIST /time_control/ auxhist15_inname
NAMELIST /time_control/ auxhist15_outname
NAMELIST /time_control/ auxhist15_interval_y
NAMELIST /time_control/ auxhist15_interval_d
NAMELIST /time_control/ auxhist15_interval_h
NAMELIST /time_control/ auxhist15_interval_m
NAMELIST /time_control/ auxhist15_interval_s
NAMELIST /time_control/ auxhist15_interval
NAMELIST /time_control/ auxhist15_begin_y
NAMELIST /time_control/ auxhist15_begin_d
NAMELIST /time_control/ auxhist15_begin_h
NAMELIST /time_control/ auxhist15_begin_m
NAMELIST /time_control/ auxhist15_begin_s
NAMELIST /time_control/ auxhist15_begin
NAMELIST /time_control/ auxhist15_end_y
NAMELIST /time_control/ auxhist15_end_d
NAMELIST /time_control/ auxhist15_end_h
NAMELIST /time_control/ auxhist15_end_m
NAMELIST /time_control/ auxhist15_end_s
NAMELIST /time_control/ auxhist15_end
NAMELIST /time_control/ io_form_auxhist15
NAMELIST /time_control/ frames_per_auxhist15
NAMELIST /time_control/ auxhist16_inname
NAMELIST /time_control/ auxhist16_outname
NAMELIST /time_control/ auxhist16_interval_y
NAMELIST /time_control/ auxhist16_interval_d
NAMELIST /time_control/ auxhist16_interval_h
NAMELIST /time_control/ auxhist16_interval_m
NAMELIST /time_control/ auxhist16_interval_s
NAMELIST /time_control/ auxhist16_interval
NAMELIST /time_control/ auxhist16_begin_y
NAMELIST /time_control/ auxhist16_begin_d
NAMELIST /time_control/ auxhist16_begin_h
NAMELIST /time_control/ auxhist16_begin_m
NAMELIST /time_control/ auxhist16_begin_s
NAMELIST /time_control/ auxhist16_begin
NAMELIST /time_control/ auxhist16_end_y
NAMELIST /time_control/ auxhist16_end_d
NAMELIST /time_control/ auxhist16_end_h
NAMELIST /time_control/ auxhist16_end_m
NAMELIST /time_control/ auxhist16_end_s
NAMELIST /time_control/ auxhist16_end
NAMELIST /time_control/ io_form_auxhist16
NAMELIST /time_control/ frames_per_auxhist16
NAMELIST /time_control/ auxhist17_inname
NAMELIST /time_control/ auxhist17_outname
NAMELIST /time_control/ auxhist17_interval_y
NAMELIST /time_control/ auxhist17_interval_d
NAMELIST /time_control/ auxhist17_interval_h
NAMELIST /time_control/ auxhist17_interval_m
NAMELIST /time_control/ auxhist17_interval_s
NAMELIST /time_control/ auxhist17_interval
NAMELIST /time_control/ auxhist17_begin_y
NAMELIST /time_control/ auxhist17_begin_d
NAMELIST /time_control/ auxhist17_begin_h
NAMELIST /time_control/ auxhist17_begin_m
NAMELIST /time_control/ auxhist17_begin_s
NAMELIST /time_control/ auxhist17_begin
NAMELIST /time_control/ auxhist17_end_y
NAMELIST /time_control/ auxhist17_end_d
NAMELIST /time_control/ auxhist17_end_h
NAMELIST /time_control/ auxhist17_end_m
NAMELIST /time_control/ auxhist17_end_s
NAMELIST /time_control/ auxhist17_end
NAMELIST /time_control/ io_form_auxhist17
NAMELIST /time_control/ frames_per_auxhist17
NAMELIST /time_control/ auxhist18_inname
NAMELIST /time_control/ auxhist18_outname
NAMELIST /time_control/ auxhist18_interval_y
NAMELIST /time_control/ auxhist18_interval_d
NAMELIST /time_control/ auxhist18_interval_h
NAMELIST /time_control/ auxhist18_interval_m
NAMELIST /time_control/ auxhist18_interval_s
NAMELIST /time_control/ auxhist18_interval
NAMELIST /time_control/ auxhist18_begin_y
NAMELIST /time_control/ auxhist18_begin_d
NAMELIST /time_control/ auxhist18_begin_h
NAMELIST /time_control/ auxhist18_begin_m
NAMELIST /time_control/ auxhist18_begin_s
NAMELIST /time_control/ auxhist18_begin
NAMELIST /time_control/ auxhist18_end_y
NAMELIST /time_control/ auxhist18_end_d
NAMELIST /time_control/ auxhist18_end_h
NAMELIST /time_control/ auxhist18_end_m
NAMELIST /time_control/ auxhist18_end_s
NAMELIST /time_control/ auxhist18_end
NAMELIST /time_control/ io_form_auxhist18
NAMELIST /time_control/ frames_per_auxhist18
NAMELIST /time_control/ auxhist19_inname
NAMELIST /time_control/ auxhist19_outname
NAMELIST /time_control/ auxhist19_interval_y
NAMELIST /time_control/ auxhist19_interval_d
NAMELIST /time_control/ auxhist19_interval_h
NAMELIST /time_control/ auxhist19_interval_m
NAMELIST /time_control/ auxhist19_interval_s
NAMELIST /time_control/ auxhist19_interval
NAMELIST /time_control/ auxhist19_begin_y
NAMELIST /time_control/ auxhist19_begin_d
NAMELIST /time_control/ auxhist19_begin_h
NAMELIST /time_control/ auxhist19_begin_m
NAMELIST /time_control/ auxhist19_begin_s
NAMELIST /time_control/ auxhist19_begin
NAMELIST /time_control/ auxhist19_end_y
NAMELIST /time_control/ auxhist19_end_d
NAMELIST /time_control/ auxhist19_end_h
NAMELIST /time_control/ auxhist19_end_m
NAMELIST /time_control/ auxhist19_end_s
NAMELIST /time_control/ auxhist19_end
NAMELIST /time_control/ io_form_auxhist19
NAMELIST /time_control/ frames_per_auxhist19
NAMELIST /time_control/ auxhist20_inname
NAMELIST /time_control/ auxhist20_outname
NAMELIST /time_control/ auxhist20_interval_y
NAMELIST /time_control/ auxhist20_interval_d
NAMELIST /time_control/ auxhist20_interval_h
NAMELIST /time_control/ auxhist20_interval_m
NAMELIST /time_control/ auxhist20_interval_s
NAMELIST /time_control/ auxhist20_interval
NAMELIST /time_control/ auxhist20_begin_y
NAMELIST /time_control/ auxhist20_begin_d
NAMELIST /time_control/ auxhist20_begin_h
NAMELIST /time_control/ auxhist20_begin_m
NAMELIST /time_control/ auxhist20_begin_s
NAMELIST /time_control/ auxhist20_begin
NAMELIST /time_control/ auxhist20_end_y
NAMELIST /time_control/ auxhist20_end_d
NAMELIST /time_control/ auxhist20_end_h
NAMELIST /time_control/ auxhist20_end_m
NAMELIST /time_control/ auxhist20_end_s
NAMELIST /time_control/ auxhist20_end
NAMELIST /time_control/ io_form_auxhist20
NAMELIST /time_control/ frames_per_auxhist20
NAMELIST /time_control/ auxhist21_inname
NAMELIST /time_control/ auxhist21_outname
NAMELIST /time_control/ auxhist21_interval_y
NAMELIST /time_control/ auxhist21_interval_d
NAMELIST /time_control/ auxhist21_interval_h
NAMELIST /time_control/ auxhist21_interval_m
NAMELIST /time_control/ auxhist21_interval_s
NAMELIST /time_control/ auxhist21_interval
NAMELIST /time_control/ auxhist21_begin_y
NAMELIST /time_control/ auxhist21_begin_d
NAMELIST /time_control/ auxhist21_begin_h
NAMELIST /time_control/ auxhist21_begin_m
NAMELIST /time_control/ auxhist21_begin_s
NAMELIST /time_control/ auxhist21_begin
NAMELIST /time_control/ auxhist21_end_y
NAMELIST /time_control/ auxhist21_end_d
NAMELIST /time_control/ auxhist21_end_h
NAMELIST /time_control/ auxhist21_end_m
NAMELIST /time_control/ auxhist21_end_s
NAMELIST /time_control/ auxhist21_end
NAMELIST /time_control/ io_form_auxhist21
NAMELIST /time_control/ frames_per_auxhist21
NAMELIST /time_control/ auxhist22_inname
NAMELIST /time_control/ auxhist22_outname
NAMELIST /time_control/ auxhist22_interval_y
NAMELIST /time_control/ auxhist22_interval_d
NAMELIST /time_control/ auxhist22_interval_h
NAMELIST /time_control/ auxhist22_interval_m
NAMELIST /time_control/ auxhist22_interval_s
NAMELIST /time_control/ auxhist22_interval
NAMELIST /time_control/ auxhist22_begin_y
NAMELIST /time_control/ auxhist22_begin_d
NAMELIST /time_control/ auxhist22_begin_h
NAMELIST /time_control/ auxhist22_begin_m
NAMELIST /time_control/ auxhist22_begin_s
NAMELIST /time_control/ auxhist22_begin
NAMELIST /time_control/ auxhist22_end_y
NAMELIST /time_control/ auxhist22_end_d
NAMELIST /time_control/ auxhist22_end_h
NAMELIST /time_control/ auxhist22_end_m
NAMELIST /time_control/ auxhist22_end_s
NAMELIST /time_control/ auxhist22_end
NAMELIST /time_control/ io_form_auxhist22
NAMELIST /time_control/ frames_per_auxhist22
NAMELIST /time_control/ auxhist23_inname
NAMELIST /time_control/ auxhist23_outname
NAMELIST /time_control/ auxhist23_interval_y
NAMELIST /time_control/ auxhist23_interval_d
NAMELIST /time_control/ auxhist23_interval_h
NAMELIST /time_control/ auxhist23_interval_m
NAMELIST /time_control/ auxhist23_interval_s
NAMELIST /time_control/ auxhist23_interval
NAMELIST /time_control/ auxhist23_begin_y
NAMELIST /time_control/ auxhist23_begin_d
NAMELIST /time_control/ auxhist23_begin_h
NAMELIST /time_control/ auxhist23_begin_m
NAMELIST /time_control/ auxhist23_begin_s
NAMELIST /time_control/ auxhist23_begin
NAMELIST /time_control/ auxhist23_end_y
NAMELIST /time_control/ auxhist23_end_d
NAMELIST /time_control/ auxhist23_end_h
NAMELIST /time_control/ auxhist23_end_m
NAMELIST /time_control/ auxhist23_end_s
NAMELIST /time_control/ auxhist23_end
NAMELIST /time_control/ io_form_auxhist23
NAMELIST /time_control/ frames_per_auxhist23
NAMELIST /time_control/ auxhist24_inname
NAMELIST /time_control/ auxhist24_outname
NAMELIST /time_control/ auxhist24_interval_y
NAMELIST /time_control/ auxhist24_interval_d
NAMELIST /time_control/ auxhist24_interval_h
NAMELIST /time_control/ auxhist24_interval_m
NAMELIST /time_control/ auxhist24_interval_s
NAMELIST /time_control/ auxhist24_interval
NAMELIST /time_control/ auxhist24_begin_y
NAMELIST /time_control/ auxhist24_begin_d
NAMELIST /time_control/ auxhist24_begin_h
NAMELIST /time_control/ auxhist24_begin_m
NAMELIST /time_control/ auxhist24_begin_s
NAMELIST /time_control/ auxhist24_begin
NAMELIST /time_control/ auxhist24_end_y
NAMELIST /time_control/ auxhist24_end_d
NAMELIST /time_control/ auxhist24_end_h
NAMELIST /time_control/ auxhist24_end_m
NAMELIST /time_control/ auxhist24_end_s
NAMELIST /time_control/ auxhist24_end
NAMELIST /time_control/ io_form_auxhist24
NAMELIST /time_control/ frames_per_auxhist24
NAMELIST /time_control/ auxinput1_outname
NAMELIST /time_control/ auxinput1_interval_y
NAMELIST /time_control/ auxinput1_interval_d
NAMELIST /time_control/ auxinput1_interval_h
NAMELIST /time_control/ auxinput1_interval_m
NAMELIST /time_control/ auxinput1_interval_s
NAMELIST /time_control/ auxinput1_interval
NAMELIST /time_control/ auxinput1_begin_y
NAMELIST /time_control/ auxinput1_begin_d
NAMELIST /time_control/ auxinput1_begin_h
NAMELIST /time_control/ auxinput1_begin_m
NAMELIST /time_control/ auxinput1_begin_s
NAMELIST /time_control/ auxinput1_begin
NAMELIST /time_control/ auxinput1_end_y
NAMELIST /time_control/ auxinput1_end_d
NAMELIST /time_control/ auxinput1_end_h
NAMELIST /time_control/ auxinput1_end_m
NAMELIST /time_control/ auxinput1_end_s
NAMELIST /time_control/ auxinput1_end
NAMELIST /time_control/ frames_per_auxinput1
NAMELIST /time_control/ auxinput2_inname
NAMELIST /time_control/ auxinput2_outname
NAMELIST /time_control/ auxinput2_interval_y
NAMELIST /time_control/ auxinput2_interval_d
NAMELIST /time_control/ auxinput2_interval_h
NAMELIST /time_control/ auxinput2_interval_m
NAMELIST /time_control/ auxinput2_interval_s
NAMELIST /time_control/ auxinput2_interval
NAMELIST /time_control/ auxinput2_begin_y
NAMELIST /time_control/ auxinput2_begin_d
NAMELIST /time_control/ auxinput2_begin_h
NAMELIST /time_control/ auxinput2_begin_m
NAMELIST /time_control/ auxinput2_begin_s
NAMELIST /time_control/ auxinput2_begin
NAMELIST /time_control/ auxinput2_end_y
NAMELIST /time_control/ auxinput2_end_d
NAMELIST /time_control/ auxinput2_end_h
NAMELIST /time_control/ auxinput2_end_m
NAMELIST /time_control/ auxinput2_end_s
NAMELIST /time_control/ auxinput2_end
NAMELIST /time_control/ io_form_auxinput2
NAMELIST /time_control/ frames_per_auxinput2
NAMELIST /time_control/ auxinput3_inname
NAMELIST /time_control/ auxinput3_outname
NAMELIST /time_control/ auxinput3_interval_y
NAMELIST /time_control/ auxinput3_interval_d
NAMELIST /time_control/ auxinput3_interval_h
NAMELIST /time_control/ auxinput3_interval_m
NAMELIST /time_control/ auxinput3_interval_s
NAMELIST /time_control/ auxinput3_interval
NAMELIST /time_control/ auxinput3_begin_y
NAMELIST /time_control/ auxinput3_begin_d
NAMELIST /time_control/ auxinput3_begin_h
NAMELIST /time_control/ auxinput3_begin_m
NAMELIST /time_control/ auxinput3_begin_s
NAMELIST /time_control/ auxinput3_begin
NAMELIST /time_control/ auxinput3_end_y
NAMELIST /time_control/ auxinput3_end_d
NAMELIST /time_control/ auxinput3_end_h
NAMELIST /time_control/ auxinput3_end_m
NAMELIST /time_control/ auxinput3_end_s
NAMELIST /time_control/ auxinput3_end
NAMELIST /time_control/ io_form_auxinput3
NAMELIST /time_control/ frames_per_auxinput3
NAMELIST /time_control/ auxinput4_inname
NAMELIST /time_control/ auxinput4_outname
NAMELIST /time_control/ auxinput4_interval_y
NAMELIST /time_control/ auxinput4_interval_d
NAMELIST /time_control/ auxinput4_interval_h
NAMELIST /time_control/ auxinput4_interval_m
NAMELIST /time_control/ auxinput4_interval_s
NAMELIST /time_control/ auxinput4_interval
NAMELIST /time_control/ auxinput4_begin_y
NAMELIST /time_control/ auxinput4_begin_d
NAMELIST /time_control/ auxinput4_begin_h
NAMELIST /time_control/ auxinput4_begin_m
NAMELIST /time_control/ auxinput4_begin_s
NAMELIST /time_control/ auxinput4_begin
NAMELIST /time_control/ auxinput4_end_y
NAMELIST /time_control/ auxinput4_end_d
NAMELIST /time_control/ auxinput4_end_h
NAMELIST /time_control/ auxinput4_end_m
NAMELIST /time_control/ auxinput4_end_s
NAMELIST /time_control/ auxinput4_end
NAMELIST /time_control/ io_form_auxinput4
NAMELIST /time_control/ frames_per_auxinput4
NAMELIST /time_control/ auxinput5_inname
NAMELIST /time_control/ auxinput5_outname
NAMELIST /time_control/ auxinput5_interval_y
NAMELIST /time_control/ auxinput5_interval_d
NAMELIST /time_control/ auxinput5_interval_h
NAMELIST /time_control/ auxinput5_interval_m
NAMELIST /time_control/ auxinput5_interval_s
NAMELIST /time_control/ auxinput5_interval
NAMELIST /time_control/ auxinput5_begin_y
NAMELIST /time_control/ auxinput5_begin_d
NAMELIST /time_control/ auxinput5_begin_h
NAMELIST /time_control/ auxinput5_begin_m
NAMELIST /time_control/ auxinput5_begin_s
NAMELIST /time_control/ auxinput5_begin
NAMELIST /time_control/ auxinput5_end_y
NAMELIST /time_control/ auxinput5_end_d
NAMELIST /time_control/ auxinput5_end_h
NAMELIST /time_control/ auxinput5_end_m
NAMELIST /time_control/ auxinput5_end_s
NAMELIST /time_control/ auxinput5_end
NAMELIST /time_control/ io_form_auxinput5
NAMELIST /time_control/ frames_per_auxinput5
NAMELIST /time_control/ auxinput6_inname
NAMELIST /time_control/ auxinput6_outname
NAMELIST /time_control/ auxinput6_interval_y
NAMELIST /time_control/ auxinput6_interval_d
NAMELIST /time_control/ auxinput6_interval_h
NAMELIST /time_control/ auxinput6_interval_m
NAMELIST /time_control/ auxinput6_interval_s
NAMELIST /time_control/ auxinput6_interval
NAMELIST /time_control/ auxinput6_begin_y
NAMELIST /time_control/ auxinput6_begin_d
NAMELIST /time_control/ auxinput6_begin_h
NAMELIST /time_control/ auxinput6_begin_m
NAMELIST /time_control/ auxinput6_begin_s
NAMELIST /time_control/ auxinput6_begin
NAMELIST /time_control/ auxinput6_end_y
NAMELIST /time_control/ auxinput6_end_d
NAMELIST /time_control/ auxinput6_end_h
NAMELIST /time_control/ auxinput6_end_m
NAMELIST /time_control/ auxinput6_end_s
NAMELIST /time_control/ auxinput6_end
NAMELIST /time_control/ io_form_auxinput6
NAMELIST /time_control/ frames_per_auxinput6
NAMELIST /time_control/ auxinput7_inname
NAMELIST /time_control/ auxinput7_outname
NAMELIST /time_control/ auxinput7_interval_y
NAMELIST /time_control/ auxinput7_interval_d
NAMELIST /time_control/ auxinput7_interval_h
NAMELIST /time_control/ auxinput7_interval_m
NAMELIST /time_control/ auxinput7_interval_s
NAMELIST /time_control/ auxinput7_interval
NAMELIST /time_control/ auxinput7_begin_y
NAMELIST /time_control/ auxinput7_begin_d
NAMELIST /time_control/ auxinput7_begin_h
NAMELIST /time_control/ auxinput7_begin_m
NAMELIST /time_control/ auxinput7_begin_s
NAMELIST /time_control/ auxinput7_begin
NAMELIST /time_control/ auxinput7_end_y
NAMELIST /time_control/ auxinput7_end_d
NAMELIST /time_control/ auxinput7_end_h
NAMELIST /time_control/ auxinput7_end_m
NAMELIST /time_control/ auxinput7_end_s
NAMELIST /time_control/ auxinput7_end
NAMELIST /time_control/ io_form_auxinput7
NAMELIST /time_control/ frames_per_auxinput7
NAMELIST /time_control/ auxinput8_inname
NAMELIST /time_control/ auxinput8_outname
NAMELIST /time_control/ auxinput8_interval_y
NAMELIST /time_control/ auxinput8_interval_d
NAMELIST /time_control/ auxinput8_interval_h
NAMELIST /time_control/ auxinput8_interval_m
NAMELIST /time_control/ auxinput8_interval_s
NAMELIST /time_control/ auxinput8_interval
NAMELIST /time_control/ auxinput8_begin_y
NAMELIST /time_control/ auxinput8_begin_d
NAMELIST /time_control/ auxinput8_begin_h
NAMELIST /time_control/ auxinput8_begin_m
NAMELIST /time_control/ auxinput8_begin_s
NAMELIST /time_control/ auxinput8_begin
NAMELIST /time_control/ auxinput8_end_y
NAMELIST /time_control/ auxinput8_end_d
NAMELIST /time_control/ auxinput8_end_h
NAMELIST /time_control/ auxinput8_end_m
NAMELIST /time_control/ auxinput8_end_s
NAMELIST /time_control/ auxinput8_end
NAMELIST /time_control/ io_form_auxinput8
NAMELIST /time_control/ frames_per_auxinput8
NAMELIST /time_control/ auxinput9_inname
NAMELIST /time_control/ auxinput9_outname
NAMELIST /time_control/ auxinput9_interval_y
NAMELIST /time_control/ auxinput9_interval_d
NAMELIST /time_control/ auxinput9_interval_h
NAMELIST /time_control/ auxinput9_interval_m
NAMELIST /time_control/ auxinput9_interval_s
NAMELIST /time_control/ auxinput9_interval
NAMELIST /time_control/ auxinput9_begin_y
NAMELIST /time_control/ auxinput9_begin_d
NAMELIST /time_control/ auxinput9_begin_h
NAMELIST /time_control/ auxinput9_begin_m
NAMELIST /time_control/ auxinput9_begin_s
NAMELIST /time_control/ auxinput9_begin
NAMELIST /time_control/ auxinput9_end_y
NAMELIST /time_control/ auxinput9_end_d
NAMELIST /time_control/ auxinput9_end_h
NAMELIST /time_control/ auxinput9_end_m
NAMELIST /time_control/ auxinput9_end_s
NAMELIST /time_control/ auxinput9_end
NAMELIST /time_control/ io_form_auxinput9
NAMELIST /time_control/ frames_per_auxinput9
NAMELIST /time_control/ auxinput10_inname
NAMELIST /time_control/ auxinput10_outname
NAMELIST /time_control/ auxinput10_interval_y
NAMELIST /time_control/ auxinput10_interval_d
NAMELIST /time_control/ auxinput10_interval_h
NAMELIST /time_control/ auxinput10_interval_m
NAMELIST /time_control/ auxinput10_interval_s
NAMELIST /time_control/ auxinput10_interval
NAMELIST /time_control/ auxinput10_begin_y
NAMELIST /time_control/ auxinput10_begin_d
NAMELIST /time_control/ auxinput10_begin_h
NAMELIST /time_control/ auxinput10_begin_m
NAMELIST /time_control/ auxinput10_begin_s
NAMELIST /time_control/ auxinput10_begin
NAMELIST /time_control/ auxinput10_end_y
NAMELIST /time_control/ auxinput10_end_d
NAMELIST /time_control/ auxinput10_end_h
NAMELIST /time_control/ auxinput10_end_m
NAMELIST /time_control/ auxinput10_end_s
NAMELIST /time_control/ auxinput10_end
NAMELIST /time_control/ io_form_auxinput10
NAMELIST /time_control/ frames_per_auxinput10
NAMELIST /time_control/ auxinput11_inname
NAMELIST /time_control/ auxinput11_outname
NAMELIST /time_control/ auxinput11_interval_y
NAMELIST /time_control/ auxinput11_interval_d
NAMELIST /time_control/ auxinput11_interval_h
NAMELIST /time_control/ auxinput11_interval_m
NAMELIST /time_control/ auxinput11_interval_s
NAMELIST /time_control/ auxinput11_interval
NAMELIST /time_control/ auxinput11_begin_y
NAMELIST /time_control/ auxinput11_begin_d
NAMELIST /time_control/ auxinput11_begin_h
NAMELIST /time_control/ auxinput11_begin_m
NAMELIST /time_control/ auxinput11_begin_s
NAMELIST /time_control/ auxinput11_begin
NAMELIST /time_control/ auxinput11_end_y
NAMELIST /time_control/ auxinput11_end_d
NAMELIST /time_control/ auxinput11_end_h
NAMELIST /time_control/ auxinput11_end_m
NAMELIST /time_control/ auxinput11_end_s
NAMELIST /time_control/ auxinput11_end
NAMELIST /time_control/ io_form_auxinput11
NAMELIST /time_control/ frames_per_auxinput11
NAMELIST /time_control/ auxinput12_inname
NAMELIST /time_control/ auxinput12_outname
NAMELIST /time_control/ auxinput12_interval_y
NAMELIST /time_control/ auxinput12_interval_d
NAMELIST /time_control/ auxinput12_interval_h
NAMELIST /time_control/ auxinput12_interval_m
NAMELIST /time_control/ auxinput12_interval_s
NAMELIST /time_control/ auxinput12_interval
NAMELIST /time_control/ auxinput12_begin_y
NAMELIST /time_control/ auxinput12_begin_d
NAMELIST /time_control/ auxinput12_begin_h
NAMELIST /time_control/ auxinput12_begin_m
NAMELIST /time_control/ auxinput12_begin_s
NAMELIST /time_control/ auxinput12_begin
NAMELIST /time_control/ auxinput12_end_y
NAMELIST /time_control/ auxinput12_end_d
NAMELIST /time_control/ auxinput12_end_h
NAMELIST /time_control/ auxinput12_end_m
NAMELIST /time_control/ auxinput12_end_s
NAMELIST /time_control/ auxinput12_end
NAMELIST /time_control/ io_form_auxinput12
NAMELIST /time_control/ frames_per_auxinput12
NAMELIST /time_control/ auxinput13_inname
NAMELIST /time_control/ auxinput13_outname
NAMELIST /time_control/ auxinput13_interval_y
NAMELIST /time_control/ auxinput13_interval_d
NAMELIST /time_control/ auxinput13_interval_h
NAMELIST /time_control/ auxinput13_interval_m
NAMELIST /time_control/ auxinput13_interval_s
NAMELIST /time_control/ auxinput13_interval
NAMELIST /time_control/ auxinput13_begin_y
NAMELIST /time_control/ auxinput13_begin_d
NAMELIST /time_control/ auxinput13_begin_h
NAMELIST /time_control/ auxinput13_begin_m
NAMELIST /time_control/ auxinput13_begin_s
NAMELIST /time_control/ auxinput13_begin
NAMELIST /time_control/ auxinput13_end_y
NAMELIST /time_control/ auxinput13_end_d
NAMELIST /time_control/ auxinput13_end_h
NAMELIST /time_control/ auxinput13_end_m
NAMELIST /time_control/ auxinput13_end_s
NAMELIST /time_control/ auxinput13_end
NAMELIST /time_control/ io_form_auxinput13
NAMELIST /time_control/ frames_per_auxinput13
NAMELIST /time_control/ auxinput14_inname
NAMELIST /time_control/ auxinput14_outname
NAMELIST /time_control/ auxinput14_interval_y
NAMELIST /time_control/ auxinput14_interval_d
NAMELIST /time_control/ auxinput14_interval_h
NAMELIST /time_control/ auxinput14_interval_m
NAMELIST /time_control/ auxinput14_interval_s
NAMELIST /time_control/ auxinput14_interval
NAMELIST /time_control/ auxinput14_begin_y
NAMELIST /time_control/ auxinput14_begin_d
NAMELIST /time_control/ auxinput14_begin_h
NAMELIST /time_control/ auxinput14_begin_m
NAMELIST /time_control/ auxinput14_begin_s
NAMELIST /time_control/ auxinput14_begin
NAMELIST /time_control/ auxinput14_end_y
NAMELIST /time_control/ auxinput14_end_d
NAMELIST /time_control/ auxinput14_end_h
NAMELIST /time_control/ auxinput14_end_m
NAMELIST /time_control/ auxinput14_end_s
NAMELIST /time_control/ auxinput14_end
NAMELIST /time_control/ io_form_auxinput14
NAMELIST /time_control/ frames_per_auxinput14
NAMELIST /time_control/ auxinput15_inname
NAMELIST /time_control/ auxinput15_outname
NAMELIST /time_control/ auxinput15_interval_y
NAMELIST /time_control/ auxinput15_interval_d
NAMELIST /time_control/ auxinput15_interval_h
NAMELIST /time_control/ auxinput15_interval_m
NAMELIST /time_control/ auxinput15_interval_s
NAMELIST /time_control/ auxinput15_interval
NAMELIST /time_control/ auxinput15_begin_y
NAMELIST /time_control/ auxinput15_begin_d
NAMELIST /time_control/ auxinput15_begin_h
NAMELIST /time_control/ auxinput15_begin_m
NAMELIST /time_control/ auxinput15_begin_s
NAMELIST /time_control/ auxinput15_begin
NAMELIST /time_control/ auxinput15_end_y
NAMELIST /time_control/ auxinput15_end_d
NAMELIST /time_control/ auxinput15_end_h
NAMELIST /time_control/ auxinput15_end_m
NAMELIST /time_control/ auxinput15_end_s
NAMELIST /time_control/ auxinput15_end
NAMELIST /time_control/ io_form_auxinput15
NAMELIST /time_control/ frames_per_auxinput15
NAMELIST /time_control/ auxinput16_inname
NAMELIST /time_control/ auxinput16_outname
NAMELIST /time_control/ auxinput16_interval_y
NAMELIST /time_control/ auxinput16_interval_d
NAMELIST /time_control/ auxinput16_interval_h
NAMELIST /time_control/ auxinput16_interval_m
NAMELIST /time_control/ auxinput16_interval_s
NAMELIST /time_control/ auxinput16_interval
NAMELIST /time_control/ auxinput16_begin_y
NAMELIST /time_control/ auxinput16_begin_d
NAMELIST /time_control/ auxinput16_begin_h
NAMELIST /time_control/ auxinput16_begin_m
NAMELIST /time_control/ auxinput16_begin_s
NAMELIST /time_control/ auxinput16_begin
NAMELIST /time_control/ auxinput16_end_y
NAMELIST /time_control/ auxinput16_end_d
NAMELIST /time_control/ auxinput16_end_h
NAMELIST /time_control/ auxinput16_end_m
NAMELIST /time_control/ auxinput16_end_s
NAMELIST /time_control/ auxinput16_end
NAMELIST /time_control/ io_form_auxinput16
NAMELIST /time_control/ frames_per_auxinput16
NAMELIST /time_control/ auxinput17_inname
NAMELIST /time_control/ auxinput17_outname
NAMELIST /time_control/ auxinput17_interval_y
NAMELIST /time_control/ auxinput17_interval_d
NAMELIST /time_control/ auxinput17_interval_h
NAMELIST /time_control/ auxinput17_interval_m
NAMELIST /time_control/ auxinput17_interval_s
NAMELIST /time_control/ auxinput17_interval
NAMELIST /time_control/ auxinput17_begin_y
NAMELIST /time_control/ auxinput17_begin_d
NAMELIST /time_control/ auxinput17_begin_h
NAMELIST /time_control/ auxinput17_begin_m
NAMELIST /time_control/ auxinput17_begin_s
NAMELIST /time_control/ auxinput17_begin
NAMELIST /time_control/ auxinput17_end_y
NAMELIST /time_control/ auxinput17_end_d
NAMELIST /time_control/ auxinput17_end_h
NAMELIST /time_control/ auxinput17_end_m
NAMELIST /time_control/ auxinput17_end_s
NAMELIST /time_control/ auxinput17_end
NAMELIST /time_control/ io_form_auxinput17
NAMELIST /time_control/ frames_per_auxinput17
NAMELIST /time_control/ auxinput18_inname
NAMELIST /time_control/ auxinput18_outname
NAMELIST /time_control/ auxinput18_interval_y
NAMELIST /time_control/ auxinput18_interval_d
NAMELIST /time_control/ auxinput18_interval_h
NAMELIST /time_control/ auxinput18_interval_m
NAMELIST /time_control/ auxinput18_interval_s
NAMELIST /time_control/ auxinput18_interval
NAMELIST /time_control/ auxinput18_begin_y
NAMELIST /time_control/ auxinput18_begin_d
NAMELIST /time_control/ auxinput18_begin_h
NAMELIST /time_control/ auxinput18_begin_m
NAMELIST /time_control/ auxinput18_begin_s
NAMELIST /time_control/ auxinput18_begin
NAMELIST /time_control/ auxinput18_end_y
NAMELIST /time_control/ auxinput18_end_d
NAMELIST /time_control/ auxinput18_end_h
NAMELIST /time_control/ auxinput18_end_m
NAMELIST /time_control/ auxinput18_end_s
NAMELIST /time_control/ auxinput18_end
NAMELIST /time_control/ io_form_auxinput18
NAMELIST /time_control/ frames_per_auxinput18
NAMELIST /time_control/ auxinput19_inname
NAMELIST /time_control/ auxinput19_outname
NAMELIST /time_control/ auxinput19_interval_y
NAMELIST /time_control/ auxinput19_interval_d
NAMELIST /time_control/ auxinput19_interval_h
NAMELIST /time_control/ auxinput19_interval_m
NAMELIST /time_control/ auxinput19_interval_s
NAMELIST /time_control/ auxinput19_interval
NAMELIST /time_control/ auxinput19_begin_y
NAMELIST /time_control/ auxinput19_begin_d
NAMELIST /time_control/ auxinput19_begin_h
NAMELIST /time_control/ auxinput19_begin_m
NAMELIST /time_control/ auxinput19_begin_s
NAMELIST /time_control/ auxinput19_begin
NAMELIST /time_control/ auxinput19_end_y
NAMELIST /time_control/ auxinput19_end_d
NAMELIST /time_control/ auxinput19_end_h
NAMELIST /time_control/ auxinput19_end_m
NAMELIST /time_control/ auxinput19_end_s
NAMELIST /time_control/ auxinput19_end
NAMELIST /time_control/ io_form_auxinput19
NAMELIST /time_control/ frames_per_auxinput19
NAMELIST /time_control/ auxinput20_inname
NAMELIST /time_control/ auxinput20_outname
NAMELIST /time_control/ auxinput20_interval_y
NAMELIST /time_control/ auxinput20_interval_d
NAMELIST /time_control/ auxinput20_interval_h
NAMELIST /time_control/ auxinput20_interval_m
NAMELIST /time_control/ auxinput20_interval_s
NAMELIST /time_control/ auxinput20_interval
NAMELIST /time_control/ auxinput20_begin_y
NAMELIST /time_control/ auxinput20_begin_d
NAMELIST /time_control/ auxinput20_begin_h
NAMELIST /time_control/ auxinput20_begin_m
NAMELIST /time_control/ auxinput20_begin_s
NAMELIST /time_control/ auxinput20_begin
NAMELIST /time_control/ auxinput20_end_y
NAMELIST /time_control/ auxinput20_end_d
NAMELIST /time_control/ auxinput20_end_h
NAMELIST /time_control/ auxinput20_end_m
NAMELIST /time_control/ auxinput20_end_s
NAMELIST /time_control/ auxinput20_end
NAMELIST /time_control/ io_form_auxinput20
NAMELIST /time_control/ frames_per_auxinput20
NAMELIST /time_control/ auxinput21_inname
NAMELIST /time_control/ auxinput21_outname
NAMELIST /time_control/ auxinput21_interval_y
NAMELIST /time_control/ auxinput21_interval_d
NAMELIST /time_control/ auxinput21_interval_h
NAMELIST /time_control/ auxinput21_interval_m
NAMELIST /time_control/ auxinput21_interval_s
NAMELIST /time_control/ auxinput21_interval
NAMELIST /time_control/ auxinput21_begin_y
NAMELIST /time_control/ auxinput21_begin_d
NAMELIST /time_control/ auxinput21_begin_h
NAMELIST /time_control/ auxinput21_begin_m
NAMELIST /time_control/ auxinput21_begin_s
NAMELIST /time_control/ auxinput21_begin
NAMELIST /time_control/ auxinput21_end_y
NAMELIST /time_control/ auxinput21_end_d
NAMELIST /time_control/ auxinput21_end_h
NAMELIST /time_control/ auxinput21_end_m
NAMELIST /time_control/ auxinput21_end_s
NAMELIST /time_control/ auxinput21_end
NAMELIST /time_control/ io_form_auxinput21
NAMELIST /time_control/ frames_per_auxinput21
NAMELIST /time_control/ auxinput22_inname
NAMELIST /time_control/ auxinput22_outname
NAMELIST /time_control/ auxinput22_interval_y
NAMELIST /time_control/ auxinput22_interval_d
NAMELIST /time_control/ auxinput22_interval_h
NAMELIST /time_control/ auxinput22_interval_m
NAMELIST /time_control/ auxinput22_interval_s
NAMELIST /time_control/ auxinput22_interval
NAMELIST /time_control/ auxinput22_begin_y
NAMELIST /time_control/ auxinput22_begin_d
NAMELIST /time_control/ auxinput22_begin_h
NAMELIST /time_control/ auxinput22_begin_m
NAMELIST /time_control/ auxinput22_begin_s
NAMELIST /time_control/ auxinput22_begin
NAMELIST /time_control/ auxinput22_end_y
NAMELIST /time_control/ auxinput22_end_d
NAMELIST /time_control/ auxinput22_end_h
NAMELIST /time_control/ auxinput22_end_m
NAMELIST /time_control/ auxinput22_end_s
NAMELIST /time_control/ auxinput22_end
NAMELIST /time_control/ io_form_auxinput22
NAMELIST /time_control/ frames_per_auxinput22
NAMELIST /time_control/ auxinput23_inname
NAMELIST /time_control/ auxinput23_outname
NAMELIST /time_control/ auxinput23_interval_y
NAMELIST /time_control/ auxinput23_interval_d
NAMELIST /time_control/ auxinput23_interval_h
NAMELIST /time_control/ auxinput23_interval_m
NAMELIST /time_control/ auxinput23_interval_s
NAMELIST /time_control/ auxinput23_interval
NAMELIST /time_control/ auxinput23_begin_y
NAMELIST /time_control/ auxinput23_begin_d
NAMELIST /time_control/ auxinput23_begin_h
NAMELIST /time_control/ auxinput23_begin_m
NAMELIST /time_control/ auxinput23_begin_s
NAMELIST /time_control/ auxinput23_begin
NAMELIST /time_control/ auxinput23_end_y
NAMELIST /time_control/ auxinput23_end_d
NAMELIST /time_control/ auxinput23_end_h
NAMELIST /time_control/ auxinput23_end_m
NAMELIST /time_control/ auxinput23_end_s
NAMELIST /time_control/ auxinput23_end
NAMELIST /time_control/ io_form_auxinput23
NAMELIST /time_control/ frames_per_auxinput23
NAMELIST /time_control/ auxinput24_inname
NAMELIST /time_control/ auxinput24_outname
NAMELIST /time_control/ auxinput24_interval_y
NAMELIST /time_control/ auxinput24_interval_d
NAMELIST /time_control/ auxinput24_interval_h
NAMELIST /time_control/ auxinput24_interval_m
NAMELIST /time_control/ auxinput24_interval_s
NAMELIST /time_control/ auxinput24_interval
NAMELIST /time_control/ auxinput24_begin_y
NAMELIST /time_control/ auxinput24_begin_d
NAMELIST /time_control/ auxinput24_begin_h
NAMELIST /time_control/ auxinput24_begin_m
NAMELIST /time_control/ auxinput24_begin_s
NAMELIST /time_control/ auxinput24_begin
NAMELIST /time_control/ auxinput24_end_y
NAMELIST /time_control/ auxinput24_end_d
NAMELIST /time_control/ auxinput24_end_h
NAMELIST /time_control/ auxinput24_end_m
NAMELIST /time_control/ auxinput24_end_s
NAMELIST /time_control/ auxinput24_end
NAMELIST /time_control/ io_form_auxinput24
NAMELIST /time_control/ frames_per_auxinput24
NAMELIST /time_control/ history_interval
NAMELIST /time_control/ frames_per_outfile
NAMELIST /time_control/ restart
NAMELIST /time_control/ restart_interval
NAMELIST /time_control/ io_form_input
NAMELIST /time_control/ io_form_history
NAMELIST /time_control/ io_form_restart
NAMELIST /time_control/ io_form_boundary
NAMELIST /time_control/ debug_level
NAMELIST /time_control/ self_test_domain
NAMELIST /time_control/ history_outname
NAMELIST /time_control/ history_inname
NAMELIST /time_control/ use_netcdf_classic
NAMELIST /time_control/ history_interval_d
NAMELIST /time_control/ history_interval_h
NAMELIST /time_control/ history_interval_m
NAMELIST /time_control/ history_interval_s
NAMELIST /time_control/ inputout_interval_d
NAMELIST /time_control/ inputout_interval_h
NAMELIST /time_control/ inputout_interval_m
NAMELIST /time_control/ inputout_interval_s
NAMELIST /time_control/ inputout_interval
NAMELIST /time_control/ restart_interval_d
NAMELIST /time_control/ restart_interval_h
NAMELIST /time_control/ restart_interval_m
NAMELIST /time_control/ restart_interval_s
NAMELIST /time_control/ history_begin_y
NAMELIST /time_control/ history_begin_d
NAMELIST /time_control/ history_begin_h
NAMELIST /time_control/ history_begin_m
NAMELIST /time_control/ history_begin_s
NAMELIST /time_control/ history_begin
NAMELIST /time_control/ inputout_begin_y
NAMELIST /time_control/ inputout_begin_d
NAMELIST /time_control/ inputout_begin_h
NAMELIST /time_control/ inputout_begin_m
NAMELIST /time_control/ inputout_begin_s
NAMELIST /time_control/ restart_begin_y
NAMELIST /time_control/ restart_begin_d
NAMELIST /time_control/ restart_begin_h
NAMELIST /time_control/ restart_begin_m
NAMELIST /time_control/ restart_begin_s
NAMELIST /time_control/ restart_begin
NAMELIST /time_control/ history_end_y
NAMELIST /time_control/ history_end_d
NAMELIST /time_control/ history_end_h
NAMELIST /time_control/ history_end_m
NAMELIST /time_control/ history_end_s
NAMELIST /time_control/ history_end
NAMELIST /time_control/ inputout_end_y
NAMELIST /time_control/ inputout_end_d
NAMELIST /time_control/ inputout_end_h
NAMELIST /time_control/ inputout_end_m
NAMELIST /time_control/ inputout_end_s
NAMELIST /time_control/ reset_simulation_start
NAMELIST /domains/ sr_x
NAMELIST /domains/ sr_y
NAMELIST /fdda/ sgfdda_inname
NAMELIST /fdda/ gfdda_inname
NAMELIST /fdda/ sgfdda_interval_d
NAMELIST /fdda/ sgfdda_interval_h
NAMELIST /fdda/ sgfdda_interval_m
NAMELIST /fdda/ sgfdda_interval_s
NAMELIST /fdda/ sgfdda_interval_y
NAMELIST /fdda/ sgfdda_interval
NAMELIST /fdda/ gfdda_interval_d
NAMELIST /fdda/ gfdda_interval_h
NAMELIST /fdda/ gfdda_interval_m
NAMELIST /fdda/ gfdda_interval_s
NAMELIST /fdda/ gfdda_interval_y
NAMELIST /fdda/ gfdda_interval
NAMELIST /fdda/ sgfdda_begin_y
NAMELIST /fdda/ sgfdda_begin_d
NAMELIST /fdda/ sgfdda_begin_h
NAMELIST /fdda/ sgfdda_begin_m
NAMELIST /fdda/ sgfdda_begin_s
NAMELIST /fdda/ gfdda_begin_y
NAMELIST /fdda/ gfdda_begin_d
NAMELIST /fdda/ gfdda_begin_h
NAMELIST /fdda/ gfdda_begin_m
NAMELIST /fdda/ gfdda_begin_s
NAMELIST /fdda/ sgfdda_end_y
NAMELIST /fdda/ sgfdda_end_d
NAMELIST /fdda/ sgfdda_end_h
NAMELIST /fdda/ sgfdda_end_m
NAMELIST /fdda/ sgfdda_end_s
NAMELIST /fdda/ gfdda_end_y
NAMELIST /fdda/ gfdda_end_d
NAMELIST /fdda/ gfdda_end_h
NAMELIST /fdda/ gfdda_end_m
NAMELIST /fdda/ gfdda_end_s
NAMELIST /fdda/ io_form_sgfdda
NAMELIST /fdda/ io_form_gfdda
NAMELIST /time_control/ iofields_filename
NAMELIST /time_control/ ignore_iofields_warning
NAMELIST /time_control/ ncd_nofill
NAMELIST /namelist_adios2/ adios2_compression_enable
NAMELIST /namelist_adios2/ adios2_blosc_compressor
NAMELIST /namelist_adios2/ adios2_numaggregators
NAMELIST /fire/ nfmc
NAMELIST /fire/ fmoist_run
NAMELIST /fire/ fmoist_interp
NAMELIST /fire/ fmoist_only
NAMELIST /fire/ fmoist_freq
NAMELIST /fire/ fmoist_dt
NAMELIST /fire/ fmep_decay_tlag
NAMELIST /fire/ ifire
NAMELIST /fire/ fire_boundary_guard
NAMELIST /fire/ fire_num_ignitions
NAMELIST /fire/ fire_ignition_ros1
NAMELIST /fire/ fire_ignition_start_lon1
NAMELIST /fire/ fire_ignition_start_lat1
NAMELIST /fire/ fire_ignition_end_lon1
NAMELIST /fire/ fire_ignition_end_lat1
NAMELIST /fire/ fire_ignition_radius1
NAMELIST /fire/ fire_ignition_start_time1
NAMELIST /fire/ fire_ignition_end_time1
NAMELIST /fire/ fire_ignition_ros2
NAMELIST /fire/ fire_ignition_start_lon2
NAMELIST /fire/ fire_ignition_start_lat2
NAMELIST /fire/ fire_ignition_end_lon2
NAMELIST /fire/ fire_ignition_end_lat2
NAMELIST /fire/ fire_ignition_radius2
NAMELIST /fire/ fire_ignition_start_time2
NAMELIST /fire/ fire_ignition_end_time2
NAMELIST /fire/ fire_ignition_ros3
NAMELIST /fire/ fire_ignition_start_lon3
NAMELIST /fire/ fire_ignition_start_lat3
NAMELIST /fire/ fire_ignition_end_lon3
NAMELIST /fire/ fire_ignition_end_lat3
NAMELIST /fire/ fire_ignition_radius3
NAMELIST /fire/ fire_ignition_start_time3
NAMELIST /fire/ fire_ignition_end_time3
NAMELIST /fire/ fire_ignition_ros4
NAMELIST /fire/ fire_ignition_start_lon4
NAMELIST /fire/ fire_ignition_start_lat4
NAMELIST /fire/ fire_ignition_end_lon4
NAMELIST /fire/ fire_ignition_end_lat4
NAMELIST /fire/ fire_ignition_radius4
NAMELIST /fire/ fire_ignition_start_time4
NAMELIST /fire/ fire_ignition_end_time4
NAMELIST /fire/ fire_ignition_ros5
NAMELIST /fire/ fire_ignition_start_lon5
NAMELIST /fire/ fire_ignition_start_lat5
NAMELIST /fire/ fire_ignition_end_lon5
NAMELIST /fire/ fire_ignition_end_lat5
NAMELIST /fire/ fire_ignition_radius5
NAMELIST /fire/ fire_ignition_start_time5
NAMELIST /fire/ fire_ignition_end_time5
NAMELIST /fire/ fire_ignition_start_x1
NAMELIST /fire/ fire_ignition_start_y1
NAMELIST /fire/ fire_ignition_end_x1
NAMELIST /fire/ fire_ignition_end_y1
NAMELIST /fire/ fire_ignition_start_x2
NAMELIST /fire/ fire_ignition_start_y2
NAMELIST /fire/ fire_ignition_end_x2
NAMELIST /fire/ fire_ignition_end_y2
NAMELIST /fire/ fire_ignition_start_x3
NAMELIST /fire/ fire_ignition_start_y3
NAMELIST /fire/ fire_ignition_end_x3
NAMELIST /fire/ fire_ignition_end_y3
NAMELIST /fire/ fire_ignition_start_x4
NAMELIST /fire/ fire_ignition_start_y4
NAMELIST /fire/ fire_ignition_end_x4
NAMELIST /fire/ fire_ignition_end_y4
NAMELIST /fire/ fire_ignition_start_x5
NAMELIST /fire/ fire_ignition_start_y5
NAMELIST /fire/ fire_ignition_end_x5
NAMELIST /fire/ fire_ignition_end_y5
NAMELIST /fire/ fire_lat_init
NAMELIST /fire/ fire_lon_init
NAMELIST /fire/ fire_ign_time
NAMELIST /fire/ fire_shape
NAMELIST /fire/ fire_sprd_mdl
NAMELIST /fire/ fire_crwn_hgt
NAMELIST /fire/ fire_ext_grnd
NAMELIST /fire/ fire_ext_crwn
NAMELIST /fire/ fire_sfc_flx
NAMELIST /fire/ fire_heat_peak
NAMELIST /fire/ fire_tg_ub
NAMELIST /fire/ fire_smk_scheme
NAMELIST /fire/ fire_smk_peak
NAMELIST /fire/ fire_smk_ext
NAMELIST /fire/ fire_wind_height
NAMELIST /fire/ fire_fuel_read
NAMELIST /fire/ fire_fuel_cat
NAMELIST /fire/ fire_fmc_read
NAMELIST /fire/ fire_print_msg
NAMELIST /fire/ fire_print_file
NAMELIST /fire/ fire_fuel_left_method
NAMELIST /fire/ fire_fuel_left_irl
NAMELIST /fire/ fire_fuel_left_jrl
NAMELIST /fire/ fire_grows_only
NAMELIST /fire/ fire_upwinding
NAMELIST /fire/ fire_upwind_split
NAMELIST /fire/ fire_viscosity
NAMELIST /fire/ fire_lfn_ext_up
NAMELIST /fire/ fire_topo_from_atm
NAMELIST /fire/ fire_advection
NAMELIST /fire/ fire_test_steps
NAMELIST /fire/ fire_const_time
NAMELIST /fire/ fire_const_grnhfx
NAMELIST /fire/ fire_const_grnqfx
NAMELIST /fire/ fire_atm_feedback
NAMELIST /fire/ fire_mountain_type
NAMELIST /fire/ fire_mountain_height
NAMELIST /fire/ fire_mountain_start_x
NAMELIST /fire/ fire_mountain_start_y
NAMELIST /fire/ fire_mountain_end_x
NAMELIST /fire/ fire_mountain_end_y
NAMELIST /fire/ delt_perturbation
NAMELIST /fire/ xrad_perturbation
NAMELIST /fire/ yrad_perturbation
NAMELIST /fire/ zrad_perturbation
NAMELIST /fire/ hght_perturbation
NAMELIST /fire/ stretch_grd
NAMELIST /fire/ stretch_hyp
NAMELIST /fire/ z_grd_scale
NAMELIST /fire/ sfc_full_init
NAMELIST /fire/ sfc_lu_index
NAMELIST /fire/ sfc_tsk
NAMELIST /fire/ sfc_tmn
NAMELIST /fire/ fire_read_lu
NAMELIST /fire/ fire_read_tsk
NAMELIST /fire/ fire_read_tmn
NAMELIST /fire/ fire_read_atm_ht
NAMELIST /fire/ fire_read_fire_ht
NAMELIST /fire/ fire_read_atm_grad
NAMELIST /fire/ fire_read_fire_grad
NAMELIST /fire/ sfc_vegfra
NAMELIST /fire/ sfc_canwat
NAMELIST /fire/ sfc_ivgtyp
NAMELIST /fire/ sfc_isltyp
NAMELIST /fire/ fire_lsm_reinit
NAMELIST /fire/ fire_lsm_reinit_iter
NAMELIST /fire/ fire_upwinding_reinit
NAMELIST /fire/ fire_is_real_perim
NAMELIST /fire/ fire_lsm_band_ngp
NAMELIST /fire/ fire_lsm_zcoupling
NAMELIST /fire/ fire_lsm_zcoupling_ref
NAMELIST /fire/ fire_tracer_smoke
NAMELIST /fire/ fire_viscosity_bg
NAMELIST /fire/ fire_viscosity_band
NAMELIST /fire/ fire_viscosity_ngp
NAMELIST /fire/ fire_slope_factor
NAMELIST /fire/ fs_array_maxsize
NAMELIST /fire/ fs_firebrand_gen_lim
NAMELIST /fire/ fs_firebrand_gen_dt
NAMELIST /fire/ fs_firebrand_gen_levels
NAMELIST /fire/ fs_firebrand_gen_maxhgt
NAMELIST /fire/ fs_firebrand_gen_levrand
NAMELIST /fire/ fs_firebrand_gen_levrand_seed
NAMELIST /fire/ fs_firebrand_gen_mom3d_dt
NAMELIST /fire/ fs_firebrand_gen_prop_diam
NAMELIST /fire/ fs_firebrand_gen_prop_effd
NAMELIST /fire/ fs_firebrand_gen_prop_temp
NAMELIST /fire/ fs_firebrand_gen_prop_tvel
NAMELIST /fire/ fs_firebrand_dens
NAMELIST /fire/ fs_firebrand_dens_char
NAMELIST /fire/ fs_firebrand_max_life_dt
NAMELIST /fire/ fs_firebrand_land_hgt
NAMELIST /fire/ fuel_crosswalk
NAMELIST /fire/ trackember
NAMELIST /dynamics/ do_avgflx_em
NAMELIST /dynamics/ do_avgflx_cugd
NAMELIST /stoch/ multi_perturb
NAMELIST /stoch/ pert_farms
NAMELIST /stoch/ pert_farms_albedo
NAMELIST /stoch/ pert_farms_aod
NAMELIST /stoch/ pert_farms_angexp
NAMELIST /stoch/ pert_farms_aerasy
NAMELIST /stoch/ pert_farms_qv
NAMELIST /stoch/ pert_farms_qc
NAMELIST /stoch/ pert_farms_qs
NAMELIST /stoch/ pert_deng
NAMELIST /stoch/ pert_deng_qv
NAMELIST /stoch/ pert_deng_qc
NAMELIST /stoch/ pert_deng_t
NAMELIST /stoch/ pert_deng_w
NAMELIST /stoch/ pert_mynn
NAMELIST /stoch/ pert_mynn_qv
NAMELIST /stoch/ pert_mynn_qc
NAMELIST /stoch/ pert_mynn_t
NAMELIST /stoch/ pert_mynn_qke
NAMELIST /stoch/ pert_noah
NAMELIST /stoch/ pert_noah_qv
NAMELIST /stoch/ pert_noah_t
NAMELIST /stoch/ pert_noah_smois
NAMELIST /stoch/ pert_noah_tslb
NAMELIST /stoch/ pert_thom
NAMELIST /stoch/ pert_thom_qv
NAMELIST /stoch/ pert_thom_qc
NAMELIST /stoch/ pert_thom_qi
NAMELIST /stoch/ pert_thom_qs
NAMELIST /stoch/ pert_thom_ni
NAMELIST /stoch/ pert_cld3
NAMELIST /stoch/ pert_cld3_qv
NAMELIST /stoch/ pert_cld3_t
NAMELIST /stoch/ spdt
NAMELIST /stoch/ num_pert_3d
NAMELIST /stoch/ nens
NAMELIST /stoch/ skebs
NAMELIST /stoch/ stoch_force_opt
NAMELIST /stoch/ skebs_vertstruc
NAMELIST /stoch/ stoch_vertstruc_opt
NAMELIST /stoch/ tot_backscat_psi
NAMELIST /stoch/ tot_backscat_t
NAMELIST /stoch/ ztau_psi
NAMELIST /stoch/ ztau_t
NAMELIST /stoch/ rexponent_psi
NAMELIST /stoch/ rexponent_t
NAMELIST /stoch/ zsigma2_eps
NAMELIST /stoch/ zsigma2_eta
NAMELIST /stoch/ kminforc
NAMELIST /stoch/ lminforc
NAMELIST /stoch/ kminforct
NAMELIST /stoch/ lminforct
NAMELIST /stoch/ kmaxforc
NAMELIST /stoch/ lmaxforc
NAMELIST /stoch/ kmaxforct
NAMELIST /stoch/ lmaxforct
NAMELIST /stoch/ iseed_skebs
NAMELIST /stoch/ sppt
NAMELIST /stoch/ gridpt_stddev_sppt
NAMELIST /stoch/ stddev_cutoff_sppt
NAMELIST /stoch/ lengthscale_sppt
NAMELIST /stoch/ timescale_sppt
NAMELIST /stoch/ sppt_vertstruc
NAMELIST /stoch/ iseed_sppt
NAMELIST /stoch/ rand_perturb
NAMELIST /stoch/ gridpt_stddev_rand_pert
NAMELIST /stoch/ stddev_cutoff_rand_pert
NAMELIST /stoch/ lengthscale_rand_pert
NAMELIST /stoch/ timescale_rand_pert
NAMELIST /stoch/ rand_pert_vertstruc
NAMELIST /stoch/ iseed_rand_pert
NAMELIST /stoch/ spp
NAMELIST /stoch/ hrrr_cycling
NAMELIST /stoch/ spp_conv
NAMELIST /stoch/ gridpt_stddev_spp_conv
NAMELIST /stoch/ stddev_cutoff_spp_conv
NAMELIST /stoch/ lengthscale_spp_conv
NAMELIST /stoch/ timescale_spp_conv
NAMELIST /stoch/ vertstruc_spp_conv
NAMELIST /stoch/ iseed_spp_conv
NAMELIST /stoch/ spp_pbl
NAMELIST /stoch/ gridpt_stddev_spp_pbl
NAMELIST /stoch/ stddev_cutoff_spp_pbl
NAMELIST /stoch/ lengthscale_spp_pbl
NAMELIST /stoch/ timescale_spp_pbl
NAMELIST /stoch/ vertstruc_spp_pbl
NAMELIST /stoch/ iseed_spp_pbl
NAMELIST /stoch/ spp_lsm
NAMELIST /stoch/ gridpt_stddev_spp_lsm
NAMELIST /stoch/ stddev_cutoff_spp_lsm
NAMELIST /stoch/ lengthscale_spp_lsm
NAMELIST /stoch/ timescale_spp_lsm
NAMELIST /stoch/ vertstruc_spp_lsm
NAMELIST /stoch/ iseed_spp_lsm
NAMELIST /dynamics/ sfs_opt
NAMELIST /dynamics/ m_opt
NAMELIST /physics/ lakedepth_default
NAMELIST /physics/ lake_min_elev
NAMELIST /physics/ use_lakedepth
NAMELIST /physics/ sbm_diagnostics
NAMELIST /afwa/ afwa_diag_opt
NAMELIST /afwa/ afwa_ptype_opt
NAMELIST /afwa/ afwa_vil_opt
NAMELIST /afwa/ afwa_radar_opt
NAMELIST /afwa/ afwa_severe_opt
NAMELIST /afwa/ afwa_icing_opt
NAMELIST /afwa/ afwa_vis_opt
NAMELIST /afwa/ afwa_cloud_opt
NAMELIST /afwa/ afwa_therm_opt
NAMELIST /afwa/ afwa_turb_opt
NAMELIST /afwa/ afwa_buoy_opt
NAMELIST /afwa/ afwa_ptype_ccn_tmp
NAMELIST /afwa/ afwa_ptype_tot_melt
NAMELIST /afwa/ afwa_bad_data_check
NAMELIST /time_control/ mean_diag
NAMELIST /time_control/ mean_freq
NAMELIST /time_control/ mean_interval
NAMELIST /time_control/ mean_diag_interval
NAMELIST /time_control/ mean_diag_interval_s
NAMELIST /time_control/ mean_diag_interval_m
NAMELIST /time_control/ mean_diag_interval_h
NAMELIST /time_control/ mean_diag_interval_d
NAMELIST /time_control/ mean_diag_interval_mo
NAMELIST /time_control/ diurnal_diag
NAMELIST /physics/ nssl_ipelec
NAMELIST /physics/ nssl_isaund
NAMELIST /physics/ nssl_iscreen
NAMELIST /physics/ nssl_lightrad
NAMELIST /physics/ nssl_idischarge
NAMELIST /physics/ nssl_ibrkd
NAMELIST /physics/ nssl_ecrit
NAMELIST /physics/ nssl_disfrac
NAMELIST /physics/ elec_physics
NAMELIST /stoch/ perturb_bdy
NAMELIST /stoch/ perturb_chem_bdy
NAMELIST /dynamics/ hybrid_opt
NAMELIST /dynamics/ etac
NAMELIST /domains/ num_wif_levels
NAMELIST /domains/ wif_input_opt
NAMELIST /diags/ diag_nwp2
NAMELIST /diags/ solar_diagnostics
NAMELIST /diags/ p_lev_diags
NAMELIST /diags/ p_lev_diags_dfi
NAMELIST /diags/ num_press_levels
NAMELIST /diags/ press_levels
NAMELIST /diags/ use_tot_or_hyd_p
NAMELIST /diags/ extrap_below_grnd
NAMELIST /diags/ p_lev_missing
NAMELIST /diags/ z_lev_diags
NAMELIST /diags/ z_lev_diags_dfi
NAMELIST /diags/ num_z_levels
NAMELIST /diags/ z_levels
NAMELIST /diags/ z_lev_missing
NAMELIST /time_control/ iau
NAMELIST /time_control/ iau_time_window_sec
NAMELIST /wrf_cmaq/ wrf_cmaq_option
NAMELIST /wrf_cmaq/ wrf_cmaq_freq
NAMELIST /wrf_cmaq/ met_file_tstep
NAMELIST /wrf_cmaq/ direct_sw_feedback
NAMELIST /wrf_cmaq/ feedback_restart
NAMELIST /dynamics/ sdirk3_jvp_checkpointing
NAMELIST /dynamics/ sdirk3_jvp_checkpoint_segments
NAMELIST /dynamics/ sdirk3_jvp_graph_caching
NAMELIST /dynamics/ sdirk3_jvp_batched
NAMELIST /dynamics/ sdirk3_jvp_batch_size
NAMELIST /dynamics/ sdirk3_jvp_mixed_precision
NAMELIST /dynamics/ sdirk3_implicit_acoustic
NAMELIST /dynamics/ sdirk3_implicit_gravity
NAMELIST /dynamics/ sdirk3_implicit_rayleigh
NAMELIST /dynamics/ sdirk3_implicit_wdamp
NAMELIST /dynamics/ sdirk3_implicit_vdiff
NAMELIST /dynamics/ sdirk3_implicit_hdiff
NAMELIST /dynamics/ sdirk3_implicit_divergence
NAMELIST /dynamics/ sdirk3_precond_diagonal_only
NAMELIST /dynamics/ sdirk3_precond_block_jacobi
NAMELIST /dynamics/ sdirk3_precond_block_size
NAMELIST /dynamics/ sdirk3_precond_ilu
NAMELIST /dynamics/ sdirk3_precond_ilu_level
NAMELIST /dynamics/ sdirk3_precond_multigrid
NAMELIST /dynamics/ sdirk3_precond_mg_levels
NAMELIST /dynamics/ sdirk3_nk_adaptive_tol
NAMELIST /dynamics/ sdirk3_nk_forcing_eta_max
NAMELIST /dynamics/ sdirk3_nk_forcing_eta_min
NAMELIST /dynamics/ sdirk3_nk_forcing_gamma
NAMELIST /dynamics/ sdirk3_nk_forcing_alpha
NAMELIST /dynamics/ sdirk3_nk_line_search
NAMELIST /dynamics/ sdirk3_nk_trust_region
NAMELIST /dynamics/ sdirk3_nk_trust_radius
NAMELIST /dynamics/ sdirk3_stage2_gmres_restart
NAMELIST /dynamics/ sdirk3_stage2_max_krylov_restarts
NAMELIST /dynamics/ sdirk3_stage2_krylov_tol
NAMELIST /dynamics/ sdirk3_stage2_ew_eta_min
NAMELIST /dynamics/ sdirk3_stage2_ew_eta_max
NAMELIST /dynamics/ sdirk3_stage3_gmres_restart
NAMELIST /dynamics/ sdirk3_stage3_max_krylov_restarts
NAMELIST /dynamics/ sdirk3_stage3_krylov_tol
NAMELIST /dynamics/ sdirk3_stage3_ew_eta_min
NAMELIST /dynamics/ sdirk3_stage3_ew_eta_max
NAMELIST /dynamics/ sdirk3_imex_split_mode
NAMELIST /dynamics/ sdirk3_imex_slow_in_tangent
NAMELIST /dynamics/ sdirk3_imex_phys_in_tangent
NAMELIST /dynamics/ sdirk3_stage1_explicit
NAMELIST /dynamics/ sdirk3_stage3_warmstart
NAMELIST /dynamics/ sdirk3_stage_fail_action
NAMELIST /dynamics/ sdirk3_gate_metric_mode
NAMELIST /dynamics/ sdirk3_stage3_gate_rel_threshold
NAMELIST /dynamics/ sdirk3_hopeless_relax
NAMELIST /dynamics/ sdirk3_precond_ratio_guard_warn_only
NAMELIST /dynamics/ sdirk3_retain_graph_for_adjoint
NAMELIST /dynamics/ sdirk3_enable_ad_halo_exchange
NAMELIST /dynamics/ sdirk3_stage_boundary_sync
NAMELIST /dynamics/ sdirk3_jvp_auto_bench_calls
NAMELIST /dynamics/ sdirk3_jvp_auto_bench_quality_gate
NAMELIST /dynamics/ sdirk3_jvp_auto_bench_warmup
NAMELIST /dynamics/ sdirk3_jvp_auto_bench_seed
NAMELIST /dynamics/ sdirk3_jvp_auto_bench_lock_reset_stage1
NAMELIST /dynamics/ sdirk3_solver_telemetry
NAMELIST /dynamics/ sdirk3_progress_invariant_enable
NAMELIST /dynamics/ sdirk3_progress_invariant_min_ratio
NAMELIST /dynamics/ sdirk3_progress_invariant_streak_limit
NAMELIST /dynamics/ sdirk3_variable_pc_event_log
NAMELIST /dynamics/ sdirk3_gmres_warmstart
NAMELIST /dynamics/ sdirk3_gmres_warmstart_quality_gate
NAMELIST /dynamics/ sdirk3_inn_warmstart_enable
NAMELIST /dynamics/ sdirk3_inn_residual_gate_enable
NAMELIST /dynamics/ sdirk3_inn_q_min
NAMELIST /dynamics/ sdirk3_inn_gate_rel_tol
NAMELIST /dynamics/ sdirk3_inn_gate_q_noise
NAMELIST /dynamics/ sdirk3_inn_beta_max
NAMELIST /dynamics/ sdirk3_inn_ood_guard_enable
NAMELIST /dynamics/ sdirk3_inn_tol_ramp_enable
NAMELIST /dynamics/ sdirk3_inn_tol_ramp_gamma
NAMELIST /dynamics/ sdirk3_rhs_bc_parity
NAMELIST /dynamics/ sdirk3_mass_pgf_bc_guard
NAMELIST /dynamics/ sdirk3_precond_horizontal_smooth_alpha
NAMELIST /dynamics/ sdirk3_precond_horizontal_smooth_iters
NAMELIST /dynamics/ sdirk3_precond_vertical_smooth_alpha
NAMELIST /dynamics/ sdirk3_precond_smooth_boundary_guard
NAMELIST /dynamics/ sdirk3_trust_region_block_aware_thresh
NAMELIST /dynamics/ sdirk3_trust_fallback_relax
NAMELIST /dynamics/ sdirk3_trust_fallback_ratio
NAMELIST /dynamics/ sdirk3_direct_u_solve_thresh
NAMELIST /dynamics/ sdirk3_memory_pool
NAMELIST /dynamics/ sdirk3_memory_pool_size_mb
NAMELIST /dynamics/ sdirk3_tensor_checkpointing
NAMELIST /dynamics/ sdirk3_gradient_checkpointing
NAMELIST /dynamics/ sdirk3_kdamp
NAMELIST /dynamics/ sdirk3_khdif
NAMELIST /dynamics/ sdirk3_kvdif
NAMELIST /dynamics/ sdirk3_w_damp_alpha
NAMELIST /dynamics/ sdirk3_w_crit_cfl
NAMELIST /dynamics/ sdirk3_validation_level
NAMELIST /dynamics/ sdirk3_check_staggered
NAMELIST /dynamics/ sdirk3_collect_stats
NAMELIST /dynamics/ sdirk3_conserv_tol_mass
NAMELIST /dynamics/ sdirk3_conserv_tol_energy
NAMELIST /dynamics/ sdirk3_conserv_tol_momentum
NAMELIST /dynamics/ sdirk3_enable_benchmarking
NAMELIST /dynamics/ sdirk3_benchmark_warmup
NAMELIST /dynamics/ sdirk3_benchmark_measure
NAMELIST /dynamics/ sdirk3_save_trajectory
NAMELIST /dynamics/ sdirk3_checkpoint_interval
NAMELIST /dynamics/ sdirk3_obs_aware_4dvar
NAMELIST /dynamics/ sdirk3_obs_source_mode
NAMELIST /dynamics/ sdirk3_obs_window_sync_mode
NAMELIST /physics/ chem_opt



      CALL MPI_INITIALIZED( mpi_inited, ierr )
      IF ( .NOT. mpi_inited ) THEN

        IF ( coupler_on ) THEN
           CALL cpl_init( mpi_comm_here )
        ELSE

           CALL mpi_init ( ierr )
           mpi_comm_here = MPI_COMM_WORLD

        END IF

        CALL wrf_set_dm_communicator( mpi_comm_here )
        CALL wrf_termio_dup( mpi_comm_here )

      END IF

      CALL wrf_get_dm_communicator( mpi_comm_here )

      CALL MPI_Comm_rank ( mpi_comm_here, mytask_local, ierr ) ;
      CALL MPI_Comm_size ( mpi_comm_here, ntasks_local, ierr ) ;
      mpi_comm_allcompute = mpi_comm_here

      IF ( mytask_local .EQ. 0 ) THEN
        max_dom = 1
        OPEN ( unit=27, file="namelist.input", form="formatted", status="old" )
        READ ( UNIT = 27 , NML = domains , IOSTAT=io_status )
        REWIND(27)
        nio_groups = 1
        nio_tasks_per_group  = 0
        poll_servers = .false.
        READ ( 27 , NML = namelist_quilt, IOSTAT=io_status )
        CLOSE(27)
      END IF
      CALL mpi_bcast( nio_tasks_per_group  , max_domains , MPI_INTEGER , 0 , mpi_comm_here, ierr )
      CALL mpi_bcast( nio_groups , 1 , MPI_INTEGER , 0 , mpi_comm_here, ierr )
      CALL mpi_bcast( max_dom, 1 , MPI_INTEGER , 0 , mpi_comm_here, ierr )
      CALL mpi_bcast( parent_id, max_domains , MPI_INTEGER , 0 , mpi_comm_here, ierr )
      CALL quilting_disabled( quilting_is_turned_off )
      IF ( quilting_is_turned_off ) THEN
        num_io_tasks = 0
        nio_tasks_per_group  = 0
        nio_groups = 1
      ELSE
        num_io_tasks = nio_tasks_per_group(1)*nio_groups
      END IF
      CALL nl_set_max_dom(1,max_dom)  

      IF ( mytask_local .EQ. 0 ) THEN
        OPEN ( unit=27, file="namelist.input", form="formatted", status="old" )

        nproc_x = -1
        nproc_y = -1
        READ ( 27 , NML = domains, IOSTAT=io_status )
        CLOSE ( 27 )
        OPEN ( unit=27, file="namelist.input", form="formatted", status="old" )
        tasks_per_split = ntasks_local



        nest_pes_x = 0    
        nest_pes_y = 0
        IF ( nproc_x .EQ. -1 .OR. nproc_y .EQ. -1 ) THEN
          CALL compute_mesh( ntasks_local-num_io_tasks, n_x, n_y )
        ELSE
          n_x = nproc_x
          n_y = nproc_y
        END IF
        comm_start = 0   
        nest_pes_x(1:max_dom) = n_x
        nest_pes_y(1:max_dom) = n_y
        READ ( 27 , NML = dm_task_split, IOSTAT=io_status )
        CLOSE ( 27 )
      END IF
      CALL mpi_bcast( io_status, 1 , MPI_INTEGER , 0 , mpi_comm_here, ierr )
      IF ( io_status .NE. 0 ) THEN

      END IF
      CALL mpi_bcast( tasks_per_split, 1 , MPI_INTEGER , 0 , mpi_comm_here, ierr )
      CALL mpi_bcast( nproc_x, 1 , MPI_INTEGER , 0 , mpi_comm_here, ierr )
      CALL mpi_bcast( nproc_y, 1 , MPI_INTEGER , 0 , mpi_comm_here, ierr )
      CALL mpi_bcast( comm_start, max_domains , MPI_INTEGER , 0 , mpi_comm_here, ierr )
      CALL mpi_bcast( nest_pes_x, max_domains , MPI_INTEGER , 0 , mpi_comm_here, ierr )
      CALL mpi_bcast( nest_pes_y, max_domains , MPI_INTEGER , 0 , mpi_comm_here, ierr )

      nkids = 1
      which_kid = 0
      DO i = 2, max_dom
        IF ( 1 .le. parent_id(i) .AND. parent_id(i) .LE. max_domains ) THEN
          which_kid(i) = nkids(parent_id(i))
          nkids(parent_id(i)) = nkids(parent_id(i)) + 1
        ELSE
          WRITE(wrf_err_message,*)'invalid parent id for domain ',i
          CALL wrf_error_fatal3("<stdin>",6258,&
TRIM(wrf_err_message))
        END IF
      END DO

      num_compute_tasks = -99
      DO nest_id = 1,max_dom
        IF ( nest_id .EQ. 1 ) THEN
          nest_task_offsets(nest_id) = comm_start(nest_id)
        ELSE
          IF ( comm_start(nest_id) .LT. comm_start(parent_id(nest_id)) ) THEN
            WRITE(wrf_err_message,&
        "('nest domain ',i3,'comm_start (',i3,') lt parent ',i3,' comm_start (',i3,')')") &
                   nest_id,comm_start,parent_id(nest_id),comm_start(parent_id(nest_id))
            CALL wrf_error_fatal3("<stdin>",6272,&
TRIM(wrf_err_message))
          ELSE IF ( comm_start(nest_id) .LT. &
                    comm_start(parent_id(nest_id)) &
                   +nest_pes_x(parent_id(nest_id))*nest_pes_y(parent_id(nest_id))) THEN
            nest_task_offsets(nest_id) = comm_start(nest_id)-comm_start(parent_id(nest_id))
          ELSE
            nest_task_offsets(nest_id) = nest_pes_x(parent_id(nest_id))*nest_pes_y(parent_id(nest_id))
          END IF
        END IF
        IF ((comm_start(nest_id)+nest_pes_x(nest_id)*nest_pes_y(nest_id)) .GT. num_compute_tasks ) THEN
          num_compute_tasks = (comm_start(nest_id)+nest_pes_x(nest_id)*nest_pes_y(nest_id))
        END IF
      END DO

      IF ( .TRUE. ) THEN










        CALL MPI_Comm_rank ( mpi_comm_here, mytask_local, ierr ) ;
        CALL MPI_Comm_rank ( mpi_comm_here, origmytask, ierr ) ;
        CALL mpi_comm_size ( mpi_comm_here, ntasks_local, ierr ) ;
        ALLOCATE( icolor(ntasks_local) )
        ALLOCATE( icolor2(ntasks_local) )
        ALLOCATE( idomain(ntasks_local) )
        k = 0



        comms_i_am_in = MPI_UNDEFINED
        DO i = 1, max_dom
          inthisone = .FALSE.
          icolor = 0
          DO j = comm_start(i), comm_start(i)+nest_pes_x(i)*nest_pes_y(i)-1
            IF ( j+1 .GT. ntasks_local ) THEN
              WRITE(wrf_err_message,*)"check comm_start, nest_pes_x, nest_pes_y settings in namelist for comm ",i
              CALL wrf_error_fatal3("<stdin>",6315,&
wrf_err_message)
            END IF
            icolor(j+1) = 1
          END DO
          IF ( icolor(mytask_local+1) .EQ. 1 ) inthisone = .TRUE.
          CALL MPI_Comm_dup(mpi_comm_here,comdup,ierr)
          CALL MPI_Comm_split(comdup,icolor(mytask_local+1),mytask_local,mpi_comm_local,ierr)
          IF ( inthisone ) THEN
            dims(1) = nest_pes_y(i) 
            dims(2) = nest_pes_x(i)  
            isperiodic(1) = .false.
            isperiodic(2) = .false.
            CALL mpi_cart_create( mpi_comm_local, 2, dims, isperiodic, .false., comms_i_am_in(i), ierr )
          END IF
        END DO


        local_communicator = MPI_UNDEFINED
        CALL wrf_set_dm_quilt_comm( mpi_comm_here )   
        DO i = 1, max_dom
          local_communicator_store(i) = comms_i_am_in(i)
          domain_active_this_task(i) = ( local_communicator_store(i) .NE. MPI_UNDEFINED )
          IF ( local_communicator_store(i) .NE. MPI_UNDEFINED ) THEN
             CALL MPI_Comm_size( local_communicator_store(i), ntasks_store(i), ierr )
             CALL MPI_Comm_rank( local_communicator_store(i), mytask_store(i), ierr )
             CALL mpi_cart_coords( local_communicator_store(i), mytask_store(i), 2, coords, ierr )
             IF ( ierr .NE. 0 ) CALL wrf_error_fatal3("<stdin>",6342,&
'MPI_cart_coords fails ')
             mytask_y_store(i) = coords(1)   
             mytask_x_store(i) = coords(2)   
             CALL MPI_Comm_dup( local_communicator_store(i), comdup2, ierr )
             IF ( ierr .NE. 0 ) CALL wrf_error_fatal3("<stdin>",6347,&
'MPI_Comm_dup fails ')

             CALL MPI_Comm_split(comdup2,mytask_y_store(i),mytask_store(i),local_communicator_x_store(i),ierr)
             IF ( ierr .NE. 0 ) CALL wrf_error_fatal3("<stdin>",6351,&
'MPI_Comm_split fails for y ')

             CALL MPI_Comm_split(comdup2,mytask_x_store(i),mytask_store(i),local_communicator_y_store(i),ierr)
             IF ( ierr .NE. 0 ) CALL wrf_error_fatal3("<stdin>",6355,&
'MPI_Comm_split fails for x ')

             CALL MPI_Comm_size( local_communicator_x_store(i), ntasks_x_store(i), ierr )
             CALL MPI_Comm_rank( local_communicator_x_store(i), mytask_x_store(i), ierr )
             CALL MPI_Comm_size( local_communicator_y_store(i), ntasks_y_store(i), ierr )
             CALL MPI_Comm_rank( local_communicator_y_store(i), mytask_y_store(i), ierr )
          END IF
        END DO

        intercomm_active  = .FALSE.
        
        
        
        
        
        


        ntasks_local = num_compute_tasks
        DO nest_id = 2, max_dom
           par_id  = parent_id(nest_id)
           icolor2 = 0
           DO j = 1,ntasks_local 
             IF ( local_communicator_store( par_id ) .NE. MPI_UNDEFINED .OR. local_communicator_store( nest_id ) .NE. MPI_UNDEFINED ) icolor2(j)=1
           END DO
        
           icolor2 = 0
           mytask_is_nest = .FALSE.
           mytask_is_par = .FALSE.
           DO j = 1,ntasks_local

             IF ( comm_start(nest_id) .LE. j-1 .AND. j-1 .LT. comm_start(nest_id) + nest_pes_x(nest_id)*nest_pes_y(nest_id) )  THEN
               icolor2(j)=1
               if ( j-1 .EQ. mytask_local ) mytask_is_nest=.TRUE.
             END IF
             IF ( comm_start(par_id ) .LE. j-1 .AND. j-1 .LT. comm_start(par_id ) + nest_pes_x(par_id )*nest_pes_y(par_id ) )  THEN
               icolor2(j)=1
               if ( j-1 .EQ. mytask_local ) mytask_is_par=.TRUE.
             END IF
           END DO

           i = icolor2(mytask_local+1)
           CALL MPI_Comm_dup(mpi_comm_here,comdup,ierr)
           CALL MPI_Comm_split(comdup,i,origmytask,mpi_comm_me_and_mom,ierr)

           IF ( mytask_is_nest  ) THEN
              intercomm_active(nest_id)  = .TRUE.
              mpi_comm_to_mom(nest_id)   =  mpi_comm_me_and_mom
           END IF
           IF ( mytask_is_par ) THEN
              intercomm_active(par_id)              = .TRUE.
              mpi_comm_to_kid(which_kid(nest_id),par_id)  =  mpi_comm_me_and_mom
           END IF
        END DO
        DEALLOCATE( icolor )
        DEALLOCATE( icolor2 )
        DEALLOCATE( idomain )

      ELSE IF ( ( tasks_per_split .LE. ntasks_local .AND. tasks_per_split .LE. 0 ) ) THEN
        domain_active_this_task(1) = .TRUE.
        IF ( mod( ntasks_local, tasks_per_split ) .NE. 0 ) THEN
          CALL wrf_message( 'WARNING: tasks_per_split does not evenly divide ntasks. Some tasks will be wasted.' )
        END IF

        ALLOCATE( icolor(ntasks_local) )
        j = 0
        DO WHILE ( j .LT. ntasks_local / tasks_per_split )
          DO i = 1, tasks_per_split
            icolor( i + j * tasks_per_split ) = j
          END DO
          j = j + 1
        END DO

        CALL MPI_Comm_dup(mpi_comm_here,comdup,ierr)
        CALL MPI_Comm_split(comdup,icolor(mytask_local+1),mytask_local,mpi_comm_local,ierr)
        CALL wrf_set_dm_communicator( mpi_comm_local )
        CALL store_communicators_for_domain(1)
        DEALLOCATE( icolor )
      ELSE
        domain_active_this_task(1) = .TRUE.
        mpi_comm_local = mpi_comm_here
        CALL wrf_set_dm_communicator( mpi_comm_local )
        CALL store_communicators_for_domain(1)
      END IF

      CALL instate_communicators_for_domain(1)


   END SUBROUTINE split_communicator

   SUBROUTINE init_module_dm

      IMPLICIT NONE
      INTEGER mpi_comm_local, mpi_comm_here, ierr, mytask, nproc
      LOGICAL mpi_inited
      CALL mpi_initialized( mpi_inited, ierr )
      IF ( .NOT. mpi_inited ) THEN
        
        
        
        
        
        CALL mpi_init ( ierr )
        mpi_comm_here = MPI_COMM_WORLD
        CALL wrf_set_dm_communicator ( mpi_comm_here )
      END IF
      CALL wrf_get_dm_communicator( mpi_comm_local )

   END SUBROUTINE init_module_dm


   SUBROUTINE wrf_dm_move_nest ( parent, nest, dx, dy )
      USE module_domain, ONLY : domain
      IMPLICIT NONE
      TYPE (domain), INTENT(INOUT) :: parent, nest
      INTEGER, INTENT(IN)          :: dx,dy
      RETURN
   END SUBROUTINE wrf_dm_move_nest


   SUBROUTINE get_full_obs_vector( nsta, nerrf, niobf,          &
                                   mp_local_uobmask,            &
                                   mp_local_vobmask,            &
                                   mp_local_cobmask, errf )
      





        
    INTEGER, INTENT(IN)   :: nsta                
    INTEGER, INTENT(IN)   :: nerrf               
    INTEGER, INTENT(IN)   :: niobf               
    LOGICAL, INTENT(IN)   :: MP_LOCAL_UOBMASK(NIOBF)
    LOGICAL, INTENT(IN)   :: MP_LOCAL_VOBMASK(NIOBF)
    LOGICAL, INTENT(IN)   :: MP_LOCAL_COBMASK(NIOBF)
    REAL, INTENT(INOUT)   :: errf(nerrf, niobf)


        

    integer i, n, nlocal_dot, nlocal_crs
    REAL UVT_BUFFER(NIOBF)    
    REAL QRK_BUFFER(NIOBF)    
    REAL SFP_BUFFER(NIOBF)    
    REAL PBL_BUFFER(NIOBF)    
    REAL QATOB_BUFFER(NIOBF)  
    INTEGER N_BUFFER(NIOBF)
    REAL FULL_BUFFER(NIOBF)
    INTEGER IFULL_BUFFER(NIOBF)
    INTEGER, ALLOCATABLE , DIMENSION(:) ::  IDISPLACEMENT
    INTEGER, ALLOCATABLE , DIMENSION(:) ::  ICOUNT

    INTEGER :: MPI_COMM_COMP      
    INTEGER :: NPROCS             
    INTEGER :: IERR               


    CALL WRF_GET_DM_COMMUNICATOR(MPI_COMM_COMP)


    CALL MPI_COMM_SIZE( MPI_COMM_COMP, NPROCS, IERR )
    ALLOCATE (IDISPLACEMENT(NPROCS))
    ALLOCATE (ICOUNT(NPROCS))


   NLOCAL_DOT = 0
   DO N = 1, NSTA
     IF ( MP_LOCAL_UOBMASK(N) ) THEN      
       NLOCAL_DOT = NLOCAL_DOT + 1
       UVT_BUFFER(NLOCAL_DOT) = ERRF(1,N)        
       SFP_BUFFER(NLOCAL_DOT) = ERRF(7,N)        
       QRK_BUFFER(NLOCAL_DOT) = ERRF(9,N)        
       N_BUFFER(NLOCAL_DOT) = N
     END IF
   END DO
   CALL MPI_ALLGATHER(NLOCAL_DOT,1,MPI_INTEGER, &
                      ICOUNT,1,MPI_INTEGER,     &
                      MPI_COMM_COMP,IERR)
   I = 1

   IDISPLACEMENT(1) = 0
   DO I = 2, NPROCS
     IDISPLACEMENT(I) = IDISPLACEMENT(I-1) + ICOUNT(I-1)
   END DO
   CALL MPI_ALLGATHERV( N_BUFFER, NLOCAL_DOT, MPI_INTEGER,    &
                        IFULL_BUFFER, ICOUNT, IDISPLACEMENT,  &
                        MPI_INTEGER, MPI_COMM_COMP, IERR)

   CALL MPI_ALLGATHERV( UVT_BUFFER, NLOCAL_DOT, MPI_REAL,     &
                        FULL_BUFFER, ICOUNT, IDISPLACEMENT,   &
                        MPI_REAL, MPI_COMM_COMP, IERR)
   DO N = 1, NSTA
     ERRF(1,IFULL_BUFFER(N)) = FULL_BUFFER(N)
   END DO

   CALL MPI_ALLGATHERV( SFP_BUFFER, NLOCAL_DOT, MPI_REAL,     &
                        FULL_BUFFER, ICOUNT, IDISPLACEMENT,   &
                        MPI_REAL, MPI_COMM_COMP, IERR)
   DO N = 1, NSTA
     ERRF(7,IFULL_BUFFER(N)) = FULL_BUFFER(N)
   END DO

   CALL MPI_ALLGATHERV( QRK_BUFFER, NLOCAL_DOT, MPI_REAL,     &
                        FULL_BUFFER, ICOUNT, IDISPLACEMENT,   &
                        MPI_REAL, MPI_COMM_COMP, IERR)
   DO N = 1, NSTA
     ERRF(9,IFULL_BUFFER(N)) = FULL_BUFFER(N)
   END DO


   NLOCAL_DOT = 0
   DO N = 1, NSTA
     IF ( MP_LOCAL_VOBMASK(N) ) THEN         
       NLOCAL_DOT = NLOCAL_DOT + 1
       UVT_BUFFER(NLOCAL_DOT) = ERRF(2,N)    
       SFP_BUFFER(NLOCAL_DOT) = ERRF(8,N)    
       N_BUFFER(NLOCAL_DOT) = N
     END IF
   END DO
   CALL MPI_ALLGATHER(NLOCAL_DOT,1,MPI_INTEGER, &
                      ICOUNT,1,MPI_INTEGER,     &
                      MPI_COMM_COMP,IERR)
   I = 1

   IDISPLACEMENT(1) = 0
   DO I = 2, NPROCS
     IDISPLACEMENT(I) = IDISPLACEMENT(I-1) + ICOUNT(I-1)
   END DO
   CALL MPI_ALLGATHERV( N_BUFFER, NLOCAL_DOT, MPI_INTEGER,    &
                        IFULL_BUFFER, ICOUNT, IDISPLACEMENT,  &
                        MPI_INTEGER, MPI_COMM_COMP, IERR)

   CALL MPI_ALLGATHERV( UVT_BUFFER, NLOCAL_DOT, MPI_REAL,     &
                        FULL_BUFFER, ICOUNT, IDISPLACEMENT,   &
                        MPI_REAL, MPI_COMM_COMP, IERR)
   DO N = 1, NSTA
     ERRF(2,IFULL_BUFFER(N)) = FULL_BUFFER(N)
   END DO

   CALL MPI_ALLGATHERV( SFP_BUFFER, NLOCAL_DOT, MPI_REAL,     &
                        FULL_BUFFER, ICOUNT, IDISPLACEMENT,   &
                        MPI_REAL, MPI_COMM_COMP, IERR)
   DO N = 1, NSTA
     ERRF(8,IFULL_BUFFER(N)) = FULL_BUFFER(N)
   END DO


   NLOCAL_CRS = 0
   DO N = 1, NSTA
     IF ( MP_LOCAL_COBMASK(N) ) THEN       
       NLOCAL_CRS = NLOCAL_CRS + 1
       UVT_BUFFER(NLOCAL_CRS) = ERRF(3,N)     
       QRK_BUFFER(NLOCAL_CRS) = ERRF(4,N)     
       PBL_BUFFER(NLOCAL_CRS) = ERRF(5,N)     
       SFP_BUFFER(NLOCAL_CRS) = ERRF(6,N)     
       QATOB_BUFFER(NLOCAL_CRS) = ERRF(10,N)     
       N_BUFFER(NLOCAL_CRS) = N
     END IF
   END DO
   CALL MPI_ALLGATHER(NLOCAL_CRS,1,MPI_INTEGER, &
                      ICOUNT,1,MPI_INTEGER,     &
                      MPI_COMM_COMP,IERR)
   IDISPLACEMENT(1) = 0
   DO I = 2, NPROCS
     IDISPLACEMENT(I) = IDISPLACEMENT(I-1) + ICOUNT(I-1)
   END DO
   CALL MPI_ALLGATHERV( N_BUFFER, NLOCAL_CRS, MPI_INTEGER,    &
                        IFULL_BUFFER, ICOUNT, IDISPLACEMENT,  &
                        MPI_INTEGER, MPI_COMM_COMP, IERR)

   CALL MPI_ALLGATHERV( UVT_BUFFER, NLOCAL_CRS, MPI_REAL,     &
                        FULL_BUFFER, ICOUNT, IDISPLACEMENT,   &
                        MPI_REAL, MPI_COMM_COMP, IERR)

   DO N = 1, NSTA
     ERRF(3,IFULL_BUFFER(N)) = FULL_BUFFER(N)
   END DO

   CALL MPI_ALLGATHERV( QRK_BUFFER, NLOCAL_CRS, MPI_REAL,     &
                        FULL_BUFFER, ICOUNT, IDISPLACEMENT,   &
                        MPI_REAL, MPI_COMM_COMP, IERR)
   DO N = 1, NSTA
     ERRF(4,IFULL_BUFFER(N)) = FULL_BUFFER(N)
   END DO

   CALL MPI_ALLGATHERV( PBL_BUFFER, NLOCAL_CRS, MPI_REAL,     &
                        FULL_BUFFER, ICOUNT, IDISPLACEMENT,   &
                        MPI_REAL, MPI_COMM_COMP, IERR)
   DO N = 1, NSTA
     ERRF(5,IFULL_BUFFER(N)) = FULL_BUFFER(N)
   END DO

   CALL MPI_ALLGATHERV( SFP_BUFFER, NLOCAL_CRS, MPI_REAL,     &
                        FULL_BUFFER, ICOUNT, IDISPLACEMENT,   &
                        MPI_REAL, MPI_COMM_COMP, IERR)
   DO N = 1, NSTA
     ERRF(6,IFULL_BUFFER(N)) = FULL_BUFFER(N)
   END DO


   CALL MPI_ALLGATHERV( QATOB_BUFFER, NLOCAL_CRS, MPI_REAL,     &
                        FULL_BUFFER, ICOUNT, IDISPLACEMENT,   &
                        MPI_REAL, MPI_COMM_COMP, IERR)
   DO N = 1, NSTA
     ERRF(10,IFULL_BUFFER(N)) = FULL_BUFFER(N)
   END DO

    DEALLOCATE (IDISPLACEMENT)
    DEALLOCATE (ICOUNT)

   END SUBROUTINE get_full_obs_vector



   SUBROUTINE wrf_dm_maxtile_real ( val , tile)
      IMPLICIT NONE
      REAL val, val_all( ntasks )
      INTEGER tile
      INTEGER ierr






      INTEGER i, comm


      CALL wrf_get_dm_communicator ( comm )
      CALL mpi_allgather ( val, 1, getrealmpitype(), val_all , 1, getrealmpitype(), comm, ierr )
      val = val_all(1)
      tile = 1
      DO i = 2, ntasks
        IF ( val_all(i) .GT. val ) THEN
           tile = i
           val = val_all(i)
        END IF
      END DO

   END SUBROUTINE wrf_dm_maxtile_real


   SUBROUTINE wrf_dm_mintile_real ( val , tile)
      IMPLICIT NONE
      REAL val, val_all( ntasks )
      INTEGER tile
      INTEGER ierr






      INTEGER i, comm


      CALL wrf_get_dm_communicator ( comm )
      CALL mpi_allgather ( val, 1, getrealmpitype(), val_all , 1, getrealmpitype(), comm, ierr )
      val = val_all(1)
      tile = 1
      DO i = 2, ntasks
        IF ( val_all(i) .LT. val ) THEN
           tile = i
           val = val_all(i)
        END IF
      END DO

   END SUBROUTINE wrf_dm_mintile_real


   SUBROUTINE wrf_dm_mintile_double ( val , tile)
      IMPLICIT NONE
      DOUBLE PRECISION val, val_all( ntasks )
      INTEGER tile
      INTEGER ierr






      INTEGER i, comm


      CALL wrf_get_dm_communicator ( comm )
      CALL mpi_allgather ( val, 1, MPI_DOUBLE_PRECISION, val_all , 1, MPI_DOUBLE_PRECISION, comm, ierr )
      val = val_all(1)
      tile = 1
      DO i = 2, ntasks
        IF ( val_all(i) .LT. val ) THEN
           tile = i
           val = val_all(i)
        END IF
      END DO

   END SUBROUTINE wrf_dm_mintile_double


   SUBROUTINE wrf_dm_tile_val_int ( val , tile)
      IMPLICIT NONE
      INTEGER val, val_all( ntasks )
      INTEGER tile
      INTEGER ierr





      INTEGER i, comm


      CALL wrf_get_dm_communicator ( comm )
      CALL mpi_allgather ( val, 1, MPI_INTEGER, val_all , 1, MPI_INTEGER, comm, ierr )
      val = val_all(tile)

   END SUBROUTINE wrf_dm_tile_val_int

   SUBROUTINE wrf_get_hostname  ( str )
      CHARACTER*(*) str
      CHARACTER tmp(512)
      INTEGER i , n, cs
      CALL rsl_lite_get_hostname( tmp, 512, n, cs )
      DO i = 1, n
        str(i:i) = tmp(i)
      END DO
      RETURN
   END SUBROUTINE wrf_get_hostname

   SUBROUTINE wrf_get_hostid  ( hostid )
      INTEGER hostid
      CHARACTER tmp(512)
      INTEGER i, sz, n, cs
      CALL rsl_lite_get_hostname( tmp, 512, n, cs )
      hostid = cs
      RETURN
   END SUBROUTINE wrf_get_hostid

END MODULE module_dm


   SUBROUTINE push_communicators_for_domain( id )
      USE module_dm
      INTEGER, INTENT(IN) :: id   


      IF ( communicator_stack_cursor .GE. max_domains ) CALL wrf_error_fatal3("<stdin>",6803,&
"push_communicators_for_domain would excede stacksize")
      communicator_stack_cursor = communicator_stack_cursor + 1

      id_stack(communicator_stack_cursor) = current_id
      local_communicator_stack( communicator_stack_cursor )    =    local_communicator
      local_communicator_periodic_stack( communicator_stack_cursor )  =    local_communicator_periodic
      local_iocommunicator_stack( communicator_stack_cursor )  =    local_iocommunicator
      local_communicator_x_stack( communicator_stack_cursor )  =    local_communicator_x
      local_communicator_y_stack( communicator_stack_cursor )  =    local_communicator_y
      ntasks_stack( communicator_stack_cursor )        =    ntasks
      ntasks_y_stack( communicator_stack_cursor )      =    ntasks_y
      ntasks_x_stack( communicator_stack_cursor )      =    ntasks_x
      mytask_stack( communicator_stack_cursor )        =    mytask
      mytask_x_stack( communicator_stack_cursor )       =    mytask_x
      mytask_y_stack( communicator_stack_cursor )       =    mytask_y

      CALL instate_communicators_for_domain( id )

   END SUBROUTINE push_communicators_for_domain
   SUBROUTINE pop_communicators_for_domain
      USE module_dm
      IMPLICIT NONE
      

      IF ( communicator_stack_cursor .LT. 1 ) CALL wrf_error_fatal3("<stdin>",6828,&
"pop_communicators_for_domain on empty stack")
      current_id = id_stack(communicator_stack_cursor)
      local_communicator = local_communicator_stack( communicator_stack_cursor )
      local_communicator_periodic = local_communicator_periodic_stack( communicator_stack_cursor )
      local_iocommunicator = local_iocommunicator_stack( communicator_stack_cursor )
      local_communicator_x = local_communicator_x_stack( communicator_stack_cursor )
      local_communicator_y = local_communicator_y_stack( communicator_stack_cursor )
      ntasks = ntasks_stack( communicator_stack_cursor )
      ntasks_y = ntasks_y_stack( communicator_stack_cursor )
      ntasks_x = ntasks_x_stack( communicator_stack_cursor )
      mytask = mytask_stack( communicator_stack_cursor )
      mytask_x = mytask_x_stack( communicator_stack_cursor )
      mytask_y = mytask_y_stack( communicator_stack_cursor )
      communicator_stack_cursor = communicator_stack_cursor - 1

   END SUBROUTINE pop_communicators_for_domain
   SUBROUTINE instate_communicators_for_domain( id )
      USE module_dm


      IMPLICIT NONE
      INTEGER, INTENT(IN) :: id
      INTEGER ierr
      current_id = id
      local_communicator          = local_communicator_store( id )
      local_communicator_periodic = local_communicator_periodic_store( id )
      local_iocommunicator        = local_iocommunicator_store( id )
      local_communicator_x        = local_communicator_x_store( id )
      local_communicator_y        = local_communicator_y_store( id )
      ntasks         = ntasks_store( id )
      mytask         = mytask_store( id )
      ntasks_x       = ntasks_x_store( id )
      ntasks_y       = ntasks_y_store( id )
      mytask_x       = mytask_x_store( id )
      mytask_y       = mytask_y_store( id )

   END SUBROUTINE instate_communicators_for_domain
   SUBROUTINE store_communicators_for_domain( id )
      USE module_dm


      IMPLICIT NONE
      INTEGER, INTENT(IN) :: id
      local_communicator_store( id )    =    local_communicator
      local_communicator_periodic_store( id )  =    local_communicator_periodic
      local_iocommunicator_store( id )  =    local_iocommunicator
      local_communicator_x_store( id )  =    local_communicator_x
      local_communicator_y_store( id )  =    local_communicator_y
      ntasks_store( id )        =    ntasks
      ntasks_x_store( id )      =    ntasks_x
      ntasks_y_store( id )      =    ntasks_y
      mytask_store( id )        =    mytask
      mytask_x_store( id )      =    mytask_x
      mytask_y_store( id )      =    mytask_y

   END SUBROUTINE store_communicators_for_domain





SUBROUTINE wrf_dm_patch_domain ( id  , domdesc , parent_id , parent_domdesc , &
                          sd1 , ed1 , sp1 , ep1 , sm1 , em1 , &
                          sd2 , ed2 , sp2 , ep2 , sm2 , em2 , &
                          sd3 , ed3 , sp3 , ep3 , sm3 , em3 , &
                                      sp1x , ep1x , sm1x , em1x , &
                                      sp2x , ep2x , sm2x , em2x , &
                                      sp3x , ep3x , sm3x , em3x , &
                                      sp1y , ep1y , sm1y , em1y , &
                                      sp2y , ep2y , sm2y , em2y , &
                                      sp3y , ep3y , sm3y , em3y , &
                          bdx , bdy )
   USE module_domain, ONLY : domain, head_grid, find_grid_by_id
   USE module_dm, ONLY : patch_domain_rsl_lite  
   IMPLICIT NONE

   INTEGER, INTENT(IN)   :: sd1 , ed1 , sd2 , ed2 , sd3 , ed3 , bdx , bdy
   INTEGER, INTENT(OUT)  :: sp1 , ep1 , sp2 , ep2 , sp3 , ep3 , &
                            sm1 , em1 , sm2 , em2 , sm3 , em3
   INTEGER               :: sp1x , ep1x , sp2x , ep2x , sp3x , ep3x , &
                            sm1x , em1x , sm2x , em2x , sm3x , em3x
   INTEGER               :: sp1y , ep1y , sp2y , ep2y , sp3y , ep3y , &
                            sm1y , em1y , sm2y , em2y , sm3y , em3y
   INTEGER, INTENT(INOUT):: id  , domdesc , parent_id , parent_domdesc

   TYPE(domain), POINTER :: parent
   TYPE(domain), POINTER :: grid_ptr

   
   
   
   

   NULLIFY( parent )
   grid_ptr => head_grid
   CALL find_grid_by_id( parent_id , grid_ptr , parent )

   CALL push_communicators_for_domain(id)

   CALL patch_domain_rsl_lite ( id  , parent, parent_id , &
                           sd1 , ed1 , sp1 , ep1 , sm1 , em1 , &
                           sd2 , ed2 , sp2 , ep2 , sm2 , em2 , &
                           sd3 , ed3 , sp3 , ep3 , sm3 , em3 , &
                                      sp1x , ep1x , sm1x , em1x , &
                                      sp2x , ep2x , sm2x , em2x , &
                                      sp3x , ep3x , sm3x , em3x , &
                                      sp1y , ep1y , sm1y , em1y , &
                                      sp2y , ep2y , sm2y , em2y , &
                                      sp3y , ep3y , sm3y , em3y , &
                           bdx , bdy )

   CALL pop_communicators_for_domain

   RETURN
END SUBROUTINE wrf_dm_patch_domain

SUBROUTINE wrf_termio_dup( comm )
  IMPLICIT NONE
  INTEGER, INTENT(IN) :: comm
  INTEGER mytask, ntasks

  INTEGER ierr
  INCLUDE 'mpif.h'
  CALL mpi_comm_size(comm, ntasks, ierr )
  CALL mpi_comm_rank(comm, mytask, ierr )
  write(0,*)'starting wrf task ',mytask,' of ',ntasks
  CALL rsl_error_dup1( mytask, ntasks )

END SUBROUTINE wrf_termio_dup

SUBROUTINE wrf_get_myproc( myproc )
  USE module_dm , ONLY : mytask
  IMPLICIT NONE
  INTEGER myproc
  myproc = mytask
  RETURN
END SUBROUTINE wrf_get_myproc

SUBROUTINE wrf_get_nproc( nproc )
  USE module_dm , ONLY : ntasks
  IMPLICIT NONE
  INTEGER nproc
  nproc = ntasks
  RETURN
END SUBROUTINE wrf_get_nproc

SUBROUTINE wrf_get_nprocx( nprocx )
  USE module_dm , ONLY : ntasks_x
  IMPLICIT NONE
  INTEGER nprocx
  nprocx = ntasks_x
  RETURN
END SUBROUTINE wrf_get_nprocx

SUBROUTINE wrf_get_nprocy( nprocy )
  USE module_dm , ONLY : ntasks_y
  IMPLICIT NONE
  INTEGER nprocy
  nprocy = ntasks_y
  RETURN
END SUBROUTINE wrf_get_nprocy

SUBROUTINE wrf_dm_bcast_bytes ( buf , size )
   USE module_dm , ONLY : local_communicator
   IMPLICIT NONE

   INCLUDE 'mpif.h'

   INTEGER size

   INTEGER*1 BUF(size)

   CALL BYTE_BCAST ( buf , size, local_communicator )

   RETURN
END SUBROUTINE wrf_dm_bcast_bytes

SUBROUTINE wrf_dm_bcast_string( BUF, N1 )
   IMPLICIT NONE
   INTEGER n1




   CHARACTER*(*) buf

   INTEGER ibuf(256),i,n
   CHARACTER*256 tstr
   n = n1
   
   
   CALL wrf_dm_bcast_integer( n , 1 )
   IF (n .GT. 256) n = 256
   IF (n .GT. 0 ) then
     DO i = 1, n
       ibuf(I) = ichar(buf(I:I))
     END DO
     CALL wrf_dm_bcast_integer( ibuf, n )
     buf = ''
     DO i = 1, n
       buf(i:i) = char(ibuf(i))
     END DO
   END IF

   RETURN
END SUBROUTINE wrf_dm_bcast_string

SUBROUTINE wrf_dm_bcast_string_comm( BUF, N1, COMM )
   IMPLICIT NONE
   INTEGER n1
   INTEGER COMM




   CHARACTER*(*) buf

   INTEGER ibuf(256),i,n
   CHARACTER*256 tstr
   n = n1
   
   
   CALL BYTE_BCAST( n, 4, COMM )
   IF (n .GT. 256) n = 256
   IF (n .GT. 0 ) then
     DO i = 1, n
       ibuf(I) = ichar(buf(I:I))
     END DO
     CALL BYTE_BCAST( ibuf, N*4, COMM )
     buf = ''
     DO i = 1, n
       buf(i:i) = char(ibuf(i))
     END DO
   END IF

   RETURN
END SUBROUTINE wrf_dm_bcast_string_comm

SUBROUTINE wrf_dm_bcast_integer( BUF, N1 )
   IMPLICIT NONE
   INTEGER n1
   INTEGER  buf(*)
   CALL wrf_dm_bcast_bytes ( BUF , N1 * 4 )
   RETURN
END SUBROUTINE wrf_dm_bcast_integer

SUBROUTINE wrf_dm_bcast_double( BUF, N1 )
   IMPLICIT NONE
   INTEGER n1




   REAL  buf(*)
   CALL wrf_dm_bcast_bytes ( BUF , N1 * 8 )
   RETURN
END SUBROUTINE wrf_dm_bcast_double

SUBROUTINE wrf_dm_bcast_real( BUF, N1 )
   IMPLICIT NONE
   INTEGER n1
   REAL  buf(*)
   CALL wrf_dm_bcast_bytes ( BUF , N1 * 4 )
   RETURN
END SUBROUTINE wrf_dm_bcast_real

SUBROUTINE wrf_dm_bcast_logical( BUF, N1 )
   IMPLICIT NONE
   INTEGER n1
   LOGICAL  buf(*)
   CALL wrf_dm_bcast_bytes ( BUF , N1 * 4 )
   RETURN
END SUBROUTINE wrf_dm_bcast_logical

SUBROUTINE write_68( grid, v , s , &
                   ids, ide, jds, jde, kds, kde, &
                   ims, ime, jms, jme, kms, kme, &
                   its, ite, jts, jte, kts, kte )
  USE module_domain, ONLY : domain
  IMPLICIT NONE
  TYPE(domain) , INTENT (INOUT) :: grid
  CHARACTER *(*) s
  INTEGER ids, ide, jds, jde, kds, kde, &
          ims, ime, jms, jme, kms, kme, &
          its, ite, jts, jte, kts, kte
  REAL, DIMENSION( ims:ime , kms:kme, jms:jme ) :: v

  INTEGER i,j,k,ierr

  logical, external :: wrf_dm_on_monitor
  real globbuf( ids:ide, kds:kde, jds:jde )
  character*3 ord, stag

  if ( kds == kde ) then
    ord = 'xy'
    stag = 'xy'
  CALL wrf_patch_to_global_real ( v, globbuf, grid%domdesc, stag, ord, &
                     ids, ide, jds, jde, kds, kde, &
                     ims, ime, jms, jme, kms, kme, &
                     its, ite, jts, jte, kts, kte )
  else

    stag = 'xyz'
    ord = 'xzy'
  CALL wrf_patch_to_global_real ( v, globbuf, grid%domdesc, stag, ord, &
                     ids, ide, kds, kde, jds, jde, &
                     ims, ime, kms, kme, jms, jme, &
                     its, ite, kts, kte, jts, jte )
  endif


  if ( wrf_dm_on_monitor() ) THEN
    WRITE(68,*) ide-ids+1, jde-jds+1 , s
    DO j = jds, jde
    DO i = ids, ide
       WRITE(68,*) globbuf(i,1,j)
    END DO
    END DO
  endif

  RETURN
END

   SUBROUTINE wrf_abort


      USE module_cpl, ONLY : coupler_on, cpl_abort


      IMPLICIT NONE

      INCLUDE 'mpif.h'
      INTEGER ierr

      IF ( coupler_on ) THEN
         CALL cpl_abort( 'wrf_abort', 'look for abort message in rsl* files' )
      ELSE

         CALL mpi_abort(MPI_COMM_WORLD,1,ierr)

      END IF

   END SUBROUTINE wrf_abort

   SUBROUTINE wrf_dm_shutdown
      IMPLICIT NONE

      INTEGER ierr
      CALL MPI_FINALIZE( ierr )

      RETURN
   END SUBROUTINE wrf_dm_shutdown

   LOGICAL FUNCTION wrf_dm_on_monitor()
      IMPLICIT NONE

      INCLUDE 'mpif.h'
      INTEGER tsk, ierr, mpi_comm_local
      CALL wrf_get_dm_communicator( mpi_comm_local )
      IF ( mpi_comm_local .NE. MPI_UNDEFINED ) THEN
        CALL mpi_comm_rank ( mpi_comm_local, tsk , ierr )
        wrf_dm_on_monitor = tsk .EQ. 0
      ELSE
        wrf_dm_on_monitor = .FALSE.
      END IF

      RETURN
   END FUNCTION wrf_dm_on_monitor

   SUBROUTINE rsl_comm_iter_init(shw,ps,pe)
      INTEGER shw, ps, pe
      INTEGER iter, plus_send_start, plus_recv_start, &
                    minus_send_start, minus_recv_start
      COMMON /rcii/ iter, plus_send_start, plus_recv_start, &
                          minus_send_start, minus_recv_start
      iter = 0
      minus_send_start = ps
      minus_recv_start = ps-1
      plus_send_start = pe
      plus_recv_start = pe+1
   END SUBROUTINE rsl_comm_iter_init

   LOGICAL FUNCTION rsl_comm_iter ( id , is_intermediate,                     &
                                    shw ,  xy , ds, de_in, ps, pe, nds,nde, &
                                    sendbeg_m, sendw_m, sendbeg_p, sendw_p,   &
                                    recvbeg_m, recvw_m, recvbeg_p, recvw_p    )
      USE module_dm, ONLY : ntasks_x, ntasks_y, mytask_x, mytask_y, minx, miny, &
                            nest_pes_x, nest_pes_y
      IMPLICIT NONE
      INTEGER, INTENT(IN)  :: id,shw,xy,ds,de_in,ps,pe,nds,nde
      LOGICAL, INTENT(IN)  :: is_intermediate  
      INTEGER, INTENT(OUT) :: sendbeg_m, sendw_m, sendbeg_p, sendw_p
      INTEGER, INTENT(OUT) :: recvbeg_m, recvw_m, recvbeg_p, recvw_p
      INTEGER k, kn, ni, nj, de, Px, Py, nt, ntx, nty, me, lb, ub, ierr
      INTEGER dum
      LOGICAL went
      INTEGER iter, plus_send_start, plus_recv_start, &
                    minus_send_start, minus_recv_start
      INTEGER parent_grid_ratio, parent_start
      COMMON /rcii/ iter, plus_send_start, plus_recv_start, &
                          minus_send_start, minus_recv_start

      de = de_in
      ntx = nest_pes_x(id)
      nty = nest_pes_y(id)
      IF ( xy .EQ. 1 ) THEN  
        nt = ntasks_x
        me = mytask_x
        dum = 2 * nty  
        IF ( is_intermediate ) THEN
           CALL nl_get_i_parent_start(id,parent_start)
           CALL nl_get_parent_grid_ratio(id,parent_grid_ratio)
        END IF
      ELSE
        nt = ntasks_y
        me = mytask_y
        dum = 2 * ntx  
        IF ( is_intermediate ) THEN
           CALL nl_get_j_parent_start(id,parent_start)
           CALL nl_get_parent_grid_ratio(id,parent_grid_ratio)
        END IF
      END IF
      iter = iter + 1


      went = .FALSE.
      
      sendw_m = 0
      sendbeg_m = 1
      IF ( me .GT. 0 ) THEN
        lb = minus_send_start
        sendbeg_m = lb-ps+1
        DO k = lb,ps+shw-1
          went = .TRUE.
          IF ( xy .eq. 1 ) THEN
            IF ( is_intermediate ) THEN
              kn =  ( k - parent_start ) * parent_grid_ratio + 1 + 1 ;
              CALL task_for_point (kn,1,nds,nde,1,dum,ntx,nty,Px,Py,minx,miny,ierr) 
              IF ( ierr .NE. 0 ) CALL wrf_error_fatal3("<stdin>",7267,&
'error code returned by task_for_point in module_dm.F (h)')
            ELSE
              CALL task_for_point (k,1,ds,de,1,dum,ntx,nty,Px,Py,minx,miny,ierr) 
              IF ( ierr .NE. 0 ) CALL wrf_error_fatal3("<stdin>",7271,&
'error code returned by task_for_point in module_dm.F (i)')
            END IF
            IF ( Px .NE. me+(iter-1) ) THEN
              exit
            END IF
          ELSE
            IF ( is_intermediate ) THEN
              kn =  ( k - parent_start ) * parent_grid_ratio + 1 + 1 ;
              CALL task_for_point (1,kn,1,dum,nds,nde,ntx,nty,Px,Py,minx,miny,ierr) 
              IF ( ierr .NE. 0 ) CALL wrf_error_fatal3("<stdin>",7281,&
'error code returned by task_for_point in module_dm.F (h)')
            ELSE
              CALL task_for_point (1,k,1,dum,ds,de,ntx,nty,Px,Py,minx,miny,ierr) 
              IF ( ierr .NE. 0 ) CALL wrf_error_fatal3("<stdin>",7285,&
'error code returned by task_for_point in module_dm.F (i)')
            END IF
            IF ( Py .NE. me+(iter-1) ) THEN
              exit
            END IF
          END IF
          minus_send_start = minus_send_start+1
          sendw_m = sendw_m + 1
        END DO
      END IF
      
      recvw_m = 0
      recvbeg_m = 1
      IF ( me .GT. 0 ) THEN
        ub = minus_recv_start
        recvbeg_m = ps - ub
        DO k = minus_recv_start,ps-shw,-1
          went = .TRUE.
          IF ( xy .eq. 1 ) THEN
          IF ( is_intermediate ) THEN
            kn =  ( k - parent_start ) * parent_grid_ratio + 1 + 1 ;
              CALL task_for_point (kn,1,nds,nde,1,dum,ntx,nty,Px,Py,minx,miny,ierr) 
              IF ( ierr .NE. 0 ) CALL wrf_error_fatal3("<stdin>",7308,&
'error code returned by task_for_point in module_dm.F (j)')
          ELSE
              CALL task_for_point (k,1,ds,de,1,dum,ntx,nty,Px,Py,minx,miny,ierr) 
              IF ( ierr .NE. 0 ) CALL wrf_error_fatal3("<stdin>",7312,&
'error code returned by task_for_point in module_dm.F (k)')
          END IF
          IF ( Px .NE. me-iter ) THEN
            exit
          END IF
          ELSE
            IF ( is_intermediate ) THEN
              kn =  ( k - parent_start ) * parent_grid_ratio + 1 + 1 ;
              CALL task_for_point (1,kn,1,dum,nds,nde,ntx,nty,Px,Py,minx,miny,ierr) 
              IF ( ierr .NE. 0 ) CALL wrf_error_fatal3("<stdin>",7322,&
'error code returned by task_for_point in module_dm.F (j)')
            ELSE
              CALL task_for_point (1,k,1,dum,ds,de,ntx,nty,Px,Py,minx,miny,ierr) 
              IF ( ierr .NE. 0 ) CALL wrf_error_fatal3("<stdin>",7326,&
'error code returned by task_for_point in module_dm.F (k)')
            END IF
            IF ( Py .NE. me-iter ) THEN
              exit
            END IF
          END IF
          minus_recv_start = minus_recv_start-1
          recvw_m = recvw_m + 1
        END DO
      END IF

      
      sendw_p = 0
      sendbeg_p = 1
      IF ( ( xy .eq. 1 .and. me .LT. ntx-1 ) .OR. ( xy .eq. 0 .and. me .LT. nty-1 ) ) THEN
        ub = plus_send_start
        sendbeg_p = pe - ub + 1
        DO k = ub,pe-shw+1,-1
          went = .TRUE.
          IF ( xy .eq. 1 ) THEN
          IF ( is_intermediate ) THEN
            kn =  ( k - parent_start ) * parent_grid_ratio + 1 + 1 ;
              CALL task_for_point (kn,1,nds,nde,1,dum,ntx,nty,Px,Py,minx,miny,ierr) 
              IF ( ierr .NE. 0 ) CALL wrf_error_fatal3("<stdin>",7350,&
'error code returned by task_for_point in module_dm.F (l)')
          ELSE
              CALL task_for_point (k,1,ds,de,1,dum,ntx,nty,Px,Py,minx,miny,ierr) 
              IF ( ierr .NE. 0 ) CALL wrf_error_fatal3("<stdin>",7354,&
'error code returned by task_for_point in module_dm.F (m)')
          END IF
          IF ( Px .NE. me-(iter-1) ) THEN
            exit
          END IF
          ELSE
            IF ( is_intermediate ) THEN
              kn =  ( k - parent_start ) * parent_grid_ratio + 1 + 1 ;
              CALL task_for_point (1,kn,1,dum,nds,nde,ntx,nty,Px,Py,minx,miny,ierr) 
              IF ( ierr .NE. 0 ) CALL wrf_error_fatal3("<stdin>",7364,&
'error code returned by task_for_point in module_dm.F (l)')
            ELSE
              CALL task_for_point (1,k,1,dum,ds,de,ntx,nty,Px,Py,minx,miny,ierr) 
              IF ( ierr .NE. 0 ) CALL wrf_error_fatal3("<stdin>",7368,&
'error code returned by task_for_point in module_dm.F (m)')
            END IF
            IF ( Py .NE. me-(iter-1) ) THEN
              exit
            END IF
          END IF
          plus_send_start = plus_send_start - 1
          sendw_p = sendw_p + 1
        END DO
      END IF
      
      recvw_p = 0
      recvbeg_p = 1
      IF ( ( xy .eq. 1 .and. me .LT. ntx-1 ) .OR. ( xy .eq. 0 .and. me .LT. nty-1 ) ) THEN
        lb = plus_recv_start
        recvbeg_p = lb - pe
        DO k = lb,pe+shw
          went = .TRUE.
          IF ( xy .eq. 1 ) THEN
          IF ( is_intermediate ) THEN
            kn =  ( k - parent_start ) * parent_grid_ratio + 1 + 1 ;
              CALL task_for_point (kn,1,nds,nde,1,dum,ntx,nty,Px,Py,minx,miny,ierr) 
              IF ( ierr .NE. 0 ) CALL wrf_error_fatal3("<stdin>",7391,&
'error code returned by task_for_point in module_dm.F (n)')
          ELSE

              CALL task_for_point (k,1,ds,de,1,dum,ntx,nty,Px,Py,minx,miny,ierr) 

              IF ( ierr .NE. 0 ) CALL wrf_error_fatal3("<stdin>",7397,&
'error code returned by task_for_point in module_dm.F (o)')
          END IF
          IF ( Px .NE. me+iter ) THEN
            exit
          END IF
          ELSE
            IF ( is_intermediate ) THEN
              kn =  ( k - parent_start ) * parent_grid_ratio + 1 + 1 ;
              CALL task_for_point (1,kn,1,dum,nds,nde,ntx,nty,Px,Py,minx,miny,ierr) 
              IF ( ierr .NE. 0 ) CALL wrf_error_fatal3("<stdin>",7407,&
'error code returned by task_for_point in module_dm.F (n)')
            ELSE
              CALL task_for_point (1,k,1,dum,ds,de,ntx,nty,Px,Py,minx,miny,ierr) 
              IF ( ierr .NE. 0 ) CALL wrf_error_fatal3("<stdin>",7411,&
'error code returned by task_for_point in module_dm.F (o)')
            END IF
            IF ( Py .NE. me+iter ) THEN
              exit
            END IF
          END IF
          plus_recv_start = plus_recv_start + 1
          recvw_p = recvw_p + 1
        END DO
      END IF

      



      
      
      rsl_comm_iter = went
   END FUNCTION rsl_comm_iter

   INTEGER FUNCTION wrf_dm_monitor_rank()
      IMPLICIT NONE
      wrf_dm_monitor_rank = 0
      RETURN
   END FUNCTION wrf_dm_monitor_rank


   SUBROUTINE wrf_get_dm_communicator_for_id ( id, communicator )
      USE module_dm , ONLY : local_communicator_store, mpi_comm_allcompute
      IMPLICIT NONE
      INTEGER , INTENT(IN) :: id
      INTEGER , INTENT(OUT) :: communicator
      IF ( id .le. 0 ) THEN
        communicator = mpi_comm_allcompute
      ELSE
        communicator = local_communicator_store(id)
      END IF
      RETURN
   END SUBROUTINE wrf_get_dm_communicator_for_id

   SUBROUTINE wrf_get_dm_communicator ( communicator )
      USE module_dm , ONLY : local_communicator
      IMPLICIT NONE
      INTEGER , INTENT(OUT) :: communicator
      communicator = local_communicator
      RETURN
   END SUBROUTINE wrf_get_dm_communicator

   SUBROUTINE wrf_get_dm_communicator_x ( communicator )
      USE module_dm , ONLY : local_communicator_x
      IMPLICIT NONE
      INTEGER , INTENT(OUT) :: communicator
      communicator = local_communicator_x
      RETURN
   END SUBROUTINE wrf_get_dm_communicator_x

   SUBROUTINE wrf_get_dm_communicator_y ( communicator )
      USE module_dm , ONLY : local_communicator_y
      IMPLICIT NONE
      INTEGER , INTENT(OUT) :: communicator
      communicator = local_communicator_y
      RETURN
   END SUBROUTINE wrf_get_dm_communicator_y

   SUBROUTINE wrf_get_dm_iocommunicator ( iocommunicator )
      USE module_dm , ONLY : local_iocommunicator
      IMPLICIT NONE
      INTEGER , INTENT(OUT) :: iocommunicator
      iocommunicator = local_iocommunicator
      RETURN
   END SUBROUTINE wrf_get_dm_iocommunicator

   SUBROUTINE wrf_set_dm_communicator ( communicator )
      USE module_dm , ONLY : local_communicator
      IMPLICIT NONE
      INTEGER , INTENT(IN) :: communicator
      local_communicator = communicator
      RETURN
   END SUBROUTINE wrf_set_dm_communicator

   SUBROUTINE wrf_set_dm_iocommunicator ( iocommunicator )
      USE module_dm , ONLY : local_iocommunicator
      IMPLICIT NONE
      INTEGER , INTENT(IN) :: iocommunicator
      local_iocommunicator = iocommunicator
      RETURN
   END SUBROUTINE wrf_set_dm_iocommunicator

   SUBROUTINE wrf_get_dm_ntasks_x ( retval )
      USE module_dm , ONLY : ntasks_x
      IMPLICIT NONE
      INTEGER , INTENT(OUT) :: retval
      retval = ntasks_x
      RETURN
   END SUBROUTINE wrf_get_dm_ntasks_x

   SUBROUTINE wrf_get_dm_ntasks_y ( retval )
      USE module_dm , ONLY : ntasks_y
      IMPLICIT NONE
      INTEGER , INTENT(OUT) :: retval
      retval = ntasks_y
      RETURN
   END SUBROUTINE wrf_get_dm_ntasks_y


   SUBROUTINE wrf_set_dm_quilt_comm ( communicator )
      USE module_dm , ONLY : local_quilt_comm
      IMPLICIT NONE
      INTEGER , INTENT(IN) :: communicator
      local_quilt_comm = communicator
      RETURN
   END SUBROUTINE wrf_set_dm_quilt_comm

   SUBROUTINE wrf_get_dm_quilt_comm ( communicator )
      USE module_dm , ONLY : local_quilt_comm
      IMPLICIT NONE
      INTEGER , INTENT(OUT) :: communicator
      communicator = local_quilt_comm
      RETURN
   END SUBROUTINE wrf_get_dm_quilt_comm




   SUBROUTINE wrf_patch_to_global_real (buf,globbuf,domdesc,stagger,ordering,&
                                       DS1,DE1,DS2,DE2,DS3,DE3,&
                                       MS1,ME1,MS2,ME2,MS3,ME3,&
                                       PS1,PE1,PS2,PE2,PS3,PE3 )
       IMPLICIT NONE
       INTEGER                         DS1,DE1,DS2,DE2,DS3,DE3,&
                                       MS1,ME1,MS2,ME2,MS3,ME3,&
                                       PS1,PE1,PS2,PE2,PS3,PE3
       CHARACTER *(*) stagger,ordering
       INTEGER fid,domdesc
       REAL globbuf(*)
       REAL buf(*)

       CALL wrf_patch_to_global_generic (buf,globbuf,domdesc,stagger,ordering,4,&
                                         DS1,DE1,DS2,DE2,DS3,DE3,&
                                         MS1,ME1,MS2,ME2,MS3,ME3,&
                                         PS1,PE1,PS2,PE2,PS3,PE3 )

       RETURN
   END SUBROUTINE wrf_patch_to_global_real

   SUBROUTINE wrf_patch_to_global_double (buf,globbuf,domdesc,stagger,ordering,&
                                       DS1,DE1,DS2,DE2,DS3,DE3,&
                                       MS1,ME1,MS2,ME2,MS3,ME3,&
                                       PS1,PE1,PS2,PE2,PS3,PE3 )
       IMPLICIT NONE
       INTEGER                         DS1,DE1,DS2,DE2,DS3,DE3,&
                                       MS1,ME1,MS2,ME2,MS3,ME3,&
                                       PS1,PE1,PS2,PE2,PS3,PE3
       CHARACTER *(*) stagger,ordering
       INTEGER fid,domdesc




       REAL globbuf(*)
       REAL buf(*)

       CALL wrf_patch_to_global_generic (buf,globbuf,domdesc,stagger,ordering,8,&
                                         DS1,DE1,DS2,DE2,DS3,DE3,&
                                         MS1,ME1,MS2,ME2,MS3,ME3,&
                                         PS1,PE1,PS2,PE2,PS3,PE3 )

       RETURN
   END SUBROUTINE wrf_patch_to_global_double


   SUBROUTINE wrf_patch_to_global_integer (buf,globbuf,domdesc,stagger,ordering,&
                                       DS1,DE1,DS2,DE2,DS3,DE3,&
                                       MS1,ME1,MS2,ME2,MS3,ME3,&
                                       PS1,PE1,PS2,PE2,PS3,PE3 )
       IMPLICIT NONE
       INTEGER                         DS1,DE1,DS2,DE2,DS3,DE3,&
                                       MS1,ME1,MS2,ME2,MS3,ME3,&
                                       PS1,PE1,PS2,PE2,PS3,PE3
       CHARACTER *(*) stagger,ordering
       INTEGER fid,domdesc
       INTEGER globbuf(*)
       INTEGER buf(*)

       CALL wrf_patch_to_global_generic (buf,globbuf,domdesc,stagger,ordering,4,&
                                         DS1,DE1,DS2,DE2,DS3,DE3,&
                                         MS1,ME1,MS2,ME2,MS3,ME3,&
                                         PS1,PE1,PS2,PE2,PS3,PE3 )

       RETURN
   END SUBROUTINE wrf_patch_to_global_integer


   SUBROUTINE wrf_patch_to_global_logical (buf,globbuf,domdesc,stagger,ordering,&
                                       DS1,DE1,DS2,DE2,DS3,DE3,&
                                       MS1,ME1,MS2,ME2,MS3,ME3,&
                                       PS1,PE1,PS2,PE2,PS3,PE3 )
       IMPLICIT NONE
       INTEGER                         DS1,DE1,DS2,DE2,DS3,DE3,&
                                       MS1,ME1,MS2,ME2,MS3,ME3,&
                                       PS1,PE1,PS2,PE2,PS3,PE3
       CHARACTER *(*) stagger,ordering
       INTEGER fid,domdesc
       LOGICAL globbuf(*)
       LOGICAL buf(*)

       CALL wrf_patch_to_global_generic (buf,globbuf,domdesc,stagger,ordering,4,&
                                         DS1,DE1,DS2,DE2,DS3,DE3,&
                                         MS1,ME1,MS2,ME2,MS3,ME3,&
                                         PS1,PE1,PS2,PE2,PS3,PE3 )

       RETURN
   END SUBROUTINE wrf_patch_to_global_logical



   SUBROUTINE wrf_patch_to_global_generic (buf,globbuf,domdesc,stagger,ordering,typesize,&
                                       DS1a,DE1a,DS2a,DE2a,DS3a,DE3a,&
                                       MS1a,ME1a,MS2a,ME2a,MS3a,ME3a,&
                                       PS1a,PE1a,PS2a,PE2a,PS3a,PE3a )
       USE module_driver_constants
       USE module_timing
       USE module_wrf_error, ONLY : wrf_at_debug_level
       USE module_dm, ONLY : local_communicator, ntasks

       IMPLICIT NONE
       INTEGER                         DS1a,DE1a,DS2a,DE2a,DS3a,DE3a,&
                                       MS1a,ME1a,MS2a,ME2a,MS3a,ME3a,&
                                       PS1a,PE1a,PS2a,PE2a,PS3a,PE3A
       CHARACTER *(*) stagger,ordering
       INTEGER domdesc,typesize,ierr
       REAL globbuf(*)
       REAL buf(*)

       INTEGER                         DS1,DE1,DS2,DE2,DS3,DE3,&
                                       MS1,ME1,MS2,ME2,MS3,ME3,&
                                       PS1,PE1,PS2,PE2,PS3,PE3
       INTEGER                         ids,ide,jds,jde,kds,kde,&
                                       ims,ime,jms,jme,kms,kme,&
                                       ips,ipe,jps,jpe,kps,kpe
       LOGICAL, EXTERNAL :: wrf_dm_on_monitor, has_char

       INTEGER i, j, k,  ndim
       INTEGER  Patch(3,2), Gpatch(3,2,ntasks)
    
       REAL, ALLOCATABLE :: tmpbuf( : )
       REAL locbuf( (PE1a-PS1a+1)*(PE2a-PS2a+1)*(PE3a-PS3a+1)/4*typesize+32 )

       DS1 = DS1a ; DE1 = DE1a ; DS2=DS2a ; DE2 = DE2a ; DS3 = DS3a ; DE3 = DE3a
       MS1 = MS1a ; ME1 = ME1a ; MS2=MS2a ; ME2 = ME2a ; MS3 = MS3a ; ME3 = ME3a
       PS1 = PS1a ; PE1 = PE1a ; PS2=PS2a ; PE2 = PE2a ; PS3 = PS3a ; PE3 = PE3a

       SELECT CASE ( TRIM(ordering) )
         CASE ( 'xy', 'yx' )
           ndim = 2
         CASE DEFAULT
           ndim = 3   
       END SELECT

       SELECT CASE ( TRIM(ordering) )
         CASE ( 'xyz','xy' )
            
            
            
           IF ( .NOT. has_char( stagger, 'x' ) ) DE1 = DE1+1
           IF ( .NOT. has_char( stagger, 'y' ) ) DE2 = DE2+1
           IF ( ndim .EQ. 3 .AND. .NOT. has_char( stagger, 'z' ) ) DE3 = DE3+1
         CASE ( 'yxz','yx' )
           IF ( .NOT. has_char( stagger, 'x' ) ) DE2 = DE2+1
           IF ( .NOT. has_char( stagger, 'y' ) ) DE1 = DE1+1
           IF ( ndim .EQ. 3 .AND. .NOT. has_char( stagger, 'z' ) ) DE3 = DE3+1
         CASE ( 'zxy' )
           IF ( .NOT. has_char( stagger, 'x' ) ) DE2 = DE2+1
           IF ( .NOT. has_char( stagger, 'y' ) ) DE3 = DE3+1
           IF ( ndim .EQ. 3 .AND. .NOT. has_char( stagger, 'z' ) ) DE1 = DE1+1
         CASE ( 'xzy' )
           IF ( .NOT. has_char( stagger, 'x' ) ) DE1 = DE1+1
           IF ( .NOT. has_char( stagger, 'y' ) ) DE3 = DE3+1
           IF ( ndim .EQ. 3 .AND. .NOT. has_char( stagger, 'z' ) ) DE2 = DE2+1
         CASE DEFAULT
       END SELECT

     
       IF ( wrf_dm_on_monitor() ) THEN
         ALLOCATE ( tmpbuf ( (DE1-DS1+1)*(DE2-DS2+1)*(DE3-DS3+1)/4*typesize+32 ), STAT=ierr )
       ELSE
         ALLOCATE ( tmpbuf ( 1 ), STAT=ierr )
       END IF
       IF ( ierr .ne. 0 ) CALL wrf_error_fatal3("<stdin>",7700,&
'allocating tmpbuf in wrf_patch_to_global_generic')

       Patch(1,1) = ps1 ; Patch(1,2) = pe1    
       Patch(2,1) = ps2 ; Patch(2,2) = pe2
       Patch(3,1) = ps3 ; Patch(3,2) = pe3

       IF      ( typesize .EQ. 4 ) THEN
         CALL just_patch_r ( buf , locbuf , size(locbuf)*4/typesize, &
                                   PS1, PE1, PS2, PE2, PS3, PE3 , &
                                   MS1, ME1, MS2, ME2, MS3, ME3   )
       ELSE IF ( typesize .EQ. 4 ) THEN
         CALL just_patch_i ( buf , locbuf , size(locbuf)*4/typesize, &
                                   PS1, PE1, PS2, PE2, PS3, PE3 , &
                                   MS1, ME1, MS2, ME2, MS3, ME3   )
       ELSE IF ( typesize .EQ. 8 ) THEN
         CALL just_patch_d ( buf , locbuf , size(locbuf)*4/typesize, &
                                   PS1, PE1, PS2, PE2, PS3, PE3 , &
                                   MS1, ME1, MS2, ME2, MS3, ME3   )
       ELSE IF ( typesize .EQ. 4 ) THEN
         CALL just_patch_l ( buf , locbuf , size(locbuf)*4/typesize, &
                                   PS1, PE1, PS2, PE2, PS3, PE3 , &
                                   MS1, ME1, MS2, ME2, MS3, ME3   )
       END IF


       CALL collect_on_comm0 (  local_communicator , 4 ,  &
                                Patch , 6 ,                       &
                                GPatch , 6*ntasks                 )

       CALL collect_on_comm0 (  local_communicator , typesize ,  &
                                locbuf , (pe1-ps1+1)*(pe2-ps2+1)*(pe3-ps3+1),   &
                                tmpbuf  , (de1-ds1+1)*(de2-ds2+1)*(de3-ds3+1) )

       ndim = len(TRIM(ordering))

       IF ( wrf_at_debug_level(500) ) THEN
         CALL start_timing
       END IF

       IF ( ndim .GE. 2 .AND. wrf_dm_on_monitor() ) THEN

         IF      ( typesize .EQ. 4 ) THEN
           CALL patch_2_outbuf_r ( tmpbuf  , globbuf ,             &
                                   DS1, DE1, DS2, DE2, DS3, DE3 , &
                                   GPATCH                         )
         ELSE IF ( typesize .EQ. 4 ) THEN
           CALL patch_2_outbuf_i ( tmpbuf  , globbuf ,             &
                                   DS1, DE1, DS2, DE2, DS3, DE3 , &
                                   GPATCH                         )
         ELSE IF ( typesize .EQ. 8 ) THEN
           CALL patch_2_outbuf_d ( tmpbuf  , globbuf ,             &
                                   DS1, DE1, DS2, DE2, DS3, DE3 , &
                                   GPATCH                         )
         ELSE IF ( typesize .EQ. 4 ) THEN
           CALL patch_2_outbuf_l ( tmpbuf  , globbuf ,             &
                                   DS1, DE1, DS2, DE2, DS3, DE3 , &
                                   GPATCH                         )
         END IF

       END IF

       IF ( wrf_at_debug_level(500) ) THEN
         CALL end_timing('wrf_patch_to_global_generic')
       END IF
       DEALLOCATE( tmpbuf )

       RETURN
    END SUBROUTINE wrf_patch_to_global_generic

  SUBROUTINE just_patch_i ( inbuf , outbuf, noutbuf,     &
                               PS1,PE1,PS2,PE2,PS3,PE3,  &
                               MS1,ME1,MS2,ME2,MS3,ME3   )
    IMPLICIT NONE
    INTEGER                         , INTENT(IN)  :: noutbuf
    INTEGER    , DIMENSION(noutbuf) , INTENT(OUT) :: outbuf
    INTEGER   MS1,ME1,MS2,ME2,MS3,ME3
    INTEGER   PS1,PE1,PS2,PE2,PS3,PE3
    INTEGER    , DIMENSION( MS1:ME1,MS2:ME2,MS3:ME3 ) , INTENT(IN) :: inbuf

    INTEGER               :: i,j,k,n   ,  icurs
    icurs = 1
      DO k = PS3, PE3
        DO j = PS2, PE2
          DO i = PS1, PE1
            outbuf( icurs )  = inbuf( i, j, k )
            icurs = icurs + 1
          END DO
        END DO
      END DO
    RETURN
  END SUBROUTINE just_patch_i

  SUBROUTINE just_patch_r ( inbuf , outbuf, noutbuf,     &
                               PS1,PE1,PS2,PE2,PS3,PE3,  &
                               MS1,ME1,MS2,ME2,MS3,ME3   )
    IMPLICIT NONE
    INTEGER                      , INTENT(IN)  :: noutbuf
    REAL    , DIMENSION(noutbuf) , INTENT(OUT) :: outbuf
    INTEGER   MS1,ME1,MS2,ME2,MS3,ME3
    INTEGER   PS1,PE1,PS2,PE2,PS3,PE3
    REAL    , DIMENSION( MS1:ME1,MS2:ME2,MS3:ME3 ) , INTENT(in) :: inbuf

    INTEGER               :: i,j,k   ,  icurs
    icurs = 1
      DO k = PS3, PE3
        DO j = PS2, PE2
          DO i = PS1, PE1
            outbuf( icurs )  = inbuf( i, j, k )
            icurs = icurs + 1
          END DO
        END DO
      END DO
    RETURN
  END SUBROUTINE just_patch_r

  SUBROUTINE just_patch_d ( inbuf , outbuf, noutbuf,     &
                               PS1,PE1,PS2,PE2,PS3,PE3,  &
                               MS1,ME1,MS2,ME2,MS3,ME3   )
    IMPLICIT NONE
    INTEGER                                  , INTENT(IN)  :: noutbuf
    DOUBLE PRECISION    , DIMENSION(noutbuf) , INTENT(OUT) :: outbuf
    INTEGER   MS1,ME1,MS2,ME2,MS3,ME3
    INTEGER   PS1,PE1,PS2,PE2,PS3,PE3
    DOUBLE PRECISION    , DIMENSION( MS1:ME1,MS2:ME2,MS3:ME3 ) , INTENT(in) :: inbuf

    INTEGER               :: i,j,k,n   ,  icurs
    icurs = 1
      DO k = PS3, PE3
        DO j = PS2, PE2
          DO i = PS1, PE1
            outbuf( icurs )  = inbuf( i, j, k )
            icurs = icurs + 1
          END DO
        END DO
      END DO
    RETURN
  END SUBROUTINE just_patch_d

  SUBROUTINE just_patch_l ( inbuf , outbuf, noutbuf,     &
                               PS1,PE1,PS2,PE2,PS3,PE3,  &
                               MS1,ME1,MS2,ME2,MS3,ME3   )
    IMPLICIT NONE
    INTEGER                         , INTENT(IN)  :: noutbuf
    LOGICAL    , DIMENSION(noutbuf) , INTENT(OUT) :: outbuf
    INTEGER   MS1,ME1,MS2,ME2,MS3,ME3
    INTEGER   PS1,PE1,PS2,PE2,PS3,PE3
    LOGICAL    , DIMENSION( MS1:ME1,MS2:ME2,MS3:ME3 ) , INTENT(in) :: inbuf

    INTEGER               :: i,j,k,n   ,  icurs
    icurs = 1
      DO k = PS3, PE3
        DO j = PS2, PE2
          DO i = PS1, PE1
            outbuf( icurs )  = inbuf( i, j, k )
            icurs = icurs + 1
          END DO
        END DO
      END DO
    RETURN
  END SUBROUTINE just_patch_l


  SUBROUTINE patch_2_outbuf_r( inbuf, outbuf,            &
                               DS1,DE1,DS2,DE2,DS3,DE3,  &
                               GPATCH )
    USE module_dm, ONLY : ntasks
    IMPLICIT NONE
    REAL    , DIMENSION(*) , INTENT(IN) :: inbuf
    INTEGER   DS1,DE1,DS2,DE2,DS3,DE3,GPATCH(3,2,ntasks)
    REAL    , DIMENSION( DS1:DE1,DS2:DE2,DS3:DE3 ) , INTENT(out) :: outbuf

    INTEGER               :: i,j,k,n   ,  icurs
    icurs = 1
    DO n = 1, ntasks
      DO k = GPATCH( 3,1,n ), GPATCH( 3,2,n )
        DO j = GPATCH( 2,1,n ), GPATCH( 2,2,n )
          DO i = GPATCH( 1,1,n ), GPATCH( 1,2,n )
            outbuf( i, j, k ) = inbuf( icurs )
            icurs = icurs + 1
          END DO
        END DO
      END DO
    END DO

    RETURN
  END SUBROUTINE patch_2_outbuf_r

  SUBROUTINE patch_2_outbuf_i( inbuf, outbuf,         &
                               DS1,DE1,DS2,DE2,DS3,DE3,&
                               GPATCH )
    USE module_dm, ONLY : ntasks
    IMPLICIT NONE
    INTEGER    , DIMENSION(*) , INTENT(IN) :: inbuf
    INTEGER   DS1,DE1,DS2,DE2,DS3,DE3,GPATCH(3,2,ntasks)
    INTEGER    , DIMENSION( DS1:DE1,DS2:DE2,DS3:DE3 ) , INTENT(out) :: outbuf

    INTEGER               :: i,j,k,n   ,  icurs
    icurs = 1
    DO n = 1, ntasks
      DO k = GPATCH( 3,1,n ), GPATCH( 3,2,n )
        DO j = GPATCH( 2,1,n ), GPATCH( 2,2,n )
          DO i = GPATCH( 1,1,n ), GPATCH( 1,2,n )
            outbuf( i, j, k ) = inbuf( icurs )
            icurs = icurs + 1
          END DO
        END DO
      END DO
    END DO
    RETURN
  END SUBROUTINE patch_2_outbuf_i

  SUBROUTINE patch_2_outbuf_d( inbuf, outbuf,         &
                               DS1,DE1,DS2,DE2,DS3,DE3,&
                               GPATCH )
    USE module_dm, ONLY : ntasks
    IMPLICIT NONE
    DOUBLE PRECISION    , DIMENSION(*) , INTENT(IN) :: inbuf
    INTEGER   DS1,DE1,DS2,DE2,DS3,DE3,GPATCH(3,2,ntasks)
    DOUBLE PRECISION    , DIMENSION( DS1:DE1,DS2:DE2,DS3:DE3 ) , INTENT(out) :: outbuf

    INTEGER               :: i,j,k,n   ,  icurs
    icurs = 1
    DO n = 1, ntasks
      DO k = GPATCH( 3,1,n ), GPATCH( 3,2,n )
        DO j = GPATCH( 2,1,n ), GPATCH( 2,2,n )
          DO i = GPATCH( 1,1,n ), GPATCH( 1,2,n )
            outbuf( i, j, k ) = inbuf( icurs )
            icurs = icurs + 1
          END DO
        END DO
      END DO
    END DO
    RETURN
  END SUBROUTINE patch_2_outbuf_d

  SUBROUTINE patch_2_outbuf_l( inbuf, outbuf,         &
                               DS1,DE1,DS2,DE2,DS3,DE3,&
                               GPATCH )
    USE module_dm, ONLY : ntasks
    IMPLICIT NONE
    LOGICAL    , DIMENSION(*) , INTENT(IN) :: inbuf
    INTEGER   DS1,DE1,DS2,DE2,DS3,DE3,GPATCH(3,2,ntasks)
    LOGICAL    , DIMENSION( DS1:DE1,DS2:DE2,DS3:DE3 ) , INTENT(out) :: outbuf

    INTEGER               :: i,j,k,n   ,  icurs
    icurs = 1
    DO n = 1, ntasks
      DO k = GPATCH( 3,1,n ), GPATCH( 3,2,n )
        DO j = GPATCH( 2,1,n ), GPATCH( 2,2,n )
          DO i = GPATCH( 1,1,n ), GPATCH( 1,2,n )
            outbuf( i, j, k ) = inbuf( icurs )
            icurs = icurs + 1
          END DO
        END DO
      END DO
    END DO
    RETURN
  END SUBROUTINE patch_2_outbuf_l



    SUBROUTINE wrf_global_to_patch_real (globbuf,buf,domdesc,stagger,ordering,&
                                       DS1,DE1,DS2,DE2,DS3,DE3,&
                                       MS1,ME1,MS2,ME2,MS3,ME3,&
                                       PS1,PE1,PS2,PE2,PS3,PE3 )
       IMPLICIT NONE
       INTEGER                         DS1,DE1,DS2,DE2,DS3,DE3,&
                                       MS1,ME1,MS2,ME2,MS3,ME3,&
                                       PS1,PE1,PS2,PE2,PS3,PE3
       CHARACTER *(*) stagger,ordering
       INTEGER fid,domdesc
       REAL globbuf(*)
       REAL buf(*)

       CALL wrf_global_to_patch_generic (globbuf,buf,domdesc,stagger,ordering,4,&
                                       DS1,DE1,DS2,DE2,DS3,DE3,&
                                       MS1,ME1,MS2,ME2,MS3,ME3,&
                                       PS1,PE1,PS2,PE2,PS3,PE3 )
       RETURN
    END SUBROUTINE wrf_global_to_patch_real

    SUBROUTINE wrf_global_to_patch_double (globbuf,buf,domdesc,stagger,ordering,&
                                       DS1,DE1,DS2,DE2,DS3,DE3,&
                                       MS1,ME1,MS2,ME2,MS3,ME3,&
                                       PS1,PE1,PS2,PE2,PS3,PE3 )
       IMPLICIT NONE
       INTEGER                         DS1,DE1,DS2,DE2,DS3,DE3,&
                                       MS1,ME1,MS2,ME2,MS3,ME3,&
                                       PS1,PE1,PS2,PE2,PS3,PE3
       CHARACTER *(*) stagger,ordering
       INTEGER fid,domdesc




       REAL globbuf(*)
       REAL buf(*)

       CALL wrf_global_to_patch_generic (globbuf,buf,domdesc,stagger,ordering,8,&
                                       DS1,DE1,DS2,DE2,DS3,DE3,&
                                       MS1,ME1,MS2,ME2,MS3,ME3,&
                                       PS1,PE1,PS2,PE2,PS3,PE3 )
       RETURN
    END SUBROUTINE wrf_global_to_patch_double


    SUBROUTINE wrf_global_to_patch_integer (globbuf,buf,domdesc,stagger,ordering,&
                                       DS1,DE1,DS2,DE2,DS3,DE3,&
                                       MS1,ME1,MS2,ME2,MS3,ME3,&
                                       PS1,PE1,PS2,PE2,PS3,PE3 )
       IMPLICIT NONE
       INTEGER                         DS1,DE1,DS2,DE2,DS3,DE3,&
                                       MS1,ME1,MS2,ME2,MS3,ME3,&
                                       PS1,PE1,PS2,PE2,PS3,PE3
       CHARACTER *(*) stagger,ordering
       INTEGER fid,domdesc
       INTEGER globbuf(*)
       INTEGER buf(*)

       CALL wrf_global_to_patch_generic (globbuf,buf,domdesc,stagger,ordering,4,&
                                       DS1,DE1,DS2,DE2,DS3,DE3,&
                                       MS1,ME1,MS2,ME2,MS3,ME3,&
                                       PS1,PE1,PS2,PE2,PS3,PE3 )
       RETURN
    END SUBROUTINE wrf_global_to_patch_integer

    SUBROUTINE wrf_global_to_patch_logical (globbuf,buf,domdesc,stagger,ordering,&
                                       DS1,DE1,DS2,DE2,DS3,DE3,&
                                       MS1,ME1,MS2,ME2,MS3,ME3,&
                                       PS1,PE1,PS2,PE2,PS3,PE3 )
       IMPLICIT NONE
       INTEGER                         DS1,DE1,DS2,DE2,DS3,DE3,&
                                       MS1,ME1,MS2,ME2,MS3,ME3,&
                                       PS1,PE1,PS2,PE2,PS3,PE3
       CHARACTER *(*) stagger,ordering
       INTEGER fid,domdesc
       LOGICAL globbuf(*)
       LOGICAL buf(*)

       CALL wrf_global_to_patch_generic (globbuf,buf,domdesc,stagger,ordering,4,&
                                       DS1,DE1,DS2,DE2,DS3,DE3,&
                                       MS1,ME1,MS2,ME2,MS3,ME3,&
                                       PS1,PE1,PS2,PE2,PS3,PE3 )
       RETURN
    END SUBROUTINE wrf_global_to_patch_logical

    SUBROUTINE wrf_global_to_patch_generic (globbuf,buf,domdesc,stagger,ordering,typesize,&
                                       DS1a,DE1a,DS2a,DE2a,DS3a,DE3a,&
                                       MS1a,ME1a,MS2a,ME2a,MS3a,ME3a,&
                                       PS1a,PE1a,PS2a,PE2a,PS3a,PE3a )
       USE module_dm, ONLY : local_communicator, ntasks
       USE module_driver_constants
       IMPLICIT NONE
       INTEGER                         DS1a,DE1a,DS2a,DE2a,DS3a,DE3a,&
                                       MS1a,ME1a,MS2a,ME2a,MS3a,ME3a,&
                                       PS1a,PE1a,PS2a,PE2a,PS3a,PE3A
       CHARACTER *(*) stagger,ordering
       INTEGER domdesc,typesize,ierr
       REAL globbuf(*)
       REAL buf(*)

       INTEGER                         DS1,DE1,DS2,DE2,DS3,DE3,&
                                       MS1,ME1,MS2,ME2,MS3,ME3,&
                                       PS1,PE1,PS2,PE2,PS3,PE3
       LOGICAL, EXTERNAL :: wrf_dm_on_monitor, has_char

       INTEGER i,j,k,ord,ord2d,ndim
       INTEGER  Patch(3,2), Gpatch(3,2,ntasks)
       REAL, ALLOCATABLE :: tmpbuf( : )
       REAL locbuf( (PE1a-PS1a+1)*(PE2a-PS2a+1)*(PE3a-PS3a+1)/4*typesize+32 )

       DS1 = DS1a ; DE1 = DE1a ; DS2=DS2a ; DE2 = DE2a ; DS3 = DS3a ; DE3 = DE3a
       MS1 = MS1a ; ME1 = ME1a ; MS2=MS2a ; ME2 = ME2a ; MS3 = MS3a ; ME3 = ME3a
       PS1 = PS1a ; PE1 = PE1a ; PS2=PS2a ; PE2 = PE2a ; PS3 = PS3a ; PE3 = PE3a

       SELECT CASE ( TRIM(ordering) )
         CASE ( 'xy', 'yx' )
           ndim = 2
         CASE DEFAULT
           ndim = 3   
       END SELECT

       SELECT CASE ( TRIM(ordering) )
         CASE ( 'xyz','xy' )
            
            
            
           IF ( .NOT. has_char( stagger, 'x' ) ) DE1 = DE1+1
           IF ( .NOT. has_char( stagger, 'y' ) ) DE2 = DE2+1
           IF ( ndim .EQ. 3 .AND. .NOT. has_char( stagger, 'z' ) ) DE3 = DE3+1
         CASE ( 'yxz','yx' )
           IF ( .NOT. has_char( stagger, 'x' ) ) DE2 = DE2+1
           IF ( .NOT. has_char( stagger, 'y' ) ) DE1 = DE1+1
           IF ( ndim .EQ. 3 .AND. .NOT. has_char( stagger, 'z' ) ) DE3 = DE3+1
         CASE ( 'zxy' )
           IF ( .NOT. has_char( stagger, 'x' ) ) DE2 = DE2+1
           IF ( .NOT. has_char( stagger, 'y' ) ) DE3 = DE3+1
           IF ( ndim .EQ. 3 .AND. .NOT. has_char( stagger, 'z' ) ) DE1 = DE1+1
         CASE ( 'xzy' )
           IF ( .NOT. has_char( stagger, 'x' ) ) DE1 = DE1+1
           IF ( .NOT. has_char( stagger, 'y' ) ) DE3 = DE3+1
           IF ( ndim .EQ. 3 .AND. .NOT. has_char( stagger, 'z' ) ) DE2 = DE2+1
         CASE DEFAULT
       END SELECT

     
       IF ( wrf_dm_on_monitor() ) THEN
         ALLOCATE ( tmpbuf ( (DE1-DS1+1)*(DE2-DS2+1)*(DE3-DS3+1)/4*typesize+32 ), STAT=ierr )
       ELSE
         ALLOCATE ( tmpbuf ( 1 ), STAT=ierr )
       END IF
       IF ( ierr .ne. 0 ) CALL wrf_error_fatal3("<stdin>",8112,&
'allocating tmpbuf in wrf_global_to_patch_generic')

       Patch(1,1) = ps1 ; Patch(1,2) = pe1    
       Patch(2,1) = ps2 ; Patch(2,2) = pe2
       Patch(3,1) = ps3 ; Patch(3,2) = pe3


       CALL collect_on_comm0 (  local_communicator , 4 ,  &
                                Patch , 6 ,                       &
                                GPatch , 6*ntasks                 )
       ndim = len(TRIM(ordering))

       IF ( wrf_dm_on_monitor() .AND. ndim .GE. 2 ) THEN
         IF      ( typesize .EQ. 4 ) THEN
           CALL outbuf_2_patch_r ( globbuf , tmpbuf  ,    &
                                   DS1, DE1, DS2, DE2, DS3, DE3 , &
                                   MS1, ME1, MS2, ME2, MS3, ME3 , &
                                   GPATCH                         )
         ELSE IF ( typesize .EQ. 4 ) THEN
           CALL outbuf_2_patch_i ( globbuf , tmpbuf  ,    &
                                   DS1, DE1, DS2, DE2, DS3, DE3 , &
                                   GPATCH                         )
         ELSE IF ( typesize .EQ. 8 ) THEN
           CALL outbuf_2_patch_d ( globbuf , tmpbuf  ,    &
                                   DS1, DE1, DS2, DE2, DS3, DE3 , &
                                   GPATCH                         )
         ELSE IF ( typesize .EQ. 4 ) THEN
           CALL outbuf_2_patch_l ( globbuf , tmpbuf  ,    &
                                   DS1, DE1, DS2, DE2, DS3, DE3 , &
                                   GPATCH                         )
         END IF
       END IF

       CALL dist_on_comm0 (  local_communicator , typesize ,  &
                             tmpbuf  , (de1-ds1+1)*(de2-ds2+1)*(de3-ds3+1) , &
                             locbuf    , (pe1-ps1+1)*(pe2-ps2+1)*(pe3-ps3+1) )

       IF      ( typesize .EQ. 4 ) THEN
         CALL all_sub_r ( locbuf , buf ,             &
                                   PS1, PE1, PS2, PE2, PS3, PE3 , &
                                   MS1, ME1, MS2, ME2, MS3, ME3   )

       ELSE IF ( typesize .EQ. 4 ) THEN
         CALL all_sub_i ( locbuf , buf ,             &
                                   PS1, PE1, PS2, PE2, PS3, PE3 , &
                                   MS1, ME1, MS2, ME2, MS3, ME3   )
       ELSE IF ( typesize .EQ. 8 ) THEN
         CALL all_sub_d ( locbuf , buf ,             &
                                   PS1, PE1, PS2, PE2, PS3, PE3 , &
                                   MS1, ME1, MS2, ME2, MS3, ME3   )
       ELSE IF ( typesize .EQ. 4 ) THEN
         CALL all_sub_l ( locbuf , buf ,             &
                                   PS1, PE1, PS2, PE2, PS3, PE3 , &
                                   MS1, ME1, MS2, ME2, MS3, ME3   )
       END IF


       DEALLOCATE ( tmpbuf )

       RETURN
    END SUBROUTINE wrf_global_to_patch_generic

  SUBROUTINE all_sub_i ( inbuf , outbuf,              &
                               PS1,PE1,PS2,PE2,PS3,PE3,  &
                               MS1,ME1,MS2,ME2,MS3,ME3   )
    IMPLICIT NONE
    INTEGER    , DIMENSION(*) , INTENT(IN) :: inbuf
    INTEGER   MS1,ME1,MS2,ME2,MS3,ME3
    INTEGER   PS1,PE1,PS2,PE2,PS3,PE3
    INTEGER    , DIMENSION( MS1:ME1,MS2:ME2,MS3:ME3 ) , INTENT(OUT) :: outbuf

    INTEGER               :: i,j,k,n   ,  icurs
    icurs = 1
      DO k = PS3, PE3
        DO j = PS2, PE2
          DO i = PS1, PE1
            outbuf( i, j, k )  = inbuf ( icurs )
            icurs = icurs + 1
          END DO
        END DO
      END DO
    RETURN
  END SUBROUTINE all_sub_i

  SUBROUTINE all_sub_r ( inbuf , outbuf,              &
                               PS1,PE1,PS2,PE2,PS3,PE3,  &
                               MS1,ME1,MS2,ME2,MS3,ME3   )
    IMPLICIT NONE
    REAL       , DIMENSION(*) , INTENT(IN) :: inbuf
    INTEGER   MS1,ME1,MS2,ME2,MS3,ME3
    INTEGER   PS1,PE1,PS2,PE2,PS3,PE3
    REAL       , DIMENSION( MS1:ME1,MS2:ME2,MS3:ME3 ) , INTENT(OUT) :: outbuf

    INTEGER               :: i,j,k,n   ,  icurs
    icurs = 1
      DO k = PS3, PE3
        DO j = PS2, PE2
          DO i = PS1, PE1
            outbuf( i, j, k )  = inbuf ( icurs )
            icurs = icurs + 1
          END DO
        END DO
      END DO

    RETURN
  END SUBROUTINE all_sub_r

  SUBROUTINE all_sub_d ( inbuf , outbuf,              &
                               PS1,PE1,PS2,PE2,PS3,PE3,  &
                               MS1,ME1,MS2,ME2,MS3,ME3   )
    IMPLICIT NONE
    DOUBLE PRECISION    , DIMENSION(*) , INTENT(IN) :: inbuf
    INTEGER   MS1,ME1,MS2,ME2,MS3,ME3
    INTEGER   PS1,PE1,PS2,PE2,PS3,PE3
    DOUBLE PRECISION    , DIMENSION( MS1:ME1,MS2:ME2,MS3:ME3 ) , INTENT(OUT) :: outbuf

    INTEGER               :: i,j,k,n   ,  icurs
    icurs = 1
      DO k = PS3, PE3
        DO j = PS2, PE2
          DO i = PS1, PE1
            outbuf( i, j, k )  = inbuf ( icurs )
            icurs = icurs + 1
          END DO
        END DO
      END DO
    RETURN
  END SUBROUTINE all_sub_d

  SUBROUTINE all_sub_l ( inbuf , outbuf,              &
                               PS1,PE1,PS2,PE2,PS3,PE3,  &
                               MS1,ME1,MS2,ME2,MS3,ME3   )
    IMPLICIT NONE
    LOGICAL    , DIMENSION(*) , INTENT(IN) :: inbuf
    INTEGER   MS1,ME1,MS2,ME2,MS3,ME3
    INTEGER   PS1,PE1,PS2,PE2,PS3,PE3
    LOGICAL    , DIMENSION( MS1:ME1,MS2:ME2,MS3:ME3 ) , INTENT(OUT) :: outbuf

    INTEGER               :: i,j,k,n   ,  icurs
    icurs = 1
      DO k = PS3, PE3
        DO j = PS2, PE2
          DO i = PS1, PE1
            outbuf( i, j, k )  = inbuf ( icurs )
            icurs = icurs + 1
          END DO
        END DO
      END DO
    RETURN
  END SUBROUTINE all_sub_l

  SUBROUTINE outbuf_2_patch_r( inbuf, outbuf,         &
                               DS1,DE1,DS2,DE2,DS3,DE3, &
                               MS1, ME1, MS2, ME2, MS3, ME3 , &
                               GPATCH )
    USE module_dm, ONLY : ntasks
    IMPLICIT NONE
    REAL    , DIMENSION(*) , INTENT(OUT) :: outbuf
    INTEGER   DS1,DE1,DS2,DE2,DS3,DE3,GPATCH(3,2,ntasks)
    INTEGER   MS1,ME1,MS2,ME2,MS3,ME3
    REAL    , DIMENSION( DS1:DE1,DS2:DE2,DS3:DE3 ) , INTENT(IN) :: inbuf

    INTEGER               :: i,j,k,n   ,  icurs

    icurs = 1
    DO n = 1, ntasks
      DO k = GPATCH( 3,1,n ), GPATCH( 3,2,n )
        DO j = GPATCH( 2,1,n ), GPATCH( 2,2,n )
          DO i = GPATCH( 1,1,n ), GPATCH( 1,2,n )
            outbuf( icurs ) = inbuf( i,j,k )
            icurs = icurs + 1
          END DO
        END DO
      END DO
    END DO
    RETURN
  END SUBROUTINE outbuf_2_patch_r

  SUBROUTINE outbuf_2_patch_i( inbuf, outbuf,         &
                               DS1,DE1,DS2,DE2,DS3,DE3,&
                               GPATCH )
    USE module_dm, ONLY : ntasks
    IMPLICIT NONE
    INTEGER    , DIMENSION(*) , INTENT(OUT) :: outbuf
    INTEGER   DS1,DE1,DS2,DE2,DS3,DE3,GPATCH(3,2,ntasks)
    INTEGER    , DIMENSION( DS1:DE1,DS2:DE2,DS3:DE3 ) , INTENT(IN) :: inbuf

    INTEGER               :: i,j,k,n   ,  icurs
    icurs = 1
    DO n = 1, ntasks
      DO k = GPATCH( 3,1,n ), GPATCH( 3,2,n )
        DO j = GPATCH( 2,1,n ), GPATCH( 2,2,n )
          DO i = GPATCH( 1,1,n ), GPATCH( 1,2,n )
            outbuf( icurs ) = inbuf( i,j,k )
            icurs = icurs + 1
          END DO
        END DO
      END DO
    END DO
    RETURN
  END SUBROUTINE outbuf_2_patch_i

  SUBROUTINE outbuf_2_patch_d( inbuf, outbuf,         &
                               DS1,DE1,DS2,DE2,DS3,DE3,&
                               GPATCH )
    USE module_dm, ONLY : ntasks
    IMPLICIT NONE
    DOUBLE PRECISION    , DIMENSION(*) , INTENT(OUT) :: outbuf
    INTEGER   DS1,DE1,DS2,DE2,DS3,DE3,GPATCH(3,2,ntasks)
    DOUBLE PRECISION    , DIMENSION( DS1:DE1,DS2:DE2,DS3:DE3 ) , INTENT(IN) :: inbuf

    INTEGER               :: i,j,k,n   ,  icurs
    icurs = 1
    DO n = 1, ntasks
      DO k = GPATCH( 3,1,n ), GPATCH( 3,2,n )
        DO j = GPATCH( 2,1,n ), GPATCH( 2,2,n )
          DO i = GPATCH( 1,1,n ), GPATCH( 1,2,n )
            outbuf( icurs ) = inbuf( i,j,k )
            icurs = icurs + 1
          END DO
        END DO
      END DO
    END DO
    RETURN
  END SUBROUTINE outbuf_2_patch_d

  SUBROUTINE outbuf_2_patch_l( inbuf, outbuf,         &
                               DS1,DE1,DS2,DE2,DS3,DE3,&
                               GPATCH )
    USE module_dm, ONLY : ntasks
    IMPLICIT NONE
    LOGICAL    , DIMENSION(*) , INTENT(OUT) :: outbuf
    INTEGER   DS1,DE1,DS2,DE2,DS3,DE3,GPATCH(3,2,ntasks)
    LOGICAL    , DIMENSION( DS1:DE1,DS2:DE2,DS3:DE3 ) , INTENT(IN) :: inbuf

    INTEGER               :: i,j,k,n   ,  icurs
    icurs = 1
    DO n = 1, ntasks
      DO k = GPATCH( 3,1,n ), GPATCH( 3,2,n )
        DO j = GPATCH( 2,1,n ), GPATCH( 2,2,n )
          DO i = GPATCH( 1,1,n ), GPATCH( 1,2,n )
            outbuf( icurs ) = inbuf( i,j,k )
            icurs = icurs + 1
          END DO
        END DO
      END DO
    END DO
    RETURN
  END SUBROUTINE outbuf_2_patch_l


  SUBROUTINE wrf_dm_nestexchange_init
      CALL rsl_lite_nesting_reset
  END SUBROUTINE wrf_dm_nestexchange_init


   SUBROUTINE wrf_gatherv_real (Field, field_ofst,            &
                                my_count ,                    &    
                                globbuf, glob_ofst ,          &    
                                counts                      , &    
                                displs                      , &    
                                root                        , &    
                                communicator                , &    
                                ierr )
   USE module_dm, ONLY : getrealmpitype
   IMPLICIT NONE
   INTEGER field_ofst, glob_ofst
   INTEGER my_count, communicator, root, ierr
   INTEGER , DIMENSION(*) :: counts, displs
   REAL, DIMENSION(*) :: Field, globbuf

   INCLUDE 'mpif.h'

           CALL mpi_gatherv( Field( field_ofst ),      &    
                            my_count ,                       &    
                            getrealmpitype() ,               &    
                            globbuf( glob_ofst ) ,                 &    
                            counts                         , &    
                            displs                         , &    
                            getrealmpitype()               , &    
                            root                           , &    
                            communicator                   , &    
                            ierr )


   END SUBROUTINE wrf_gatherv_real

   SUBROUTINE wrf_gatherv_double (Field, field_ofst,            &
                                my_count ,                    &    
                                globbuf, glob_ofst ,          &    
                                counts                      , &    
                                displs                      , &    
                                root                        , &    
                                communicator                , &    
                                ierr )

   IMPLICIT NONE
   INTEGER field_ofst, glob_ofst
   INTEGER my_count, communicator, root, ierr
   INTEGER , DIMENSION(*) :: counts, displs




   REAL, DIMENSION(*) :: Field, globbuf

   INCLUDE 'mpif.h'

           CALL mpi_gatherv( Field( field_ofst ),      &    
                            my_count ,                       &    
                            MPI_DOUBLE_PRECISION         ,               &    
                            globbuf( glob_ofst ) ,                 &    
                            counts                         , &    
                            displs                         , &    
                            MPI_DOUBLE_PRECISION                       , &    
                            root                           , &    
                            communicator                   , &    
                            ierr )


   END SUBROUTINE wrf_gatherv_double

   SUBROUTINE wrf_gatherv_integer (Field, field_ofst,            &
                                my_count ,                    &    
                                globbuf, glob_ofst ,          &    
                                counts                      , &    
                                displs                      , &    
                                root                        , &    
                                communicator                , &    
                                ierr )
   IMPLICIT NONE
   INTEGER field_ofst, glob_ofst
   INTEGER my_count, communicator, root, ierr
   INTEGER , DIMENSION(*) :: counts, displs
   INTEGER, DIMENSION(*) :: Field, globbuf

   INCLUDE 'mpif.h'

           CALL mpi_gatherv( Field( field_ofst ),      &    
                            my_count ,                       &    
                            MPI_INTEGER         ,               &    
                            globbuf( glob_ofst ) ,                 &    
                            counts                         , &    
                            displs                         , &    
                            MPI_INTEGER                       , &    
                            root                           , &    
                            communicator                   , &    
                            ierr )


   END SUBROUTINE wrf_gatherv_integer


   SUBROUTINE wrf_scatterv_real (                             &
                                globbuf, glob_ofst ,          &    
                                counts                      , &    
                                Field, field_ofst,            &
                                my_count ,                    &    
                                displs                      , &    
                                root                        , &    
                                communicator                , &    
                                ierr )
   USE module_dm, ONLY : getrealmpitype
   IMPLICIT NONE
   INTEGER field_ofst, glob_ofst
   INTEGER my_count, communicator, root, ierr
   INTEGER , DIMENSION(*) :: counts, displs
   REAL, DIMENSION(*) :: Field, globbuf

   INCLUDE 'mpif.h'

           CALL mpi_scatterv(                                &
                            globbuf( glob_ofst ) ,           &    
                            counts                         , &    
                            displs                         , &    
                            getrealmpitype()               , &    
                            Field( field_ofst ),             &    
                            my_count ,                       &    
                            getrealmpitype() ,               &    
                            root                           , &    
                            communicator                   , &    
                            ierr )


   END SUBROUTINE wrf_scatterv_real

   SUBROUTINE wrf_scatterv_double (                           &
                                globbuf, glob_ofst ,          &    
                                counts                      , &    
                                Field, field_ofst,            &
                                my_count ,                    &    
                                displs                      , &    
                                root                        , &    
                                communicator                , &    
                                ierr )
   IMPLICIT NONE
   INTEGER field_ofst, glob_ofst
   INTEGER my_count, communicator, root, ierr
   INTEGER , DIMENSION(*) :: counts, displs
   REAL, DIMENSION(*) :: Field, globbuf

   INCLUDE 'mpif.h'





           CALL mpi_scatterv(                                &
                            globbuf( glob_ofst ) ,           &    
                            counts                         , &    
                            displs                         , &    
                            MPI_DOUBLE_PRECISION           , &    
                            Field( field_ofst ),             &    
                            my_count ,                       &    
                            MPI_DOUBLE_PRECISION         ,   &    
                            root                           , &    
                            communicator                   , &    
                            ierr )


   END SUBROUTINE wrf_scatterv_double

   SUBROUTINE wrf_scatterv_integer (                          &
                                globbuf, glob_ofst ,          &    
                                counts                      , &    
                                Field, field_ofst,            &
                                my_count ,                    &    
                                displs                      , &    
                                root                        , &    
                                communicator                , &    
                                ierr )
   IMPLICIT NONE
   INTEGER field_ofst, glob_ofst
   INTEGER my_count, communicator, root, ierr
   INTEGER , DIMENSION(*) :: counts, displs
   INTEGER, DIMENSION(*) :: Field, globbuf

   INCLUDE 'mpif.h'

           CALL mpi_scatterv(                                &
                            globbuf( glob_ofst ) ,           &    
                            counts                         , &    
                            displs                         , &    
                            MPI_INTEGER                    , &    
                            Field( field_ofst ),             &    
                            my_count ,                       &    
                            MPI_INTEGER         ,            &    
                            root                           , &    
                            communicator                   , &    
                            ierr )


   END SUBROUTINE wrf_scatterv_integer


     SUBROUTINE wrf_dm_gatherv ( v, elemsize , km_s, km_e, wordsz )
      IMPLICIT NONE
      INTEGER  elemsize, km_s, km_e, wordsz
      REAL v(*)
      IF ( wordsz .EQ. 8 ) THEN
         CALL wrf_dm_gatherv_double(v, elemsize , km_s, km_e)
      ELSE
         CALL wrf_dm_gatherv_single(v, elemsize , km_s, km_e)
      END IF
     END SUBROUTINE wrf_dm_gatherv

     SUBROUTINE wrf_dm_gatherv_double ( v, elemsize , km_s, km_e )
      IMPLICIT NONE
      INTEGER  elemsize, km_s, km_e
      REAL*8 v(0:*)

      REAL*8 v_local((km_e-km_s+1)*elemsize)

      INTEGER, DIMENSION(:), ALLOCATABLE :: recvcounts, displs
      INTEGER send_type, myproc, nproc, local_comm, ierr, i
   INCLUDE 'mpif.h'
      send_type = MPI_DOUBLE_PRECISION
      CALL wrf_get_dm_communicator ( local_comm )
      CALL wrf_get_nproc( nproc )
      CALL wrf_get_myproc( myproc )
      ALLOCATE( recvcounts(nproc), displs(nproc) )
      i = (km_e-km_s+1)*elemsize
      CALL mpi_allgather( i,1,MPI_INTEGER,recvcounts,1,MPI_INTEGER,local_comm,ierr) ;
      i = (km_s)*elemsize
      CALL mpi_allgather( i,1,MPI_INTEGER,displs,1,MPI_INTEGER,local_comm,ierr) ;

      DO i = 1,elemsize*(km_e-km_s+1)
        v_local(i) = v(i+elemsize*km_s-1)
      END DO
      CALL mpi_allgatherv( v_local,                                       &

                           (km_e-km_s+1)*elemsize,                        &
                           send_type,                                     &
                           v,                                             &
                           recvcounts,                                    &
                           displs,                                        &
                           send_type,                                     &
                           local_comm,                                    &
                           ierr )
      DEALLOCATE(recvcounts)
      DEALLOCATE(displs)

      return
     END SUBROUTINE wrf_dm_gatherv_double

     SUBROUTINE wrf_dm_gatherv_single ( v, elemsize , km_s, km_e )
      IMPLICIT NONE
      INTEGER  elemsize, km_s, km_e
      REAL*4 v(0:*)

      REAL*4 v_local((km_e-km_s+1)*elemsize)

      INTEGER, DIMENSION(:), ALLOCATABLE :: recvcounts, displs
      INTEGER send_type, myproc, nproc, local_comm, ierr, i
   INCLUDE 'mpif.h'
      send_type = MPI_REAL
      CALL wrf_get_dm_communicator ( local_comm )
      CALL wrf_get_nproc( nproc )
      CALL wrf_get_myproc( myproc )
      ALLOCATE( recvcounts(nproc), displs(nproc) )
      i = (km_e-km_s+1)*elemsize
      CALL mpi_allgather( i,1,MPI_INTEGER,recvcounts,1,MPI_INTEGER,local_comm,ierr) ;
      i = (km_s)*elemsize
      CALL mpi_allgather( i,1,MPI_INTEGER,displs,1,MPI_INTEGER,local_comm,ierr) ;

      DO i = 1,elemsize*(km_e-km_s+1)
        v_local(i) = v(i+elemsize*km_s-1)
      END DO
      CALL mpi_allgatherv( v_local,                                       &

                           (km_e-km_s+1)*elemsize,                        &
                           send_type,                                     &
                           v,                                             &
                           recvcounts,                                    &
                           displs,                                        &
                           send_type,                                     &
                           local_comm,                                    &
                           ierr )
      DEALLOCATE(recvcounts)
      DEALLOCATE(displs)

      return
     END SUBROUTINE wrf_dm_gatherv_single

      SUBROUTINE wrf_dm_decomp1d( nt, km_s, km_e )
       IMPLICIT NONE
       INTEGER, INTENT(IN)  :: nt
       INTEGER, INTENT(OUT) :: km_s, km_e
     
       INTEGER nn, nnp,  na, nb
       INTEGER myproc, nproc

       CALL wrf_get_myproc(myproc)
       CALL wrf_get_nproc(nproc)
       nn = nt / nproc           
       nnp = nn
       if ( myproc .lt. mod( nt, nproc ) )   nnp = nnp + 1 

       na = min( myproc, mod(nt,nproc) ) 
       nb = max( 0, myproc - na )        
       km_s = na * ( nn+1) + nb * nn     
       km_e = km_s + nnp - 1             
      END SUBROUTINE wrf_dm_decomp1d


SUBROUTINE wrf_dm_define_comms ( grid )
   USE module_domain, ONLY : domain
   IMPLICIT NONE
   TYPE(domain) , INTENT (INOUT) :: grid
   RETURN
END SUBROUTINE wrf_dm_define_comms

SUBROUTINE tfp_message( fname, lno )
   CHARACTER*(*) fname
   INTEGER lno
   CHARACTER*1024 mess

   WRITE(mess,*)'tfp_message: ',trim(fname),lno
   CALL wrf_message(mess)

     CALL wrf_error_fatal3("<stdin>",8693,&
mess)

END SUBROUTINE tfp_message

   SUBROUTINE set_dm_debug
    USE module_dm, ONLY : dm_debug_flag
    IMPLICIT NONE
    dm_debug_flag = .TRUE.
   END SUBROUTINE set_dm_debug
   SUBROUTINE reset_dm_debug
    USE module_dm, ONLY : dm_debug_flag
    IMPLICIT NONE
    dm_debug_flag = .FALSE.
   END SUBROUTINE reset_dm_debug
   SUBROUTINE get_dm_debug ( arg )
    USE module_dm, ONLY : dm_debug_flag
    IMPLICIT NONE
    LOGICAL arg
    arg = dm_debug_flag
   END SUBROUTINE get_dm_debug



