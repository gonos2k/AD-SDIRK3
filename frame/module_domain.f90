
























MODULE module_domain

   USE module_driver_constants
   USE module_machine
   USE module_configure
   USE module_wrf_error
   USE module_utility
   USE module_domain_type

   
   
   
   

   
   
   
   
   

   TYPE(domain) , POINTER :: head_grid , new_grid , next_grid , old_grid

   
   
   
   

   TYPE domain_levels
      TYPE(domain) , POINTER                              :: first_domain
   END TYPE domain_levels

   TYPE(domain_levels) , DIMENSION(max_levels)            :: head_for_each_level

   
   TYPE(domain), POINTER :: current_grid
   LOGICAL, SAVE :: current_grid_set = .FALSE.

   
   PRIVATE domain_time_test_print
   PRIVATE test_adjust_io_timestr

   INTERFACE get_ijk_from_grid
     MODULE PROCEDURE get_ijk_from_grid1, get_ijk_from_grid2
   END INTERFACE

   INTEGER, PARAMETER :: max_hst_mods = 1000

CONTAINS

   SUBROUTINE adjust_domain_dims_for_move( grid , dx, dy )
    IMPLICIT NONE

    TYPE( domain ), POINTER   :: grid
    INTEGER, INTENT(IN) ::  dx, dy

    data_ordering : SELECT CASE ( model_data_order )
       CASE  ( DATA_ORDER_XYZ )
            grid%sm31  = grid%sm31 + dx
            grid%em31  = grid%em31 + dx
            grid%sm32  = grid%sm32 + dy
            grid%em32  = grid%em32 + dy
            grid%sp31  = grid%sp31 + dx
            grid%ep31  = grid%ep31 + dx
            grid%sp32  = grid%sp32 + dy
            grid%ep32  = grid%ep32 + dy
            grid%sd31  = grid%sd31 + dx
            grid%ed31  = grid%ed31 + dx
            grid%sd32  = grid%sd32 + dy
            grid%ed32  = grid%ed32 + dy

       CASE  ( DATA_ORDER_YXZ )
            grid%sm31  = grid%sm31 + dy
            grid%em31  = grid%em31 + dy
            grid%sm32  = grid%sm32 + dx
            grid%em32  = grid%em32 + dx
            grid%sp31  = grid%sp31 + dy
            grid%ep31  = grid%ep31 + dy
            grid%sp32  = grid%sp32 + dx
            grid%ep32  = grid%ep32 + dx
            grid%sd31  = grid%sd31 + dy
            grid%ed31  = grid%ed31 + dy
            grid%sd32  = grid%sd32 + dx
            grid%ed32  = grid%ed32 + dx

       CASE  ( DATA_ORDER_ZXY )
            grid%sm32  = grid%sm32 + dx
            grid%em32  = grid%em32 + dx
            grid%sm33  = grid%sm33 + dy
            grid%em33  = grid%em33 + dy
            grid%sp32  = grid%sp32 + dx
            grid%ep32  = grid%ep32 + dx
            grid%sp33  = grid%sp33 + dy
            grid%ep33  = grid%ep33 + dy
            grid%sd32  = grid%sd32 + dx
            grid%ed32  = grid%ed32 + dx
            grid%sd33  = grid%sd33 + dy
            grid%ed33  = grid%ed33 + dy

       CASE  ( DATA_ORDER_ZYX )
            grid%sm32  = grid%sm32 + dy
            grid%em32  = grid%em32 + dy
            grid%sm33  = grid%sm33 + dx
            grid%em33  = grid%em33 + dx
            grid%sp32  = grid%sp32 + dy
            grid%ep32  = grid%ep32 + dy
            grid%sp33  = grid%sp33 + dx
            grid%ep33  = grid%ep33 + dx
            grid%sd32  = grid%sd32 + dy
            grid%ed32  = grid%ed32 + dy
            grid%sd33  = grid%sd33 + dx
            grid%ed33  = grid%ed33 + dx

       CASE  ( DATA_ORDER_XZY )
            grid%sm31  = grid%sm31 + dx
            grid%em31  = grid%em31 + dx
            grid%sm33  = grid%sm33 + dy
            grid%em33  = grid%em33 + dy
            grid%sp31  = grid%sp31 + dx
            grid%ep31  = grid%ep31 + dx
            grid%sp33  = grid%sp33 + dy
            grid%ep33  = grid%ep33 + dy
            grid%sd31  = grid%sd31 + dx
            grid%ed31  = grid%ed31 + dx
            grid%sd33  = grid%sd33 + dy
            grid%ed33  = grid%ed33 + dy

       CASE  ( DATA_ORDER_YZX )
            grid%sm31  = grid%sm31 + dy
            grid%em31  = grid%em31 + dy
            grid%sm33  = grid%sm33 + dx
            grid%em33  = grid%em33 + dx
            grid%sp31  = grid%sp31 + dy
            grid%ep31  = grid%ep31 + dy
            grid%sp33  = grid%sp33 + dx
            grid%ep33  = grid%ep33 + dx
            grid%sd31  = grid%sd31 + dy
            grid%ed31  = grid%ed31 + dy
            grid%sd33  = grid%sd33 + dx
            grid%ed33  = grid%ed33 + dx

    END SELECT data_ordering



    RETURN
   END SUBROUTINE adjust_domain_dims_for_move


   SUBROUTINE get_ijk_from_grid1 (  grid ,                   &
                           ids, ide, jds, jde, kds, kde,    &
                           ims, ime, jms, jme, kms, kme,    &
                           ips, ipe, jps, jpe, kps, kpe,    &
                           imsx, imex, jmsx, jmex, kmsx, kmex,    &
                           ipsx, ipex, jpsx, jpex, kpsx, kpex,    &
                           imsy, imey, jmsy, jmey, kmsy, kmey,    &
                           ipsy, ipey, jpsy, jpey, kpsy, kpey )
    IMPLICIT NONE
    TYPE( domain ), INTENT (IN)  :: grid
    INTEGER, INTENT(OUT) ::                                 &
                           ids, ide, jds, jde, kds, kde,    &
                           ims, ime, jms, jme, kms, kme,    &
                           ips, ipe, jps, jpe, kps, kpe,    &
                           imsx, imex, jmsx, jmex, kmsx, kmex,    &
                           ipsx, ipex, jpsx, jpex, kpsx, kpex,    &
                           imsy, imey, jmsy, jmey, kmsy, kmey,    &
                           ipsy, ipey, jpsy, jpey, kpsy, kpey

     CALL get_ijk_from_grid2 (  grid ,                   &
                           ids, ide, jds, jde, kds, kde,    &
                           ims, ime, jms, jme, kms, kme,    &
                           ips, ipe, jps, jpe, kps, kpe )
     data_ordering : SELECT CASE ( model_data_order )
       CASE  ( DATA_ORDER_XYZ )
           imsx = grid%sm31x ; imex = grid%em31x ; jmsx = grid%sm32x ; jmex = grid%em32x ; kmsx = grid%sm33x ; kmex = grid%em33x ;
           ipsx = grid%sp31x ; ipex = grid%ep31x ; jpsx = grid%sp32x ; jpex = grid%ep32x ; kpsx = grid%sp33x ; kpex = grid%ep33x ;
           imsy = grid%sm31y ; imey = grid%em31y ; jmsy = grid%sm32y ; jmey = grid%em32y ; kmsy = grid%sm33y ; kmey = grid%em33y ;
           ipsy = grid%sp31y ; ipey = grid%ep31y ; jpsy = grid%sp32y ; jpey = grid%ep32y ; kpsy = grid%sp33y ; kpey = grid%ep33y ;
       CASE  ( DATA_ORDER_YXZ )
           imsx = grid%sm32x ; imex = grid%em32x ; jmsx = grid%sm31x ; jmex = grid%em31x ; kmsx = grid%sm33x ; kmex = grid%em33x ;
           ipsx = grid%sp32x ; ipex = grid%ep32x ; jpsx = grid%sp31x ; jpex = grid%ep31x ; kpsx = grid%sp33x ; kpex = grid%ep33x ;
           imsy = grid%sm32y ; imey = grid%em32y ; jmsy = grid%sm31y ; jmey = grid%em31y ; kmsy = grid%sm33y ; kmey = grid%em33y ;
           ipsy = grid%sp32y ; ipey = grid%ep32y ; jpsy = grid%sp31y ; jpey = grid%ep31y ; kpsy = grid%sp33y ; kpey = grid%ep33y ;
       CASE  ( DATA_ORDER_ZXY )
           imsx = grid%sm32x ; imex = grid%em32x ; jmsx = grid%sm33x ; jmex = grid%em33x ; kmsx = grid%sm31x ; kmex = grid%em31x ;
           ipsx = grid%sp32x ; ipex = grid%ep32x ; jpsx = grid%sp33x ; jpex = grid%ep33x ; kpsx = grid%sp31x ; kpex = grid%ep31x ;
           imsy = grid%sm32y ; imey = grid%em32y ; jmsy = grid%sm33y ; jmey = grid%em33y ; kmsy = grid%sm31y ; kmey = grid%em31y ;
           ipsy = grid%sp32y ; ipey = grid%ep32y ; jpsy = grid%sp33y ; jpey = grid%ep33y ; kpsy = grid%sp31y ; kpey = grid%ep31y ;
       CASE  ( DATA_ORDER_ZYX )
           imsx = grid%sm33x ; imex = grid%em33x ; jmsx = grid%sm32x ; jmex = grid%em32x ; kmsx = grid%sm31x ; kmex = grid%em31x ;
           ipsx = grid%sp33x ; ipex = grid%ep33x ; jpsx = grid%sp32x ; jpex = grid%ep32x ; kpsx = grid%sp31x ; kpex = grid%ep31x ;
           imsy = grid%sm33y ; imey = grid%em33y ; jmsy = grid%sm32y ; jmey = grid%em32y ; kmsy = grid%sm31y ; kmey = grid%em31y ;
           ipsy = grid%sp33y ; ipey = grid%ep33y ; jpsy = grid%sp32y ; jpey = grid%ep32y ; kpsy = grid%sp31y ; kpey = grid%ep31y ;
       CASE  ( DATA_ORDER_XZY )
           imsx = grid%sm31x ; imex = grid%em31x ; jmsx = grid%sm33x ; jmex = grid%em33x ; kmsx = grid%sm32x ; kmex = grid%em32x ;
           ipsx = grid%sp31x ; ipex = grid%ep31x ; jpsx = grid%sp33x ; jpex = grid%ep33x ; kpsx = grid%sp32x ; kpex = grid%ep32x ;
           imsy = grid%sm31y ; imey = grid%em31y ; jmsy = grid%sm33y ; jmey = grid%em33y ; kmsy = grid%sm32y ; kmey = grid%em32y ;
           ipsy = grid%sp31y ; ipey = grid%ep31y ; jpsy = grid%sp33y ; jpey = grid%ep33y ; kpsy = grid%sp32y ; kpey = grid%ep32y ;
       CASE  ( DATA_ORDER_YZX )
           imsx = grid%sm33x ; imex = grid%em33x ; jmsx = grid%sm31x ; jmex = grid%em31x ; kmsx = grid%sm32x ; kmex = grid%em32x ;
           ipsx = grid%sp33x ; ipex = grid%ep33x ; jpsx = grid%sp31x ; jpex = grid%ep31x ; kpsx = grid%sp32x ; kpex = grid%ep32x ;
           imsy = grid%sm33y ; imey = grid%em33y ; jmsy = grid%sm31y ; jmey = grid%em31y ; kmsy = grid%sm32y ; kmey = grid%em32y ;
           ipsy = grid%sp33y ; ipey = grid%ep33y ; jpsy = grid%sp31y ; jpey = grid%ep31y ; kpsy = grid%sp32y ; kpey = grid%ep32y ;
     END SELECT data_ordering
   END SUBROUTINE get_ijk_from_grid1

   SUBROUTINE get_ijk_from_grid2 (  grid ,                   &
                           ids, ide, jds, jde, kds, kde,    &
                           ims, ime, jms, jme, kms, kme,    &
                           ips, ipe, jps, jpe, kps, kpe )

    IMPLICIT NONE

    TYPE( domain ), INTENT (IN)  :: grid
    INTEGER, INTENT(OUT) ::                                 &
                           ids, ide, jds, jde, kds, kde,    &
                           ims, ime, jms, jme, kms, kme,    &
                           ips, ipe, jps, jpe, kps, kpe

    data_ordering : SELECT CASE ( model_data_order )
       CASE  ( DATA_ORDER_XYZ )
           ids = grid%sd31 ; ide = grid%ed31 ; jds = grid%sd32 ; jde = grid%ed32 ; kds = grid%sd33 ; kde = grid%ed33 ;
           ims = grid%sm31 ; ime = grid%em31 ; jms = grid%sm32 ; jme = grid%em32 ; kms = grid%sm33 ; kme = grid%em33 ;
           ips = grid%sp31 ; ipe = grid%ep31 ; jps = grid%sp32 ; jpe = grid%ep32 ; kps = grid%sp33 ; kpe = grid%ep33 ; 
       CASE  ( DATA_ORDER_YXZ )
           ids = grid%sd32  ; ide = grid%ed32  ; jds = grid%sd31  ; jde = grid%ed31  ; kds = grid%sd33  ; kde = grid%ed33  ; 
           ims = grid%sm32  ; ime = grid%em32  ; jms = grid%sm31  ; jme = grid%em31  ; kms = grid%sm33  ; kme = grid%em33  ; 
           ips = grid%sp32  ; ipe = grid%ep32  ; jps = grid%sp31  ; jpe = grid%ep31  ; kps = grid%sp33  ; kpe = grid%ep33  ; 
       CASE  ( DATA_ORDER_ZXY )
           ids = grid%sd32  ; ide = grid%ed32  ; jds = grid%sd33  ; jde = grid%ed33  ; kds = grid%sd31  ; kde = grid%ed31  ; 
           ims = grid%sm32  ; ime = grid%em32  ; jms = grid%sm33  ; jme = grid%em33  ; kms = grid%sm31  ; kme = grid%em31  ; 
           ips = grid%sp32  ; ipe = grid%ep32  ; jps = grid%sp33  ; jpe = grid%ep33  ; kps = grid%sp31  ; kpe = grid%ep31  ; 
       CASE  ( DATA_ORDER_ZYX )
           ids = grid%sd33  ; ide = grid%ed33  ; jds = grid%sd32  ; jde = grid%ed32  ; kds = grid%sd31  ; kde = grid%ed31  ; 
           ims = grid%sm33  ; ime = grid%em33  ; jms = grid%sm32  ; jme = grid%em32  ; kms = grid%sm31  ; kme = grid%em31  ; 
           ips = grid%sp33  ; ipe = grid%ep33  ; jps = grid%sp32  ; jpe = grid%ep32  ; kps = grid%sp31  ; kpe = grid%ep31  ; 
       CASE  ( DATA_ORDER_XZY )
           ids = grid%sd31  ; ide = grid%ed31  ; jds = grid%sd33  ; jde = grid%ed33  ; kds = grid%sd32  ; kde = grid%ed32  ; 
           ims = grid%sm31  ; ime = grid%em31  ; jms = grid%sm33  ; jme = grid%em33  ; kms = grid%sm32  ; kme = grid%em32  ; 
           ips = grid%sp31  ; ipe = grid%ep31  ; jps = grid%sp33  ; jpe = grid%ep33  ; kps = grid%sp32  ; kpe = grid%ep32  ; 
       CASE  ( DATA_ORDER_YZX )
           ids = grid%sd33  ; ide = grid%ed33  ; jds = grid%sd31  ; jde = grid%ed31  ; kds = grid%sd32  ; kde = grid%ed32  ; 
           ims = grid%sm33  ; ime = grid%em33  ; jms = grid%sm31  ; jme = grid%em31  ; kms = grid%sm32  ; kme = grid%em32  ; 
           ips = grid%sp33  ; ipe = grid%ep33  ; jps = grid%sp31  ; jpe = grid%ep31  ; kps = grid%sp32  ; kpe = grid%ep32  ; 
    END SELECT data_ordering
   END SUBROUTINE get_ijk_from_grid2




   SUBROUTINE get_ijk_from_subgrid (  grid ,                &
                           ids0, ide0, jds0, jde0, kds0, kde0,    &
                           ims0, ime0, jms0, jme0, kms0, kme0,    &
                           ips0, ipe0, jps0, jpe0, kps0, kpe0    )
    TYPE( domain ), INTENT (IN)  :: grid
    INTEGER, INTENT(OUT) ::                                 &
                           ids0, ide0, jds0, jde0, kds0, kde0,    &
                           ims0, ime0, jms0, jme0, kms0, kme0,    &
                           ips0, ipe0, jps0, jpe0, kps0, kpe0
   
    INTEGER              ::                                 &
                           ids, ide, jds, jde, kds, kde,    &
                           ims, ime, jms, jme, kms, kme,    &
                           ips, ipe, jps, jpe, kps, kpe
     CALL get_ijk_from_grid (  grid ,                         &
                             ids, ide, jds, jde, kds, kde,    &
                             ims, ime, jms, jme, kms, kme,    &
                             ips, ipe, jps, jpe, kps, kpe    )
     ids0 = ids
     ide0 = ide * grid%sr_x
     ims0 = (ims-1)*grid%sr_x+1
     ime0 = ime * grid%sr_x
     ips0 = (ips-1)*grid%sr_x+1
     ipe0 = ipe * grid%sr_x

     jds0 = jds
     jde0 = jde * grid%sr_y
     jms0 = (jms-1)*grid%sr_y+1
     jme0 = jme * grid%sr_y
     jps0 = (jps-1)*grid%sr_y+1
     jpe0 = jpe * grid%sr_y

     kds0 = kds
     kde0 = kde
     kms0 = kms
     kme0 = kme
     kps0 = kps
     kpe0 = kpe
   RETURN
   END SUBROUTINE get_ijk_from_subgrid




   SUBROUTINE wrf_patch_domain( id , domdesc , parent, parent_id , parent_domdesc , &
                            sd1 , ed1 , sp1 , ep1 , sm1 , em1 , &
                            sd2 , ed2 , sp2 , ep2 , sm2 , em2 , &
                            sd3 , ed3 , sp3 , ep3 , sm3 , em3 , &
                                        sp1x , ep1x , sm1x , em1x , &
                                        sp2x , ep2x , sm2x , em2x , &
                                        sp3x , ep3x , sm3x , em3x , &
                                        sp1y , ep1y , sm1y , em1y , &
                                        sp2y , ep2y , sm2y , em2y , &
                                        sp3y , ep3y , sm3y , em3y , &
                            bdx , bdy , bdy_mask )
















































   USE module_machine
   IMPLICIT NONE
   LOGICAL, DIMENSION(4), INTENT(OUT)  :: bdy_mask
   INTEGER, INTENT(IN)   :: sd1 , ed1 , sd2 , ed2 , sd3 , ed3 , bdx , bdy
   INTEGER, INTENT(OUT)  :: sp1  , ep1  , sp2  , ep2  , sp3  , ep3  , &  
                            sm1  , em1  , sm2  , em2  , sm3  , em3
   INTEGER, INTENT(OUT)  :: sp1x , ep1x , sp2x , ep2x , sp3x , ep3x , &  
                            sm1x , em1x , sm2x , em2x , sm3x , em3x
   INTEGER, INTENT(OUT)  :: sp1y , ep1y , sp2y , ep2y , sp3y , ep3y , &  
                            sm1y , em1y , sm2y , em2y , sm3y , em3y
   INTEGER, INTENT(IN)   :: id , parent_id , parent_domdesc
   INTEGER, INTENT(INOUT)  :: domdesc
   TYPE(domain), POINTER :: parent



   INTEGER spec_bdy_width

   CALL nl_get_spec_bdy_width( 1, spec_bdy_width )
















   CALL wrf_dm_patch_domain( id , domdesc , parent_id , parent_domdesc , &
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

   SELECT CASE ( model_data_order )
      CASE ( DATA_ORDER_XYZ )
   bdy_mask( P_XSB ) = ( sd1                  <= sp1 .AND. sp1 <= sd1+spec_bdy_width-1 )
   bdy_mask( P_YSB ) = ( sd2                  <= sp2 .AND. sp2 <= sd2+spec_bdy_width-1 )
   bdy_mask( P_XEB ) = ( ed1-spec_bdy_width-1 <= ep1 .AND. ep1 <= ed1                  )
   bdy_mask( P_YEB ) = ( ed2-spec_bdy_width-1 <= ep2 .AND. ep2 <= ed2                  )
      CASE ( DATA_ORDER_YXZ )
   bdy_mask( P_XSB ) = ( sd2                  <= sp2 .AND. sp2 <= sd2+spec_bdy_width-1 )
   bdy_mask( P_YSB ) = ( sd1                  <= sp1 .AND. sp1 <= sd1+spec_bdy_width-1 )
   bdy_mask( P_XEB ) = ( ed2-spec_bdy_width-1 <= ep2 .AND. ep2 <= ed2                  )
   bdy_mask( P_YEB ) = ( ed1-spec_bdy_width-1 <= ep1 .AND. ep1 <= ed1                  )
      CASE ( DATA_ORDER_ZXY )
   bdy_mask( P_XSB ) = ( sd2                  <= sp2 .AND. sp2 <= sd2+spec_bdy_width-1 )
   bdy_mask( P_YSB ) = ( sd3                  <= sp3 .AND. sp3 <= sd3+spec_bdy_width-1 )
   bdy_mask( P_XEB ) = ( ed2-spec_bdy_width-1 <= ep2 .AND. ep2 <= ed2                  )
   bdy_mask( P_YEB ) = ( ed3-spec_bdy_width-1 <= ep3 .AND. ep3 <= ed3                  )
      CASE ( DATA_ORDER_ZYX )
   bdy_mask( P_XSB ) = ( sd3                  <= sp3 .AND. sp3 <= sd3+spec_bdy_width-1 )
   bdy_mask( P_YSB ) = ( sd2                  <= sp2 .AND. sp2 <= sd2+spec_bdy_width-1 )
   bdy_mask( P_XEB ) = ( ed3-spec_bdy_width-1 <= ep3 .AND. ep3 <= ed3                  )
   bdy_mask( P_YEB ) = ( ed2-spec_bdy_width-1 <= ep2 .AND. ep2 <= ed2                  )
      CASE ( DATA_ORDER_XZY )
   bdy_mask( P_XSB ) = ( sd1                  <= sp1 .AND. sp1 <= sd1+spec_bdy_width-1 )
   bdy_mask( P_YSB ) = ( sd3                  <= sp3 .AND. sp3 <= sd3+spec_bdy_width-1 )
   bdy_mask( P_XEB ) = ( ed1-spec_bdy_width-1 <= ep1 .AND. ep1 <= ed1                  )
   bdy_mask( P_YEB ) = ( ed3-spec_bdy_width-1 <= ep3 .AND. ep3 <= ed3                  )
      CASE ( DATA_ORDER_YZX )
   bdy_mask( P_XSB ) = ( sd3                  <= sp3 .AND. sp3 <= sd3+spec_bdy_width-1 )
   bdy_mask( P_YSB ) = ( sd1                  <= sp1 .AND. sp1 <= sd1+spec_bdy_width-1 )
   bdy_mask( P_XEB ) = ( ed3-spec_bdy_width-1 <= ep3 .AND. ep3 <= ed3                  )
   bdy_mask( P_YEB ) = ( ed1-spec_bdy_width-1 <= ep1 .AND. ep1 <= ed1                  )
   END SELECT



   RETURN
   END SUBROUTINE wrf_patch_domain

   SUBROUTINE alloc_and_configure_domain ( domain_id , active_this_task, grid , parent, kid )









































      IMPLICIT NONE

      

      INTEGER , INTENT(IN)            :: domain_id
      LOGICAL , OPTIONAL, INTENT(IN)  :: active_this_task 
      TYPE( domain ) , POINTER        :: grid
      TYPE( domain ) , POINTER        :: parent
      INTEGER , INTENT(IN)            :: kid    

      
      INTEGER                     :: sd1 , ed1 , sp1 , ep1 , sm1 , em1
      INTEGER                     :: sd2 , ed2 , sp2 , ep2 , sm2 , em2
      INTEGER                     :: sd3 , ed3 , sp3 , ep3 , sm3 , em3

      INTEGER                     :: sd1x , ed1x , sp1x , ep1x , sm1x , em1x
      INTEGER                     :: sd2x , ed2x , sp2x , ep2x , sm2x , em2x
      INTEGER                     :: sd3x , ed3x , sp3x , ep3x , sm3x , em3x

      INTEGER                     :: sd1y , ed1y , sp1y , ep1y , sm1y , em1y
      INTEGER                     :: sd2y , ed2y , sp2y , ep2y , sm2y , em2y
      INTEGER                     :: sd3y , ed3y , sp3y , ep3y , sm3y , em3y

      TYPE(domain) , POINTER      :: new_grid
      INTEGER                     :: i
      INTEGER                     :: parent_id , parent_domdesc , new_domdesc
      INTEGER                     :: bdyzone_x , bdyzone_y
      INTEGER                     :: nx, ny
      LOGICAL :: active


      active = .TRUE.
      IF ( PRESENT( active_this_task ) ) THEN
         active = active_this_task
      ENDIF






      data_ordering : SELECT CASE ( model_data_order )
        CASE  ( DATA_ORDER_XYZ )

          CALL nl_get_s_we( domain_id , sd1 )
          CALL nl_get_e_we( domain_id , ed1 )
          CALL nl_get_s_sn( domain_id , sd2 )
          CALL nl_get_e_sn( domain_id , ed2 )
          CALL nl_get_s_vert( domain_id , sd3 )
          CALL nl_get_e_vert( domain_id , ed3 )
          nx = ed1-sd1+1
          ny = ed2-sd2+1

        CASE  ( DATA_ORDER_YXZ )

          CALL nl_get_s_sn( domain_id , sd1 )
          CALL nl_get_e_sn( domain_id , ed1 )
          CALL nl_get_s_we( domain_id , sd2 )
          CALL nl_get_e_we( domain_id , ed2 )
          CALL nl_get_s_vert( domain_id , sd3 )
          CALL nl_get_e_vert( domain_id , ed3 )
          nx = ed2-sd2+1
          ny = ed1-sd1+1

        CASE  ( DATA_ORDER_ZXY )

          CALL nl_get_s_vert( domain_id , sd1 )
          CALL nl_get_e_vert( domain_id , ed1 )
          CALL nl_get_s_we( domain_id , sd2 )
          CALL nl_get_e_we( domain_id , ed2 )
          CALL nl_get_s_sn( domain_id , sd3 )
          CALL nl_get_e_sn( domain_id , ed3 )
          nx = ed2-sd2+1
          ny = ed3-sd3+1

        CASE  ( DATA_ORDER_ZYX )

          CALL nl_get_s_vert( domain_id , sd1 )
          CALL nl_get_e_vert( domain_id , ed1 )
          CALL nl_get_s_sn( domain_id , sd2 )
          CALL nl_get_e_sn( domain_id , ed2 )
          CALL nl_get_s_we( domain_id , sd3 )
          CALL nl_get_e_we( domain_id , ed3 )
          nx = ed3-sd3+1
          ny = ed2-sd2+1

        CASE  ( DATA_ORDER_XZY )

          CALL nl_get_s_we( domain_id , sd1 )
          CALL nl_get_e_we( domain_id , ed1 )
          CALL nl_get_s_vert( domain_id , sd2 )
          CALL nl_get_e_vert( domain_id , ed2 )
          CALL nl_get_s_sn( domain_id , sd3 )
          CALL nl_get_e_sn( domain_id , ed3 )
          nx = ed1-sd1+1
          ny = ed3-sd3+1

        CASE  ( DATA_ORDER_YZX )

          CALL nl_get_s_sn( domain_id , sd1 )
          CALL nl_get_e_sn( domain_id , ed1 )
          CALL nl_get_s_vert( domain_id , sd2 )
          CALL nl_get_e_vert( domain_id , ed2 )
          CALL nl_get_s_we( domain_id , sd3 )
          CALL nl_get_e_we( domain_id , ed3 )
          nx = ed3-sd3+1
          ny = ed1-sd1+1

      END SELECT data_ordering

      IF ( num_time_levels > 3 ) THEN
        WRITE ( wrf_err_message , * ) 'alloc_and_configure_domain: ', &
          'Incorrect value for num_time_levels ', num_time_levels
        CALL wrf_error_fatal3("<stdin>",618,&
TRIM ( wrf_err_message ) )
      ENDIF

      IF (ASSOCIATED(parent)) THEN
        parent_id = parent%id
        parent_domdesc = parent%domdesc
      ELSE
        parent_id = -1
        parent_domdesc = -1
      ENDIF


      CALL get_bdyzone_x( bdyzone_x )
      CALL get_bdyzone_y( bdyzone_y )

      ALLOCATE ( new_grid )
      ALLOCATE( new_grid%head_statevars )
      new_grid%head_statevars%Ndim = 0
      NULLIFY( new_grid%head_statevars%next)
      new_grid%tail_statevars => new_grid%head_statevars 

      ALLOCATE ( new_grid%parents( max_parents ) ) 
      ALLOCATE ( new_grid%nests( max_nests ) )
      NULLIFY( new_grid%sibling )
      DO i = 1, max_nests
         NULLIFY( new_grid%nests(i)%ptr )
      ENDDO
      NULLIFY  (new_grid%next)
      NULLIFY  (new_grid%same_level)
      NULLIFY  (new_grid%i_start)
      NULLIFY  (new_grid%j_start)
      NULLIFY  (new_grid%i_end)
      NULLIFY  (new_grid%j_end)
      ALLOCATE( new_grid%domain_clock )
      new_grid%domain_clock_created = .FALSE.
      ALLOCATE( new_grid%alarms( MAX_WRF_ALARMS ) )    
      ALLOCATE( new_grid%alarms_created( MAX_WRF_ALARMS ) )
      DO i = 1, MAX_WRF_ALARMS
        new_grid%alarms_created( i ) = .FALSE.
      ENDDO
      new_grid%time_set = .FALSE.
      new_grid%is_intermediate = .FALSE.
      new_grid%have_displayed_alloc_stats = .FALSE.

      new_grid%tiling_latch = .FALSE.  

      
      
      
      
      

 
      IF ( domain_id .NE. 1 ) THEN
         new_grid%parents(1)%ptr => parent
         new_grid%num_parents = 1
         parent%nests(kid)%ptr => new_grid
         new_grid%child_of_parent(1) = kid    
         parent%num_nests = parent%num_nests + 1
      END IF
      new_grid%id = domain_id                 
      new_grid%active_this_task = active

      CALL wrf_patch_domain( domain_id  , new_domdesc , parent, parent_id, parent_domdesc , &

                             sd1 , ed1 , sp1 , ep1 , sm1 , em1 , &     
                             sd2 , ed2 , sp2 , ep2 , sm2 , em2 , &     
                             sd3 , ed3 , sp3 , ep3 , sm3 , em3 , &

                                     sp1x , ep1x , sm1x , em1x , &     
                                     sp2x , ep2x , sm2x , em2x , &
                                     sp3x , ep3x , sm3x , em3x , &

                                     sp1y , ep1y , sm1y , em1y , &     
                                     sp2y , ep2y , sm2y , em2y , &
                                     sp3y , ep3y , sm3y , em3y , &

                         bdyzone_x  , bdyzone_y , new_grid%bdy_mask &
      ) 


      new_grid%domdesc = new_domdesc
      new_grid%num_nests = 0
      new_grid%num_siblings = 0
      new_grid%num_parents = 0
      new_grid%max_tiles   = 0
      new_grid%num_tiles_spec   = 0
      new_grid%nframes   = 0         









        
      new_grid%active_this_task = active
      CALL alloc_space_field ( new_grid, domain_id , 3 , 3 , .FALSE. , active,     &
                               sd1, ed1, sd2, ed2, sd3, ed3,       &
                               sm1,  em1,  sm2,  em2,  sm3,  em3,  &
                               sp1,  ep1,  sp2,  ep2,  sp3,  ep3,  &
                               sp1x, ep1x, sp2x, ep2x, sp3x, ep3x, &
                               sp1y, ep1y, sp2y, ep2y, sp3y, ep3y, &
                               sm1x, em1x, sm2x, em2x, sm3x, em3x, &   
                               sm1y, em1y, sm2y, em2y, sm3y, em3y  &   
      )








      new_grid%stepping_to_time = .FALSE.
      new_grid%adaptation_domain = 1
      new_grid%last_step_updated = -1





      new_grid%sd31                            = sd1 
      new_grid%ed31                            = ed1
      new_grid%sp31                            = sp1 
      new_grid%ep31                            = ep1 
      new_grid%sm31                            = sm1 
      new_grid%em31                            = em1
      new_grid%sd32                            = sd2 
      new_grid%ed32                            = ed2
      new_grid%sp32                            = sp2 
      new_grid%ep32                            = ep2 
      new_grid%sm32                            = sm2 
      new_grid%em32                            = em2
      new_grid%sd33                            = sd3 
      new_grid%ed33                            = ed3
      new_grid%sp33                            = sp3 
      new_grid%ep33                            = ep3 
      new_grid%sm33                            = sm3 
      new_grid%em33                            = em3

      new_grid%sp31x                           = sp1x
      new_grid%ep31x                           = ep1x
      new_grid%sm31x                           = sm1x
      new_grid%em31x                           = em1x
      new_grid%sp32x                           = sp2x
      new_grid%ep32x                           = ep2x
      new_grid%sm32x                           = sm2x
      new_grid%em32x                           = em2x
      new_grid%sp33x                           = sp3x
      new_grid%ep33x                           = ep3x
      new_grid%sm33x                           = sm3x
      new_grid%em33x                           = em3x

      new_grid%sp31y                           = sp1y
      new_grid%ep31y                           = ep1y
      new_grid%sm31y                           = sm1y
      new_grid%em31y                           = em1y
      new_grid%sp32y                           = sp2y
      new_grid%ep32y                           = ep2y
      new_grid%sm32y                           = sm2y
      new_grid%em32y                           = em2y
      new_grid%sp33y                           = sp3y
      new_grid%ep33y                           = ep3y
      new_grid%sm33y                           = sm3y
      new_grid%em33y                           = em3y

      SELECT CASE ( model_data_order )
         CASE  ( DATA_ORDER_XYZ )
            new_grid%sd21 = sd1 ; new_grid%sd22 = sd2 ;
            new_grid%ed21 = ed1 ; new_grid%ed22 = ed2 ;
            new_grid%sp21 = sp1 ; new_grid%sp22 = sp2 ;
            new_grid%ep21 = ep1 ; new_grid%ep22 = ep2 ;
            new_grid%sm21 = sm1 ; new_grid%sm22 = sm2 ;
            new_grid%em21 = em1 ; new_grid%em22 = em2 ;
            new_grid%sd11 = sd1
            new_grid%ed11 = ed1
            new_grid%sp11 = sp1
            new_grid%ep11 = ep1
            new_grid%sm11 = sm1
            new_grid%em11 = em1
         CASE  ( DATA_ORDER_YXZ )
            new_grid%sd21 = sd1 ; new_grid%sd22 = sd2 ;
            new_grid%ed21 = ed1 ; new_grid%ed22 = ed2 ;
            new_grid%sp21 = sp1 ; new_grid%sp22 = sp2 ;
            new_grid%ep21 = ep1 ; new_grid%ep22 = ep2 ;
            new_grid%sm21 = sm1 ; new_grid%sm22 = sm2 ;
            new_grid%em21 = em1 ; new_grid%em22 = em2 ;
            new_grid%sd11 = sd1
            new_grid%ed11 = ed1
            new_grid%sp11 = sp1
            new_grid%ep11 = ep1
            new_grid%sm11 = sm1
            new_grid%em11 = em1
         CASE  ( DATA_ORDER_ZXY )
            new_grid%sd21 = sd2 ; new_grid%sd22 = sd3 ;
            new_grid%ed21 = ed2 ; new_grid%ed22 = ed3 ;
            new_grid%sp21 = sp2 ; new_grid%sp22 = sp3 ;
            new_grid%ep21 = ep2 ; new_grid%ep22 = ep3 ;
            new_grid%sm21 = sm2 ; new_grid%sm22 = sm3 ;
            new_grid%em21 = em2 ; new_grid%em22 = em3 ;
            new_grid%sd11 = sd2
            new_grid%ed11 = ed2
            new_grid%sp11 = sp2
            new_grid%ep11 = ep2
            new_grid%sm11 = sm2
            new_grid%em11 = em2
         CASE  ( DATA_ORDER_ZYX )
            new_grid%sd21 = sd2 ; new_grid%sd22 = sd3 ;
            new_grid%ed21 = ed2 ; new_grid%ed22 = ed3 ;
            new_grid%sp21 = sp2 ; new_grid%sp22 = sp3 ;
            new_grid%ep21 = ep2 ; new_grid%ep22 = ep3 ;
            new_grid%sm21 = sm2 ; new_grid%sm22 = sm3 ;
            new_grid%em21 = em2 ; new_grid%em22 = em3 ;
            new_grid%sd11 = sd2
            new_grid%ed11 = ed2
            new_grid%sp11 = sp2
            new_grid%ep11 = ep2
            new_grid%sm11 = sm2
            new_grid%em11 = em2
         CASE  ( DATA_ORDER_XZY )
            new_grid%sd21 = sd1 ; new_grid%sd22 = sd3 ;
            new_grid%ed21 = ed1 ; new_grid%ed22 = ed3 ;
            new_grid%sp21 = sp1 ; new_grid%sp22 = sp3 ;
            new_grid%ep21 = ep1 ; new_grid%ep22 = ep3 ;
            new_grid%sm21 = sm1 ; new_grid%sm22 = sm3 ;
            new_grid%em21 = em1 ; new_grid%em22 = em3 ;
            new_grid%sd11 = sd1
            new_grid%ed11 = ed1
            new_grid%sp11 = sp1
            new_grid%ep11 = ep1
            new_grid%sm11 = sm1
            new_grid%em11 = em1
         CASE  ( DATA_ORDER_YZX )
            new_grid%sd21 = sd1 ; new_grid%sd22 = sd3 ;
            new_grid%ed21 = ed1 ; new_grid%ed22 = ed3 ;
            new_grid%sp21 = sp1 ; new_grid%sp22 = sp3 ;
            new_grid%ep21 = ep1 ; new_grid%ep22 = ep3 ;
            new_grid%sm21 = sm1 ; new_grid%sm22 = sm3 ;
            new_grid%em21 = em1 ; new_grid%em22 = em3 ;
            new_grid%sd11 = sd1
            new_grid%ed11 = ed1
            new_grid%sp11 = sp1
            new_grid%ep11 = ep1
            new_grid%sm11 = sm1
            new_grid%em11 = em1
      END SELECT

      CALL med_add_config_info_to_grid ( new_grid )           



      new_grid%tiled                           = .false.
      new_grid%patched                         = .false.
      NULLIFY(new_grid%mapping)




      grid => new_grid

 

      IF ( grid%active_this_task ) THEN

        ALLOCATE( grid%lattsloc( grid%max_ts_locs ) )
        ALLOCATE( grid%lontsloc( grid%max_ts_locs ) )
        ALLOCATE( grid%nametsloc( grid%max_ts_locs ) )
        ALLOCATE( grid%desctsloc( grid%max_ts_locs ) )
        ALLOCATE( grid%itsloc( grid%max_ts_locs ) )
        ALLOCATE( grid%jtsloc( grid%max_ts_locs ) )
        ALLOCATE( grid%id_tsloc( grid%max_ts_locs ) )
        ALLOCATE( grid%ts_filename( grid%max_ts_locs ) )
        grid%ntsloc        = 0
        grid%ntsloc_domain = 0



        ALLOCATE( grid%track_time_in( grid%track_loc_in ) )
        ALLOCATE( grid%track_lat_in( grid%track_loc_in ) )
        ALLOCATE( grid%track_lon_in( grid%track_loc_in ) )
  
        ALLOCATE( grid%track_time_domain( grid%track_loc_in ) )
        ALLOCATE( grid%track_lat_domain( grid%track_loc_in ) )
        ALLOCATE( grid%track_lon_domain( grid%track_loc_in ) )
        ALLOCATE( grid%track_i( grid%track_loc_in ) )
        ALLOCATE( grid%track_j( grid%track_loc_in ) )

      grid%track_loc        = 0
      grid%track_loc_domain = 0
      grid%track_have_calculated = .FALSE.
      grid%track_have_input      = .FALSE.

      ELSE
        WRITE (wrf_err_message,*)"Not allocating time series storage for domain ",domain_id," on this set of tasks"
        CALL wrf_message(TRIM(wrf_err_message))
      ENDIF


      CALL wrf_get_dm_communicator_for_id( grid%id, grid%communicator )
      CALL wrf_dm_define_comms( grid )


      grid%interp_mp = .true.

   END SUBROUTINE alloc_and_configure_domain

   SUBROUTINE get_fieldstr(ix,c,instr,outstr,noutstr,noerr)
     IMPLICIT NONE
     INTEGER, INTENT(IN)          :: ix
     CHARACTER*(*), INTENT(IN)    :: c
     CHARACTER*(*), INTENT(IN)    :: instr
     CHARACTER*(*), INTENT(OUT)   :: outstr
     INTEGER,       INTENT(IN)    :: noutstr  
     LOGICAL,       INTENT(INOUT) :: noerr     
     
     INTEGER, PARAMETER :: MAX_DEXES = 1000
     INTEGER I, PREV, IDEX
     INTEGER DEXES(MAX_DEXES)
     outstr = ""
     prev = 1
     dexes(1) = 1
     DO i = 2,MAX_DEXES
       idex = INDEX(instr(prev:LEN(TRIM(instr))),c)
       IF ( idex .GT. 0 ) THEN
         dexes(i) = idex+prev
         prev = dexes(i)+1
       ELSE
         dexes(i) = LEN(TRIM(instr))+2
       ENDIF
     ENDDO

     IF     ( (dexes(ix+1)-2)-(dexes(ix)) .GT. noutstr ) THEN
       noerr = .FALSE.  
     ELSE IF( dexes(ix) .EQ. dexes(ix+1) ) THEN 
       noerr = .FALSE.  
     ELSE
       outstr = instr(dexes(ix):(dexes(ix+1)-2))
       noerr = noerr .AND. .TRUE.
     ENDIF
   END SUBROUTINE get_fieldstr

   SUBROUTINE change_to_lower_case(instr,outstr)
     CHARACTER*(*) ,INTENT(IN)  :: instr
     CHARACTER*(*) ,INTENT(OUT) :: outstr

     CHARACTER*1                :: c
     INTEGER       ,PARAMETER   :: upper_to_lower =IACHAR('a')-IACHAR('A')
     INTEGER                    :: i,n,n1

     outstr = ' '
     N = len(instr)
     N1 = len(outstr)
     N = MIN(N,N1)
     outstr(1:N) = instr(1:N)
     DO i=1,N
       c = instr(i:i)
       if('A'<=c .and. c <='Z') outstr(i:i)=achar(iachar(c)+upper_to_lower)
     ENDDO
     RETURN
   END SUBROUTINE change_to_lower_case


   SUBROUTINE modify_io_masks1 ( grid , id )
      IMPLICIT NONE



      INTEGER              , INTENT(IN  )  :: id
      TYPE(domain), POINTER                :: grid
      
      TYPE(fieldlist), POINTER :: p, q
      INTEGER, PARAMETER :: read_unit = 10
      LOGICAL, EXTERNAL  :: wrf_dm_on_monitor
      CHARACTER*8000     :: inln, t1, fieldlst
      CHARACTER*256      :: fname, mess, dname, lookee
      CHARACTER*1        :: op, strmtyp
      CHARACTER*3        :: strmid
      CHARACTER*10       :: strmtyp_name
      INTEGER            :: io_status
      INTEGER            :: strmtyp_int, count_em
      INTEGER            :: lineno, fieldno, istrm, retval, itrace
      LOGICAL            :: keepgoing, noerr, gavewarning, ignorewarning, found
      LOGICAL, SAVE      :: you_warned_me = .FALSE.
      LOGICAL, SAVE      :: you_warned_me2(max_hst_mods,max_domains) = .FALSE.

      gavewarning = .FALSE.

      CALL nl_get_iofields_filename( id, fname )

      IF ( grid%is_intermediate ) RETURN                
      IF ( TRIM(fname) .EQ. "NONE_SPECIFIED" ) RETURN   

      IF ( wrf_dm_on_monitor() ) THEN
        OPEN ( UNIT   = read_unit    ,      &
               FILE   = TRIM(fname)      ,      &
               FORM   = "FORMATTED"      ,      &
               STATUS = "OLD"            ,      &
               IOSTAT = io_status         )
        IF ( io_status .EQ. 0 ) THEN   
          keepgoing = .TRUE.
          lineno = 0
          count_em = 0    
          DO WHILE ( keepgoing )
            READ(UNIT=read_unit,FMT='(A)',IOSTAT=io_status) inln
            keepgoing = (io_status .EQ. 0) .AND. (LEN(TRIM(inln)) .GT. 0)  
            IF ( keepgoing ) THEN
              lineno = lineno + 1
              IF ( .NOT. LEN(TRIM(inln)) .LT. LEN(inln) ) THEN
                WRITE(mess,*)'W A R N I N G : Line ',lineno,' of ',TRIM(fname),' is too long. Limit is ',LEN(inln),' characters.' 
                gavewarning = .TRUE.
              ENDIF
              IF ( INDEX(inln,'#') .EQ. 0 ) THEN   
                IF ( keepgoing ) THEN
                  noerr = .TRUE.
                  CALL get_fieldstr(1,':',inln,op,1,noerr)          
                  IF ( TRIM(op) .NE. '+' .AND. TRIM(op) .NE. '-' ) THEN
                    WRITE(mess,*)'W A R N I N G : unknown operation ',TRIM(op),' (should be + or -). Line ',lineno
                    gavewarning = .TRUE.
                  ENDIF
                  CALL get_fieldstr(2,':',inln,t1,1,noerr)          
                  CALL change_to_lower_case(t1,strmtyp) 

                  SELECT CASE (TRIM(strmtyp))
                  CASE ('h')
                     strmtyp_name = 'history'
                     strmtyp_int  = first_history
                  CASE ('i')
                     strmtyp_name = 'input'
                     strmtyp_int  = first_input
                  CASE DEFAULT
                     WRITE(mess,*)'W A R N I N G : unknown stream type ',TRIM(strmtyp),'. Line ',lineno
                     gavewarning = .TRUE.
                  END SELECT

                  CALL get_fieldstr(3,':',inln,strmid,3,noerr)      
                  READ(strmid,'(I3)') istrm
                  IF ( istrm .LT. 0 .OR. istrm .GT. last_history ) THEN
                    WRITE(mess,*)'W A R N I N G : invalid stream id ',istrm,' (should be 0 <= id <= ',last_history,'). Line ',lineno
                    gavewarning = .TRUE.
                  ENDIF
                  CALL get_fieldstr(4,':',inln,fieldlst,8000,noerr) 
                  IF ( noerr ) THEN
                    fieldno = 1
                    CALL get_fieldstr(fieldno,',',fieldlst,t1,8000,noerr)
                    CALL change_to_lower_case(t1,lookee)
                    DO WHILE ( noerr )    
                      p => grid%head_statevars%next
                      found = .FALSE.
                      count_em = count_em + 1
                      DO WHILE ( ASSOCIATED( p ) )
  
                        IF ( p%Ndim .EQ. 4 .AND. p%scalar_array ) THEN
  
                          DO itrace = PARAM_FIRST_SCALAR , p%num_table(grid%id)
                            CALL change_to_lower_case( p%dname_table( grid%id, itrace ) , dname ) 

                            IF ( TRIM(dname) .EQ. TRIM(lookee) ) &
                            CALL warn_me_or_set_mask (id, istrm, lineno, strmtyp_int, count_em, op, &
                                                      strmtyp_name, dname, fname, lookee,      &
                                                      p%streams_table(grid%id,itrace)%stream,  &
                                                      mess, found, you_warned_me2)
                          ENDDO
                        ELSE 
                          IF ( p%Ntl .GT. 0 ) THEN
                            CALL change_to_lower_case(p%DataName(1:LEN(TRIM(p%DataName))-2),dname)
                          ELSE
                            CALL change_to_lower_case(p%DataName,dname)
                          ENDIF
  
                          IF ( TRIM(dname) .EQ. TRIM(lookee) ) &
                          CALL warn_me_or_set_mask (id, istrm, lineno, strmtyp_int, count_em, op, &
                                                    strmtyp_name, dname, fname, lookee,      &
                                                    p%streams, mess, found, you_warned_me2)
                        ENDIF
                        p => p%next
                      ENDDO
                      IF ( .NOT. found ) THEN

                        WRITE(mess,*)'W A R N I N G : Unable to modify mask for ',TRIM(lookee),&
                                     '.  Variable not found. File: ',TRIM(fname),' at line ',lineno
                        CALL wrf_message(mess)

                        gavewarning = .TRUE.
                      ENDIF
                      fieldno = fieldno + 1
                      CALL get_fieldstr(fieldno,',',fieldlst,t1,256,noerr)
                      CALL change_to_lower_case(t1,lookee)
                    ENDDO
                  ELSE
                    WRITE(mess,*)'W A R N I N G : Problem reading ',TRIM(fname),' at line ',lineno
                    CALL wrf_message(mess)
                    gavewarning = .TRUE.
                  ENDIF
                ENDIF  
              ENDIF    
            ENDIF      
          ENDDO
        ELSE
          WRITE(mess,*)'W A R N I N G : Problem opening ',TRIM(fname)
          CALL wrf_message(mess)
          gavewarning = .TRUE.
        ENDIF
        CLOSE( read_unit )
        IF ( gavewarning ) THEN
          CALL nl_get_ignore_iofields_warning(1,ignorewarning)
          IF ( .NOT. ignorewarning ) THEN
            CALL wrf_message(mess)
            WRITE(mess,*)'modify_io_masks: problems reading ',TRIM(fname) 
            CALL wrf_message(mess)
            CALL wrf_error_fatal3("<stdin>",1131,&
'Set ignore_iofields_warn to true in namelist to ignore')
          ELSE
            IF ( .NOT. you_warned_me ) THEN
              if ( .NOT. you_warned_me2(count_em,id) ) CALL wrf_message(mess)  
              WRITE(mess,*)'Ignoring problems reading ',TRIM(fname) 
              CALL wrf_message(mess)
              CALL wrf_message('Continuing.  To make this a fatal error, set ignore_iofields_warn to false in namelist' )
              CALL wrf_message(' ')
              you_warned_me = .TRUE.
            ENDIF
          ENDIF
        ENDIF
      ENDIF  



      p => grid%head_statevars%next
      DO WHILE ( ASSOCIATED( p ) )
        IF ( p%Ndim .EQ. 4 .AND. p%scalar_array ) THEN

          DO itrace = PARAM_FIRST_SCALAR , p%num_table(grid%id)
            CALL wrf_dm_bcast_integer( p%streams_table(grid%id,itrace)%stream, (((2*(25)+2))/(4*8)+1) )
          ENDDO

        ELSE
          CALL wrf_dm_bcast_integer( p%streams, (((2*(25)+2))/(4*8)+1) )
        ENDIF
        p => p%next
      ENDDO

      
   END SUBROUTINE modify_io_masks1

   SUBROUTINE warn_me_or_set_mask (id, istrm, lineno, strmtyp_int, count_em, op, &
                                   strmtyp_name, dname, fname, lookee,      &
                                   p_stream, mess, found, you_warned_me2)

      IMPLICIT NONE






     INTEGER,       INTENT(IN )   :: id, istrm, lineno, strmtyp_int
     INTEGER,       INTENT(IN )   :: p_stream(*), count_em
     CHARACTER*1,   INTENT(IN )   :: op
     CHARACTER*10,  INTENT(IN )   :: strmtyp_name
     CHARACTER*256, INTENT(IN )   :: dname, fname, lookee
     CHARACTER*256, INTENT(OUT)   :: mess
     LOGICAL,       INTENT(OUT)   :: found
     LOGICAL,       INTENT(INOUT) :: you_warned_me2(max_hst_mods,max_domains)
   
     INTEGER                      :: retval

     found = .TRUE.
     IF      ( TRIM(op) .EQ. '+' ) THEN
       CALL get_mask( p_stream, strmtyp_int + istrm - 1, retval )
       IF ( retval .NE. 0 ) THEN
         WRITE(mess,*) 'Domain ',id, ' W A R N I N G : Variable ',TRIM(lookee),' already on ', &
                       TRIM(strmtyp_name), ' stream ',istrm, '.  File: ', TRIM(fname),' at line ',lineno
       ELSE
         WRITE(mess,*) 'Domain ', id, ' Setting ', TRIM(strmtyp_name), ' stream ',istrm,' for ', &
                                  TRIM(DNAME)  ; CALL wrf_debug(1,mess)
         CALL set_mask( p_stream, strmtyp_int + istrm - 1 )
       ENDIF
     ELSE IF ( TRIM(op) .EQ. '-' ) THEN
       CALL get_mask( p_stream, strmtyp_int + istrm - 1, retval )
       IF ( retval .EQ. 0 ) THEN
         WRITE(mess,*) 'Domain ',id, ' W A R N I N G : Variable ',TRIM(lookee),' already off ', &
                       TRIM(strmtyp_name), ' stream ',istrm, '. File: ',TRIM(fname),' at line ',lineno
       ELSE
         WRITE(mess,*) 'Domain ', id, ' Resetting ', TRIM(strmtyp_name), ' stream ',istrm,' for ', &
                                    TRIM(DNAME)  ; CALL wrf_debug(1,mess) 
         CALL reset_mask( p_stream, strmtyp_int + istrm - 1)
       ENDIF
     ENDIF
     IF ( count_em > max_hst_mods ) THEN

       WRITE(mess,*)'ERROR module_domain:  Array size for you_warned_me2 is fixed at ',max_hst_mods
       CALL wrf_message(mess)
       CALL wrf_error_fatal3("<stdin>",1213,&
'Did you really type > max_hst_mods fields into ', TRIM(fname) ,' ?')

     ELSE
       IF ( .NOT. you_warned_me2(count_em,id) ) THEN
         CALL wrf_message(mess)     
         you_warned_me2(count_em,id) = .TRUE.
       ENDIF
     ENDIF

   END SUBROUTINE warn_me_or_set_mask 







   SUBROUTINE alloc_space_field ( grid,   id, setinitval_in ,  tl_in , inter_domain_in , okay_to_alloc_in,  &
                                  sd31, ed31, sd32, ed32, sd33, ed33, &
                                  sm31 , em31 , sm32 , em32 , sm33 , em33 , &
                                  sp31 , ep31 , sp32 , ep32 , sp33 , ep33 , &
                                  sp31x, ep31x, sp32x, ep32x, sp33x, ep33x, &
                                  sp31y, ep31y, sp32y, ep32y, sp33y, ep33y, &
                                  sm31x, em31x, sm32x, em32x, sm33x, em33x, &
                                  sm31y, em31y, sm32y, em32y, sm33y, em33y )


      IMPLICIT NONE








INTERFACE
  SUBROUTINE allocs_0( grid,   id, setinitval_in ,  tl_in , inter_domain_in , okay_to_alloc_in, num_bytes_allocated ,  &
           sd31, ed31, sd32, ed32, sd33, ed33, &
           sm31 , em31 , sm32 , em32 , sm33 , em33 , &
           sp31 , ep31 , sp32 , ep32 , sp33 , ep33 , &
           sp31x, ep31x, sp32x, ep32x, sp33x, ep33x, &
           sp31y, ep31y, sp32y, ep32y, sp33y, ep33y, &
           sm31x, em31x, sm32x, em32x, sm33x, em33x, &
           sm31y, em31y, sm32y, em32y, sm33y, em33y )
    USE module_domain_type
    USE module_configure, ONLY : model_config_rec, grid_config_rec_type, in_use_for_config, model_to_grid_config_rec
    USE module_scalar_tables 
    IMPLICIT NONE
    

    TYPE(domain)               , POINTER          :: grid
    INTEGER , INTENT(IN)            :: id
    INTEGER , INTENT(IN)            :: setinitval_in   
    INTEGER , INTENT(IN)            :: sd31, ed31, sd32, ed32, sd33, ed33
    INTEGER , INTENT(IN)            :: sm31, em31, sm32, em32, sm33, em33
    INTEGER , INTENT(IN)            :: sp31, ep31, sp32, ep32, sp33, ep33
    INTEGER , INTENT(IN)            :: sp31x, ep31x, sp32x, ep32x, sp33x, ep33x
    INTEGER , INTENT(IN)            :: sp31y, ep31y, sp32y, ep32y, sp33y, ep33y
    INTEGER , INTENT(IN)            :: sm31x, em31x, sm32x, em32x, sm33x, em33x
    INTEGER , INTENT(IN)            :: sm31y, em31y, sm32y, em32y, sm33y, em33y

    
    
    
    
    INTEGER , INTENT(IN)            :: tl_in

    
    
    LOGICAL , INTENT(IN)            :: inter_domain_in, okay_to_alloc_in

    INTEGER(KIND=8) , INTENT(INOUT)         :: num_bytes_allocated
  END SUBROUTINE allocs_0
  SUBROUTINE allocs_1( grid,   id, setinitval_in ,  tl_in , inter_domain_in , okay_to_alloc_in, num_bytes_allocated ,  &
           sd31, ed31, sd32, ed32, sd33, ed33, &
           sm31 , em31 , sm32 , em32 , sm33 , em33 , &
           sp31 , ep31 , sp32 , ep32 , sp33 , ep33 , &
           sp31x, ep31x, sp32x, ep32x, sp33x, ep33x, &
           sp31y, ep31y, sp32y, ep32y, sp33y, ep33y, &
           sm31x, em31x, sm32x, em32x, sm33x, em33x, &
           sm31y, em31y, sm32y, em32y, sm33y, em33y )
    USE module_domain_type
    USE module_configure, ONLY : model_config_rec, grid_config_rec_type, in_use_for_config, model_to_grid_config_rec
    USE module_scalar_tables 
    IMPLICIT NONE
    

    TYPE(domain)               , POINTER          :: grid
    INTEGER , INTENT(IN)            :: id
    INTEGER , INTENT(IN)            :: setinitval_in   
    INTEGER , INTENT(IN)            :: sd31, ed31, sd32, ed32, sd33, ed33
    INTEGER , INTENT(IN)            :: sm31, em31, sm32, em32, sm33, em33
    INTEGER , INTENT(IN)            :: sp31, ep31, sp32, ep32, sp33, ep33
    INTEGER , INTENT(IN)            :: sp31x, ep31x, sp32x, ep32x, sp33x, ep33x
    INTEGER , INTENT(IN)            :: sp31y, ep31y, sp32y, ep32y, sp33y, ep33y
    INTEGER , INTENT(IN)            :: sm31x, em31x, sm32x, em32x, sm33x, em33x
    INTEGER , INTENT(IN)            :: sm31y, em31y, sm32y, em32y, sm33y, em33y

    
    
    
    
    INTEGER , INTENT(IN)            :: tl_in

    
    
    LOGICAL , INTENT(IN)            :: inter_domain_in, okay_to_alloc_in

    INTEGER(KIND=8) , INTENT(INOUT)         :: num_bytes_allocated
  END SUBROUTINE allocs_1
  SUBROUTINE allocs_2( grid,   id, setinitval_in ,  tl_in , inter_domain_in , okay_to_alloc_in, num_bytes_allocated ,  &
           sd31, ed31, sd32, ed32, sd33, ed33, &
           sm31 , em31 , sm32 , em32 , sm33 , em33 , &
           sp31 , ep31 , sp32 , ep32 , sp33 , ep33 , &
           sp31x, ep31x, sp32x, ep32x, sp33x, ep33x, &
           sp31y, ep31y, sp32y, ep32y, sp33y, ep33y, &
           sm31x, em31x, sm32x, em32x, sm33x, em33x, &
           sm31y, em31y, sm32y, em32y, sm33y, em33y )
    USE module_domain_type
    USE module_configure, ONLY : model_config_rec, grid_config_rec_type, in_use_for_config, model_to_grid_config_rec
    USE module_scalar_tables 
    IMPLICIT NONE
    

    TYPE(domain)               , POINTER          :: grid
    INTEGER , INTENT(IN)            :: id
    INTEGER , INTENT(IN)            :: setinitval_in   
    INTEGER , INTENT(IN)            :: sd31, ed31, sd32, ed32, sd33, ed33
    INTEGER , INTENT(IN)            :: sm31, em31, sm32, em32, sm33, em33
    INTEGER , INTENT(IN)            :: sp31, ep31, sp32, ep32, sp33, ep33
    INTEGER , INTENT(IN)            :: sp31x, ep31x, sp32x, ep32x, sp33x, ep33x
    INTEGER , INTENT(IN)            :: sp31y, ep31y, sp32y, ep32y, sp33y, ep33y
    INTEGER , INTENT(IN)            :: sm31x, em31x, sm32x, em32x, sm33x, em33x
    INTEGER , INTENT(IN)            :: sm31y, em31y, sm32y, em32y, sm33y, em33y

    
    
    
    
    INTEGER , INTENT(IN)            :: tl_in

    
    
    LOGICAL , INTENT(IN)            :: inter_domain_in, okay_to_alloc_in

    INTEGER(KIND=8) , INTENT(INOUT)         :: num_bytes_allocated
  END SUBROUTINE allocs_2
  SUBROUTINE allocs_3( grid,   id, setinitval_in ,  tl_in , inter_domain_in , okay_to_alloc_in, num_bytes_allocated ,  &
           sd31, ed31, sd32, ed32, sd33, ed33, &
           sm31 , em31 , sm32 , em32 , sm33 , em33 , &
           sp31 , ep31 , sp32 , ep32 , sp33 , ep33 , &
           sp31x, ep31x, sp32x, ep32x, sp33x, ep33x, &
           sp31y, ep31y, sp32y, ep32y, sp33y, ep33y, &
           sm31x, em31x, sm32x, em32x, sm33x, em33x, &
           sm31y, em31y, sm32y, em32y, sm33y, em33y )
    USE module_domain_type
    USE module_configure, ONLY : model_config_rec, grid_config_rec_type, in_use_for_config, model_to_grid_config_rec
    USE module_scalar_tables 
    IMPLICIT NONE
    

    TYPE(domain)               , POINTER          :: grid
    INTEGER , INTENT(IN)            :: id
    INTEGER , INTENT(IN)            :: setinitval_in   
    INTEGER , INTENT(IN)            :: sd31, ed31, sd32, ed32, sd33, ed33
    INTEGER , INTENT(IN)            :: sm31, em31, sm32, em32, sm33, em33
    INTEGER , INTENT(IN)            :: sp31, ep31, sp32, ep32, sp33, ep33
    INTEGER , INTENT(IN)            :: sp31x, ep31x, sp32x, ep32x, sp33x, ep33x
    INTEGER , INTENT(IN)            :: sp31y, ep31y, sp32y, ep32y, sp33y, ep33y
    INTEGER , INTENT(IN)            :: sm31x, em31x, sm32x, em32x, sm33x, em33x
    INTEGER , INTENT(IN)            :: sm31y, em31y, sm32y, em32y, sm33y, em33y

    
    
    
    
    INTEGER , INTENT(IN)            :: tl_in

    
    
    LOGICAL , INTENT(IN)            :: inter_domain_in, okay_to_alloc_in

    INTEGER(KIND=8) , INTENT(INOUT)         :: num_bytes_allocated
  END SUBROUTINE allocs_3
  SUBROUTINE allocs_4( grid,   id, setinitval_in ,  tl_in , inter_domain_in , okay_to_alloc_in, num_bytes_allocated ,  &
           sd31, ed31, sd32, ed32, sd33, ed33, &
           sm31 , em31 , sm32 , em32 , sm33 , em33 , &
           sp31 , ep31 , sp32 , ep32 , sp33 , ep33 , &
           sp31x, ep31x, sp32x, ep32x, sp33x, ep33x, &
           sp31y, ep31y, sp32y, ep32y, sp33y, ep33y, &
           sm31x, em31x, sm32x, em32x, sm33x, em33x, &
           sm31y, em31y, sm32y, em32y, sm33y, em33y )
    USE module_domain_type
    USE module_configure, ONLY : model_config_rec, grid_config_rec_type, in_use_for_config, model_to_grid_config_rec
    USE module_scalar_tables 
    IMPLICIT NONE
    

    TYPE(domain)               , POINTER          :: grid
    INTEGER , INTENT(IN)            :: id
    INTEGER , INTENT(IN)            :: setinitval_in   
    INTEGER , INTENT(IN)            :: sd31, ed31, sd32, ed32, sd33, ed33
    INTEGER , INTENT(IN)            :: sm31, em31, sm32, em32, sm33, em33
    INTEGER , INTENT(IN)            :: sp31, ep31, sp32, ep32, sp33, ep33
    INTEGER , INTENT(IN)            :: sp31x, ep31x, sp32x, ep32x, sp33x, ep33x
    INTEGER , INTENT(IN)            :: sp31y, ep31y, sp32y, ep32y, sp33y, ep33y
    INTEGER , INTENT(IN)            :: sm31x, em31x, sm32x, em32x, sm33x, em33x
    INTEGER , INTENT(IN)            :: sm31y, em31y, sm32y, em32y, sm33y, em33y

    
    
    
    
    INTEGER , INTENT(IN)            :: tl_in

    
    
    LOGICAL , INTENT(IN)            :: inter_domain_in, okay_to_alloc_in

    INTEGER(KIND=8) , INTENT(INOUT)         :: num_bytes_allocated
  END SUBROUTINE allocs_4
  SUBROUTINE allocs_5( grid,   id, setinitval_in ,  tl_in , inter_domain_in , okay_to_alloc_in, num_bytes_allocated ,  &
           sd31, ed31, sd32, ed32, sd33, ed33, &
           sm31 , em31 , sm32 , em32 , sm33 , em33 , &
           sp31 , ep31 , sp32 , ep32 , sp33 , ep33 , &
           sp31x, ep31x, sp32x, ep32x, sp33x, ep33x, &
           sp31y, ep31y, sp32y, ep32y, sp33y, ep33y, &
           sm31x, em31x, sm32x, em32x, sm33x, em33x, &
           sm31y, em31y, sm32y, em32y, sm33y, em33y )
    USE module_domain_type
    USE module_configure, ONLY : model_config_rec, grid_config_rec_type, in_use_for_config, model_to_grid_config_rec
    USE module_scalar_tables 
    IMPLICIT NONE
    

    TYPE(domain)               , POINTER          :: grid
    INTEGER , INTENT(IN)            :: id
    INTEGER , INTENT(IN)            :: setinitval_in   
    INTEGER , INTENT(IN)            :: sd31, ed31, sd32, ed32, sd33, ed33
    INTEGER , INTENT(IN)            :: sm31, em31, sm32, em32, sm33, em33
    INTEGER , INTENT(IN)            :: sp31, ep31, sp32, ep32, sp33, ep33
    INTEGER , INTENT(IN)            :: sp31x, ep31x, sp32x, ep32x, sp33x, ep33x
    INTEGER , INTENT(IN)            :: sp31y, ep31y, sp32y, ep32y, sp33y, ep33y
    INTEGER , INTENT(IN)            :: sm31x, em31x, sm32x, em32x, sm33x, em33x
    INTEGER , INTENT(IN)            :: sm31y, em31y, sm32y, em32y, sm33y, em33y

    
    
    
    
    INTEGER , INTENT(IN)            :: tl_in

    
    
    LOGICAL , INTENT(IN)            :: inter_domain_in, okay_to_alloc_in

    INTEGER(KIND=8) , INTENT(INOUT)         :: num_bytes_allocated
  END SUBROUTINE allocs_5
  SUBROUTINE allocs_6( grid,   id, setinitval_in ,  tl_in , inter_domain_in , okay_to_alloc_in, num_bytes_allocated ,  &
           sd31, ed31, sd32, ed32, sd33, ed33, &
           sm31 , em31 , sm32 , em32 , sm33 , em33 , &
           sp31 , ep31 , sp32 , ep32 , sp33 , ep33 , &
           sp31x, ep31x, sp32x, ep32x, sp33x, ep33x, &
           sp31y, ep31y, sp32y, ep32y, sp33y, ep33y, &
           sm31x, em31x, sm32x, em32x, sm33x, em33x, &
           sm31y, em31y, sm32y, em32y, sm33y, em33y )
    USE module_domain_type
    USE module_configure, ONLY : model_config_rec, grid_config_rec_type, in_use_for_config, model_to_grid_config_rec
    USE module_scalar_tables 
    IMPLICIT NONE
    

    TYPE(domain)               , POINTER          :: grid
    INTEGER , INTENT(IN)            :: id
    INTEGER , INTENT(IN)            :: setinitval_in   
    INTEGER , INTENT(IN)            :: sd31, ed31, sd32, ed32, sd33, ed33
    INTEGER , INTENT(IN)            :: sm31, em31, sm32, em32, sm33, em33
    INTEGER , INTENT(IN)            :: sp31, ep31, sp32, ep32, sp33, ep33
    INTEGER , INTENT(IN)            :: sp31x, ep31x, sp32x, ep32x, sp33x, ep33x
    INTEGER , INTENT(IN)            :: sp31y, ep31y, sp32y, ep32y, sp33y, ep33y
    INTEGER , INTENT(IN)            :: sm31x, em31x, sm32x, em32x, sm33x, em33x
    INTEGER , INTENT(IN)            :: sm31y, em31y, sm32y, em32y, sm33y, em33y

    
    
    
    
    INTEGER , INTENT(IN)            :: tl_in

    
    
    LOGICAL , INTENT(IN)            :: inter_domain_in, okay_to_alloc_in

    INTEGER(KIND=8) , INTENT(INOUT)         :: num_bytes_allocated
  END SUBROUTINE allocs_6
  SUBROUTINE allocs_7( grid,   id, setinitval_in ,  tl_in , inter_domain_in , okay_to_alloc_in, num_bytes_allocated ,  &
           sd31, ed31, sd32, ed32, sd33, ed33, &
           sm31 , em31 , sm32 , em32 , sm33 , em33 , &
           sp31 , ep31 , sp32 , ep32 , sp33 , ep33 , &
           sp31x, ep31x, sp32x, ep32x, sp33x, ep33x, &
           sp31y, ep31y, sp32y, ep32y, sp33y, ep33y, &
           sm31x, em31x, sm32x, em32x, sm33x, em33x, &
           sm31y, em31y, sm32y, em32y, sm33y, em33y )
    USE module_domain_type
    USE module_configure, ONLY : model_config_rec, grid_config_rec_type, in_use_for_config, model_to_grid_config_rec
    USE module_scalar_tables 
    IMPLICIT NONE
    

    TYPE(domain)               , POINTER          :: grid
    INTEGER , INTENT(IN)            :: id
    INTEGER , INTENT(IN)            :: setinitval_in   
    INTEGER , INTENT(IN)            :: sd31, ed31, sd32, ed32, sd33, ed33
    INTEGER , INTENT(IN)            :: sm31, em31, sm32, em32, sm33, em33
    INTEGER , INTENT(IN)            :: sp31, ep31, sp32, ep32, sp33, ep33
    INTEGER , INTENT(IN)            :: sp31x, ep31x, sp32x, ep32x, sp33x, ep33x
    INTEGER , INTENT(IN)            :: sp31y, ep31y, sp32y, ep32y, sp33y, ep33y
    INTEGER , INTENT(IN)            :: sm31x, em31x, sm32x, em32x, sm33x, em33x
    INTEGER , INTENT(IN)            :: sm31y, em31y, sm32y, em32y, sm33y, em33y

    
    
    
    
    INTEGER , INTENT(IN)            :: tl_in

    
    
    LOGICAL , INTENT(IN)            :: inter_domain_in, okay_to_alloc_in

    INTEGER(KIND=8) , INTENT(INOUT)         :: num_bytes_allocated
  END SUBROUTINE allocs_7
  SUBROUTINE allocs_8( grid,   id, setinitval_in ,  tl_in , inter_domain_in , okay_to_alloc_in, num_bytes_allocated ,  &
           sd31, ed31, sd32, ed32, sd33, ed33, &
           sm31 , em31 , sm32 , em32 , sm33 , em33 , &
           sp31 , ep31 , sp32 , ep32 , sp33 , ep33 , &
           sp31x, ep31x, sp32x, ep32x, sp33x, ep33x, &
           sp31y, ep31y, sp32y, ep32y, sp33y, ep33y, &
           sm31x, em31x, sm32x, em32x, sm33x, em33x, &
           sm31y, em31y, sm32y, em32y, sm33y, em33y )
    USE module_domain_type
    USE module_configure, ONLY : model_config_rec, grid_config_rec_type, in_use_for_config, model_to_grid_config_rec
    USE module_scalar_tables 
    IMPLICIT NONE
    

    TYPE(domain)               , POINTER          :: grid
    INTEGER , INTENT(IN)            :: id
    INTEGER , INTENT(IN)            :: setinitval_in   
    INTEGER , INTENT(IN)            :: sd31, ed31, sd32, ed32, sd33, ed33
    INTEGER , INTENT(IN)            :: sm31, em31, sm32, em32, sm33, em33
    INTEGER , INTENT(IN)            :: sp31, ep31, sp32, ep32, sp33, ep33
    INTEGER , INTENT(IN)            :: sp31x, ep31x, sp32x, ep32x, sp33x, ep33x
    INTEGER , INTENT(IN)            :: sp31y, ep31y, sp32y, ep32y, sp33y, ep33y
    INTEGER , INTENT(IN)            :: sm31x, em31x, sm32x, em32x, sm33x, em33x
    INTEGER , INTENT(IN)            :: sm31y, em31y, sm32y, em32y, sm33y, em33y

    
    
    
    
    INTEGER , INTENT(IN)            :: tl_in

    
    
    LOGICAL , INTENT(IN)            :: inter_domain_in, okay_to_alloc_in

    INTEGER(KIND=8) , INTENT(INOUT)         :: num_bytes_allocated
  END SUBROUTINE allocs_8
  SUBROUTINE allocs_9( grid,   id, setinitval_in ,  tl_in , inter_domain_in , okay_to_alloc_in, num_bytes_allocated ,  &
           sd31, ed31, sd32, ed32, sd33, ed33, &
           sm31 , em31 , sm32 , em32 , sm33 , em33 , &
           sp31 , ep31 , sp32 , ep32 , sp33 , ep33 , &
           sp31x, ep31x, sp32x, ep32x, sp33x, ep33x, &
           sp31y, ep31y, sp32y, ep32y, sp33y, ep33y, &
           sm31x, em31x, sm32x, em32x, sm33x, em33x, &
           sm31y, em31y, sm32y, em32y, sm33y, em33y )
    USE module_domain_type
    USE module_configure, ONLY : model_config_rec, grid_config_rec_type, in_use_for_config, model_to_grid_config_rec
    USE module_scalar_tables 
    IMPLICIT NONE
    

    TYPE(domain)               , POINTER          :: grid
    INTEGER , INTENT(IN)            :: id
    INTEGER , INTENT(IN)            :: setinitval_in   
    INTEGER , INTENT(IN)            :: sd31, ed31, sd32, ed32, sd33, ed33
    INTEGER , INTENT(IN)            :: sm31, em31, sm32, em32, sm33, em33
    INTEGER , INTENT(IN)            :: sp31, ep31, sp32, ep32, sp33, ep33
    INTEGER , INTENT(IN)            :: sp31x, ep31x, sp32x, ep32x, sp33x, ep33x
    INTEGER , INTENT(IN)            :: sp31y, ep31y, sp32y, ep32y, sp33y, ep33y
    INTEGER , INTENT(IN)            :: sm31x, em31x, sm32x, em32x, sm33x, em33x
    INTEGER , INTENT(IN)            :: sm31y, em31y, sm32y, em32y, sm33y, em33y

    
    
    
    
    INTEGER , INTENT(IN)            :: tl_in

    
    
    LOGICAL , INTENT(IN)            :: inter_domain_in, okay_to_alloc_in

    INTEGER(KIND=8) , INTENT(INOUT)         :: num_bytes_allocated
  END SUBROUTINE allocs_9
  SUBROUTINE allocs_10( grid,   id, setinitval_in ,  tl_in , inter_domain_in , okay_to_alloc_in, num_bytes_allocated ,  &
           sd31, ed31, sd32, ed32, sd33, ed33, &
           sm31 , em31 , sm32 , em32 , sm33 , em33 , &
           sp31 , ep31 , sp32 , ep32 , sp33 , ep33 , &
           sp31x, ep31x, sp32x, ep32x, sp33x, ep33x, &
           sp31y, ep31y, sp32y, ep32y, sp33y, ep33y, &
           sm31x, em31x, sm32x, em32x, sm33x, em33x, &
           sm31y, em31y, sm32y, em32y, sm33y, em33y )
    USE module_domain_type
    USE module_configure, ONLY : model_config_rec, grid_config_rec_type, in_use_for_config, model_to_grid_config_rec
    USE module_scalar_tables 
    IMPLICIT NONE
    

    TYPE(domain)               , POINTER          :: grid
    INTEGER , INTENT(IN)            :: id
    INTEGER , INTENT(IN)            :: setinitval_in   
    INTEGER , INTENT(IN)            :: sd31, ed31, sd32, ed32, sd33, ed33
    INTEGER , INTENT(IN)            :: sm31, em31, sm32, em32, sm33, em33
    INTEGER , INTENT(IN)            :: sp31, ep31, sp32, ep32, sp33, ep33
    INTEGER , INTENT(IN)            :: sp31x, ep31x, sp32x, ep32x, sp33x, ep33x
    INTEGER , INTENT(IN)            :: sp31y, ep31y, sp32y, ep32y, sp33y, ep33y
    INTEGER , INTENT(IN)            :: sm31x, em31x, sm32x, em32x, sm33x, em33x
    INTEGER , INTENT(IN)            :: sm31y, em31y, sm32y, em32y, sm33y, em33y

    
    
    
    
    INTEGER , INTENT(IN)            :: tl_in

    
    
    LOGICAL , INTENT(IN)            :: inter_domain_in, okay_to_alloc_in

    INTEGER(KIND=8) , INTENT(INOUT)         :: num_bytes_allocated
  END SUBROUTINE allocs_10
  SUBROUTINE allocs_11( grid,   id, setinitval_in ,  tl_in , inter_domain_in , okay_to_alloc_in, num_bytes_allocated ,  &
           sd31, ed31, sd32, ed32, sd33, ed33, &
           sm31 , em31 , sm32 , em32 , sm33 , em33 , &
           sp31 , ep31 , sp32 , ep32 , sp33 , ep33 , &
           sp31x, ep31x, sp32x, ep32x, sp33x, ep33x, &
           sp31y, ep31y, sp32y, ep32y, sp33y, ep33y, &
           sm31x, em31x, sm32x, em32x, sm33x, em33x, &
           sm31y, em31y, sm32y, em32y, sm33y, em33y )
    USE module_domain_type
    USE module_configure, ONLY : model_config_rec, grid_config_rec_type, in_use_for_config, model_to_grid_config_rec
    USE module_scalar_tables 
    IMPLICIT NONE
    

    TYPE(domain)               , POINTER          :: grid
    INTEGER , INTENT(IN)            :: id
    INTEGER , INTENT(IN)            :: setinitval_in   
    INTEGER , INTENT(IN)            :: sd31, ed31, sd32, ed32, sd33, ed33
    INTEGER , INTENT(IN)            :: sm31, em31, sm32, em32, sm33, em33
    INTEGER , INTENT(IN)            :: sp31, ep31, sp32, ep32, sp33, ep33
    INTEGER , INTENT(IN)            :: sp31x, ep31x, sp32x, ep32x, sp33x, ep33x
    INTEGER , INTENT(IN)            :: sp31y, ep31y, sp32y, ep32y, sp33y, ep33y
    INTEGER , INTENT(IN)            :: sm31x, em31x, sm32x, em32x, sm33x, em33x
    INTEGER , INTENT(IN)            :: sm31y, em31y, sm32y, em32y, sm33y, em33y

    
    
    
    
    INTEGER , INTENT(IN)            :: tl_in

    
    
    LOGICAL , INTENT(IN)            :: inter_domain_in, okay_to_alloc_in

    INTEGER(KIND=8) , INTENT(INOUT)         :: num_bytes_allocated
  END SUBROUTINE allocs_11
  SUBROUTINE allocs_12( grid,   id, setinitval_in ,  tl_in , inter_domain_in , okay_to_alloc_in, num_bytes_allocated ,  &
           sd31, ed31, sd32, ed32, sd33, ed33, &
           sm31 , em31 , sm32 , em32 , sm33 , em33 , &
           sp31 , ep31 , sp32 , ep32 , sp33 , ep33 , &
           sp31x, ep31x, sp32x, ep32x, sp33x, ep33x, &
           sp31y, ep31y, sp32y, ep32y, sp33y, ep33y, &
           sm31x, em31x, sm32x, em32x, sm33x, em33x, &
           sm31y, em31y, sm32y, em32y, sm33y, em33y )
    USE module_domain_type
    USE module_configure, ONLY : model_config_rec, grid_config_rec_type, in_use_for_config, model_to_grid_config_rec
    USE module_scalar_tables 
    IMPLICIT NONE
    

    TYPE(domain)               , POINTER          :: grid
    INTEGER , INTENT(IN)            :: id
    INTEGER , INTENT(IN)            :: setinitval_in   
    INTEGER , INTENT(IN)            :: sd31, ed31, sd32, ed32, sd33, ed33
    INTEGER , INTENT(IN)            :: sm31, em31, sm32, em32, sm33, em33
    INTEGER , INTENT(IN)            :: sp31, ep31, sp32, ep32, sp33, ep33
    INTEGER , INTENT(IN)            :: sp31x, ep31x, sp32x, ep32x, sp33x, ep33x
    INTEGER , INTENT(IN)            :: sp31y, ep31y, sp32y, ep32y, sp33y, ep33y
    INTEGER , INTENT(IN)            :: sm31x, em31x, sm32x, em32x, sm33x, em33x
    INTEGER , INTENT(IN)            :: sm31y, em31y, sm32y, em32y, sm33y, em33y

    
    
    
    
    INTEGER , INTENT(IN)            :: tl_in

    
    
    LOGICAL , INTENT(IN)            :: inter_domain_in, okay_to_alloc_in

    INTEGER(KIND=8) , INTENT(INOUT)         :: num_bytes_allocated
  END SUBROUTINE allocs_12
  SUBROUTINE allocs_13( grid,   id, setinitval_in ,  tl_in , inter_domain_in , okay_to_alloc_in, num_bytes_allocated ,  &
           sd31, ed31, sd32, ed32, sd33, ed33, &
           sm31 , em31 , sm32 , em32 , sm33 , em33 , &
           sp31 , ep31 , sp32 , ep32 , sp33 , ep33 , &
           sp31x, ep31x, sp32x, ep32x, sp33x, ep33x, &
           sp31y, ep31y, sp32y, ep32y, sp33y, ep33y, &
           sm31x, em31x, sm32x, em32x, sm33x, em33x, &
           sm31y, em31y, sm32y, em32y, sm33y, em33y )
    USE module_domain_type
    USE module_configure, ONLY : model_config_rec, grid_config_rec_type, in_use_for_config, model_to_grid_config_rec
    USE module_scalar_tables 
    IMPLICIT NONE
    

    TYPE(domain)               , POINTER          :: grid
    INTEGER , INTENT(IN)            :: id
    INTEGER , INTENT(IN)            :: setinitval_in   
    INTEGER , INTENT(IN)            :: sd31, ed31, sd32, ed32, sd33, ed33
    INTEGER , INTENT(IN)            :: sm31, em31, sm32, em32, sm33, em33
    INTEGER , INTENT(IN)            :: sp31, ep31, sp32, ep32, sp33, ep33
    INTEGER , INTENT(IN)            :: sp31x, ep31x, sp32x, ep32x, sp33x, ep33x
    INTEGER , INTENT(IN)            :: sp31y, ep31y, sp32y, ep32y, sp33y, ep33y
    INTEGER , INTENT(IN)            :: sm31x, em31x, sm32x, em32x, sm33x, em33x
    INTEGER , INTENT(IN)            :: sm31y, em31y, sm32y, em32y, sm33y, em33y

    
    
    
    
    INTEGER , INTENT(IN)            :: tl_in

    
    
    LOGICAL , INTENT(IN)            :: inter_domain_in, okay_to_alloc_in

    INTEGER(KIND=8) , INTENT(INOUT)         :: num_bytes_allocated
  END SUBROUTINE allocs_13
  SUBROUTINE allocs_14( grid,   id, setinitval_in ,  tl_in , inter_domain_in , okay_to_alloc_in, num_bytes_allocated ,  &
           sd31, ed31, sd32, ed32, sd33, ed33, &
           sm31 , em31 , sm32 , em32 , sm33 , em33 , &
           sp31 , ep31 , sp32 , ep32 , sp33 , ep33 , &
           sp31x, ep31x, sp32x, ep32x, sp33x, ep33x, &
           sp31y, ep31y, sp32y, ep32y, sp33y, ep33y, &
           sm31x, em31x, sm32x, em32x, sm33x, em33x, &
           sm31y, em31y, sm32y, em32y, sm33y, em33y )
    USE module_domain_type
    USE module_configure, ONLY : model_config_rec, grid_config_rec_type, in_use_for_config, model_to_grid_config_rec
    USE module_scalar_tables 
    IMPLICIT NONE
    

    TYPE(domain)               , POINTER          :: grid
    INTEGER , INTENT(IN)            :: id
    INTEGER , INTENT(IN)            :: setinitval_in   
    INTEGER , INTENT(IN)            :: sd31, ed31, sd32, ed32, sd33, ed33
    INTEGER , INTENT(IN)            :: sm31, em31, sm32, em32, sm33, em33
    INTEGER , INTENT(IN)            :: sp31, ep31, sp32, ep32, sp33, ep33
    INTEGER , INTENT(IN)            :: sp31x, ep31x, sp32x, ep32x, sp33x, ep33x
    INTEGER , INTENT(IN)            :: sp31y, ep31y, sp32y, ep32y, sp33y, ep33y
    INTEGER , INTENT(IN)            :: sm31x, em31x, sm32x, em32x, sm33x, em33x
    INTEGER , INTENT(IN)            :: sm31y, em31y, sm32y, em32y, sm33y, em33y

    
    
    
    
    INTEGER , INTENT(IN)            :: tl_in

    
    
    LOGICAL , INTENT(IN)            :: inter_domain_in, okay_to_alloc_in

    INTEGER(KIND=8) , INTENT(INOUT)         :: num_bytes_allocated
  END SUBROUTINE allocs_14
  SUBROUTINE allocs_15( grid,   id, setinitval_in ,  tl_in , inter_domain_in , okay_to_alloc_in, num_bytes_allocated ,  &
           sd31, ed31, sd32, ed32, sd33, ed33, &
           sm31 , em31 , sm32 , em32 , sm33 , em33 , &
           sp31 , ep31 , sp32 , ep32 , sp33 , ep33 , &
           sp31x, ep31x, sp32x, ep32x, sp33x, ep33x, &
           sp31y, ep31y, sp32y, ep32y, sp33y, ep33y, &
           sm31x, em31x, sm32x, em32x, sm33x, em33x, &
           sm31y, em31y, sm32y, em32y, sm33y, em33y )
    USE module_domain_type
    USE module_configure, ONLY : model_config_rec, grid_config_rec_type, in_use_for_config, model_to_grid_config_rec
    USE module_scalar_tables 
    IMPLICIT NONE
    

    TYPE(domain)               , POINTER          :: grid
    INTEGER , INTENT(IN)            :: id
    INTEGER , INTENT(IN)            :: setinitval_in   
    INTEGER , INTENT(IN)            :: sd31, ed31, sd32, ed32, sd33, ed33
    INTEGER , INTENT(IN)            :: sm31, em31, sm32, em32, sm33, em33
    INTEGER , INTENT(IN)            :: sp31, ep31, sp32, ep32, sp33, ep33
    INTEGER , INTENT(IN)            :: sp31x, ep31x, sp32x, ep32x, sp33x, ep33x
    INTEGER , INTENT(IN)            :: sp31y, ep31y, sp32y, ep32y, sp33y, ep33y
    INTEGER , INTENT(IN)            :: sm31x, em31x, sm32x, em32x, sm33x, em33x
    INTEGER , INTENT(IN)            :: sm31y, em31y, sm32y, em32y, sm33y, em33y

    
    
    
    
    INTEGER , INTENT(IN)            :: tl_in

    
    
    LOGICAL , INTENT(IN)            :: inter_domain_in, okay_to_alloc_in

    INTEGER(KIND=8) , INTENT(INOUT)         :: num_bytes_allocated
  END SUBROUTINE allocs_15
  SUBROUTINE allocs_16( grid,   id, setinitval_in ,  tl_in , inter_domain_in , okay_to_alloc_in, num_bytes_allocated ,  &
           sd31, ed31, sd32, ed32, sd33, ed33, &
           sm31 , em31 , sm32 , em32 , sm33 , em33 , &
           sp31 , ep31 , sp32 , ep32 , sp33 , ep33 , &
           sp31x, ep31x, sp32x, ep32x, sp33x, ep33x, &
           sp31y, ep31y, sp32y, ep32y, sp33y, ep33y, &
           sm31x, em31x, sm32x, em32x, sm33x, em33x, &
           sm31y, em31y, sm32y, em32y, sm33y, em33y )
    USE module_domain_type
    USE module_configure, ONLY : model_config_rec, grid_config_rec_type, in_use_for_config, model_to_grid_config_rec
    USE module_scalar_tables 
    IMPLICIT NONE
    

    TYPE(domain)               , POINTER          :: grid
    INTEGER , INTENT(IN)            :: id
    INTEGER , INTENT(IN)            :: setinitval_in   
    INTEGER , INTENT(IN)            :: sd31, ed31, sd32, ed32, sd33, ed33
    INTEGER , INTENT(IN)            :: sm31, em31, sm32, em32, sm33, em33
    INTEGER , INTENT(IN)            :: sp31, ep31, sp32, ep32, sp33, ep33
    INTEGER , INTENT(IN)            :: sp31x, ep31x, sp32x, ep32x, sp33x, ep33x
    INTEGER , INTENT(IN)            :: sp31y, ep31y, sp32y, ep32y, sp33y, ep33y
    INTEGER , INTENT(IN)            :: sm31x, em31x, sm32x, em32x, sm33x, em33x
    INTEGER , INTENT(IN)            :: sm31y, em31y, sm32y, em32y, sm33y, em33y

    
    
    
    
    INTEGER , INTENT(IN)            :: tl_in

    
    
    LOGICAL , INTENT(IN)            :: inter_domain_in, okay_to_alloc_in

    INTEGER(KIND=8) , INTENT(INOUT)         :: num_bytes_allocated
  END SUBROUTINE allocs_16
  SUBROUTINE allocs_17( grid,   id, setinitval_in ,  tl_in , inter_domain_in , okay_to_alloc_in, num_bytes_allocated ,  &
           sd31, ed31, sd32, ed32, sd33, ed33, &
           sm31 , em31 , sm32 , em32 , sm33 , em33 , &
           sp31 , ep31 , sp32 , ep32 , sp33 , ep33 , &
           sp31x, ep31x, sp32x, ep32x, sp33x, ep33x, &
           sp31y, ep31y, sp32y, ep32y, sp33y, ep33y, &
           sm31x, em31x, sm32x, em32x, sm33x, em33x, &
           sm31y, em31y, sm32y, em32y, sm33y, em33y )
    USE module_domain_type
    USE module_configure, ONLY : model_config_rec, grid_config_rec_type, in_use_for_config, model_to_grid_config_rec
    USE module_scalar_tables 
    IMPLICIT NONE
    

    TYPE(domain)               , POINTER          :: grid
    INTEGER , INTENT(IN)            :: id
    INTEGER , INTENT(IN)            :: setinitval_in   
    INTEGER , INTENT(IN)            :: sd31, ed31, sd32, ed32, sd33, ed33
    INTEGER , INTENT(IN)            :: sm31, em31, sm32, em32, sm33, em33
    INTEGER , INTENT(IN)            :: sp31, ep31, sp32, ep32, sp33, ep33
    INTEGER , INTENT(IN)            :: sp31x, ep31x, sp32x, ep32x, sp33x, ep33x
    INTEGER , INTENT(IN)            :: sp31y, ep31y, sp32y, ep32y, sp33y, ep33y
    INTEGER , INTENT(IN)            :: sm31x, em31x, sm32x, em32x, sm33x, em33x
    INTEGER , INTENT(IN)            :: sm31y, em31y, sm32y, em32y, sm33y, em33y

    
    
    
    
    INTEGER , INTENT(IN)            :: tl_in

    
    
    LOGICAL , INTENT(IN)            :: inter_domain_in, okay_to_alloc_in

    INTEGER(KIND=8) , INTENT(INOUT)         :: num_bytes_allocated
  END SUBROUTINE allocs_17
  SUBROUTINE allocs_18( grid,   id, setinitval_in ,  tl_in , inter_domain_in , okay_to_alloc_in, num_bytes_allocated ,  &
           sd31, ed31, sd32, ed32, sd33, ed33, &
           sm31 , em31 , sm32 , em32 , sm33 , em33 , &
           sp31 , ep31 , sp32 , ep32 , sp33 , ep33 , &
           sp31x, ep31x, sp32x, ep32x, sp33x, ep33x, &
           sp31y, ep31y, sp32y, ep32y, sp33y, ep33y, &
           sm31x, em31x, sm32x, em32x, sm33x, em33x, &
           sm31y, em31y, sm32y, em32y, sm33y, em33y )
    USE module_domain_type
    USE module_configure, ONLY : model_config_rec, grid_config_rec_type, in_use_for_config, model_to_grid_config_rec
    USE module_scalar_tables 
    IMPLICIT NONE
    

    TYPE(domain)               , POINTER          :: grid
    INTEGER , INTENT(IN)            :: id
    INTEGER , INTENT(IN)            :: setinitval_in   
    INTEGER , INTENT(IN)            :: sd31, ed31, sd32, ed32, sd33, ed33
    INTEGER , INTENT(IN)            :: sm31, em31, sm32, em32, sm33, em33
    INTEGER , INTENT(IN)            :: sp31, ep31, sp32, ep32, sp33, ep33
    INTEGER , INTENT(IN)            :: sp31x, ep31x, sp32x, ep32x, sp33x, ep33x
    INTEGER , INTENT(IN)            :: sp31y, ep31y, sp32y, ep32y, sp33y, ep33y
    INTEGER , INTENT(IN)            :: sm31x, em31x, sm32x, em32x, sm33x, em33x
    INTEGER , INTENT(IN)            :: sm31y, em31y, sm32y, em32y, sm33y, em33y

    
    
    
    
    INTEGER , INTENT(IN)            :: tl_in

    
    
    LOGICAL , INTENT(IN)            :: inter_domain_in, okay_to_alloc_in

    INTEGER(KIND=8) , INTENT(INOUT)         :: num_bytes_allocated
  END SUBROUTINE allocs_18
  SUBROUTINE allocs_19( grid,   id, setinitval_in ,  tl_in , inter_domain_in , okay_to_alloc_in, num_bytes_allocated ,  &
           sd31, ed31, sd32, ed32, sd33, ed33, &
           sm31 , em31 , sm32 , em32 , sm33 , em33 , &
           sp31 , ep31 , sp32 , ep32 , sp33 , ep33 , &
           sp31x, ep31x, sp32x, ep32x, sp33x, ep33x, &
           sp31y, ep31y, sp32y, ep32y, sp33y, ep33y, &
           sm31x, em31x, sm32x, em32x, sm33x, em33x, &
           sm31y, em31y, sm32y, em32y, sm33y, em33y )
    USE module_domain_type
    USE module_configure, ONLY : model_config_rec, grid_config_rec_type, in_use_for_config, model_to_grid_config_rec
    USE module_scalar_tables 
    IMPLICIT NONE
    

    TYPE(domain)               , POINTER          :: grid
    INTEGER , INTENT(IN)            :: id
    INTEGER , INTENT(IN)            :: setinitval_in   
    INTEGER , INTENT(IN)            :: sd31, ed31, sd32, ed32, sd33, ed33
    INTEGER , INTENT(IN)            :: sm31, em31, sm32, em32, sm33, em33
    INTEGER , INTENT(IN)            :: sp31, ep31, sp32, ep32, sp33, ep33
    INTEGER , INTENT(IN)            :: sp31x, ep31x, sp32x, ep32x, sp33x, ep33x
    INTEGER , INTENT(IN)            :: sp31y, ep31y, sp32y, ep32y, sp33y, ep33y
    INTEGER , INTENT(IN)            :: sm31x, em31x, sm32x, em32x, sm33x, em33x
    INTEGER , INTENT(IN)            :: sm31y, em31y, sm32y, em32y, sm33y, em33y

    
    
    
    
    INTEGER , INTENT(IN)            :: tl_in

    
    
    LOGICAL , INTENT(IN)            :: inter_domain_in, okay_to_alloc_in

    INTEGER(KIND=8) , INTENT(INOUT)         :: num_bytes_allocated
  END SUBROUTINE allocs_19
  SUBROUTINE allocs_20( grid,   id, setinitval_in ,  tl_in , inter_domain_in , okay_to_alloc_in, num_bytes_allocated ,  &
           sd31, ed31, sd32, ed32, sd33, ed33, &
           sm31 , em31 , sm32 , em32 , sm33 , em33 , &
           sp31 , ep31 , sp32 , ep32 , sp33 , ep33 , &
           sp31x, ep31x, sp32x, ep32x, sp33x, ep33x, &
           sp31y, ep31y, sp32y, ep32y, sp33y, ep33y, &
           sm31x, em31x, sm32x, em32x, sm33x, em33x, &
           sm31y, em31y, sm32y, em32y, sm33y, em33y )
    USE module_domain_type
    USE module_configure, ONLY : model_config_rec, grid_config_rec_type, in_use_for_config, model_to_grid_config_rec
    USE module_scalar_tables 
    IMPLICIT NONE
    

    TYPE(domain)               , POINTER          :: grid
    INTEGER , INTENT(IN)            :: id
    INTEGER , INTENT(IN)            :: setinitval_in   
    INTEGER , INTENT(IN)            :: sd31, ed31, sd32, ed32, sd33, ed33
    INTEGER , INTENT(IN)            :: sm31, em31, sm32, em32, sm33, em33
    INTEGER , INTENT(IN)            :: sp31, ep31, sp32, ep32, sp33, ep33
    INTEGER , INTENT(IN)            :: sp31x, ep31x, sp32x, ep32x, sp33x, ep33x
    INTEGER , INTENT(IN)            :: sp31y, ep31y, sp32y, ep32y, sp33y, ep33y
    INTEGER , INTENT(IN)            :: sm31x, em31x, sm32x, em32x, sm33x, em33x
    INTEGER , INTENT(IN)            :: sm31y, em31y, sm32y, em32y, sm33y, em33y

    
    
    
    
    INTEGER , INTENT(IN)            :: tl_in

    
    
    LOGICAL , INTENT(IN)            :: inter_domain_in, okay_to_alloc_in

    INTEGER(KIND=8) , INTENT(INOUT)         :: num_bytes_allocated
  END SUBROUTINE allocs_20
  SUBROUTINE allocs_21( grid,   id, setinitval_in ,  tl_in , inter_domain_in , okay_to_alloc_in, num_bytes_allocated ,  &
           sd31, ed31, sd32, ed32, sd33, ed33, &
           sm31 , em31 , sm32 , em32 , sm33 , em33 , &
           sp31 , ep31 , sp32 , ep32 , sp33 , ep33 , &
           sp31x, ep31x, sp32x, ep32x, sp33x, ep33x, &
           sp31y, ep31y, sp32y, ep32y, sp33y, ep33y, &
           sm31x, em31x, sm32x, em32x, sm33x, em33x, &
           sm31y, em31y, sm32y, em32y, sm33y, em33y )
    USE module_domain_type
    USE module_configure, ONLY : model_config_rec, grid_config_rec_type, in_use_for_config, model_to_grid_config_rec
    USE module_scalar_tables 
    IMPLICIT NONE
    

    TYPE(domain)               , POINTER          :: grid
    INTEGER , INTENT(IN)            :: id
    INTEGER , INTENT(IN)            :: setinitval_in   
    INTEGER , INTENT(IN)            :: sd31, ed31, sd32, ed32, sd33, ed33
    INTEGER , INTENT(IN)            :: sm31, em31, sm32, em32, sm33, em33
    INTEGER , INTENT(IN)            :: sp31, ep31, sp32, ep32, sp33, ep33
    INTEGER , INTENT(IN)            :: sp31x, ep31x, sp32x, ep32x, sp33x, ep33x
    INTEGER , INTENT(IN)            :: sp31y, ep31y, sp32y, ep32y, sp33y, ep33y
    INTEGER , INTENT(IN)            :: sm31x, em31x, sm32x, em32x, sm33x, em33x
    INTEGER , INTENT(IN)            :: sm31y, em31y, sm32y, em32y, sm33y, em33y

    
    
    
    
    INTEGER , INTENT(IN)            :: tl_in

    
    
    LOGICAL , INTENT(IN)            :: inter_domain_in, okay_to_alloc_in

    INTEGER(KIND=8) , INTENT(INOUT)         :: num_bytes_allocated
  END SUBROUTINE allocs_21
  SUBROUTINE allocs_22( grid,   id, setinitval_in ,  tl_in , inter_domain_in , okay_to_alloc_in, num_bytes_allocated ,  &
           sd31, ed31, sd32, ed32, sd33, ed33, &
           sm31 , em31 , sm32 , em32 , sm33 , em33 , &
           sp31 , ep31 , sp32 , ep32 , sp33 , ep33 , &
           sp31x, ep31x, sp32x, ep32x, sp33x, ep33x, &
           sp31y, ep31y, sp32y, ep32y, sp33y, ep33y, &
           sm31x, em31x, sm32x, em32x, sm33x, em33x, &
           sm31y, em31y, sm32y, em32y, sm33y, em33y )
    USE module_domain_type
    USE module_configure, ONLY : model_config_rec, grid_config_rec_type, in_use_for_config, model_to_grid_config_rec
    USE module_scalar_tables 
    IMPLICIT NONE
    

    TYPE(domain)               , POINTER          :: grid
    INTEGER , INTENT(IN)            :: id
    INTEGER , INTENT(IN)            :: setinitval_in   
    INTEGER , INTENT(IN)            :: sd31, ed31, sd32, ed32, sd33, ed33
    INTEGER , INTENT(IN)            :: sm31, em31, sm32, em32, sm33, em33
    INTEGER , INTENT(IN)            :: sp31, ep31, sp32, ep32, sp33, ep33
    INTEGER , INTENT(IN)            :: sp31x, ep31x, sp32x, ep32x, sp33x, ep33x
    INTEGER , INTENT(IN)            :: sp31y, ep31y, sp32y, ep32y, sp33y, ep33y
    INTEGER , INTENT(IN)            :: sm31x, em31x, sm32x, em32x, sm33x, em33x
    INTEGER , INTENT(IN)            :: sm31y, em31y, sm32y, em32y, sm33y, em33y

    
    
    
    
    INTEGER , INTENT(IN)            :: tl_in

    
    
    LOGICAL , INTENT(IN)            :: inter_domain_in, okay_to_alloc_in

    INTEGER(KIND=8) , INTENT(INOUT)         :: num_bytes_allocated
  END SUBROUTINE allocs_22
  SUBROUTINE allocs_23( grid,   id, setinitval_in ,  tl_in , inter_domain_in , okay_to_alloc_in, num_bytes_allocated ,  &
           sd31, ed31, sd32, ed32, sd33, ed33, &
           sm31 , em31 , sm32 , em32 , sm33 , em33 , &
           sp31 , ep31 , sp32 , ep32 , sp33 , ep33 , &
           sp31x, ep31x, sp32x, ep32x, sp33x, ep33x, &
           sp31y, ep31y, sp32y, ep32y, sp33y, ep33y, &
           sm31x, em31x, sm32x, em32x, sm33x, em33x, &
           sm31y, em31y, sm32y, em32y, sm33y, em33y )
    USE module_domain_type
    USE module_configure, ONLY : model_config_rec, grid_config_rec_type, in_use_for_config, model_to_grid_config_rec
    USE module_scalar_tables 
    IMPLICIT NONE
    

    TYPE(domain)               , POINTER          :: grid
    INTEGER , INTENT(IN)            :: id
    INTEGER , INTENT(IN)            :: setinitval_in   
    INTEGER , INTENT(IN)            :: sd31, ed31, sd32, ed32, sd33, ed33
    INTEGER , INTENT(IN)            :: sm31, em31, sm32, em32, sm33, em33
    INTEGER , INTENT(IN)            :: sp31, ep31, sp32, ep32, sp33, ep33
    INTEGER , INTENT(IN)            :: sp31x, ep31x, sp32x, ep32x, sp33x, ep33x
    INTEGER , INTENT(IN)            :: sp31y, ep31y, sp32y, ep32y, sp33y, ep33y
    INTEGER , INTENT(IN)            :: sm31x, em31x, sm32x, em32x, sm33x, em33x
    INTEGER , INTENT(IN)            :: sm31y, em31y, sm32y, em32y, sm33y, em33y

    
    
    
    
    INTEGER , INTENT(IN)            :: tl_in

    
    
    LOGICAL , INTENT(IN)            :: inter_domain_in, okay_to_alloc_in

    INTEGER(KIND=8) , INTENT(INOUT)         :: num_bytes_allocated
  END SUBROUTINE allocs_23
  SUBROUTINE allocs_24( grid,   id, setinitval_in ,  tl_in , inter_domain_in , okay_to_alloc_in, num_bytes_allocated ,  &
           sd31, ed31, sd32, ed32, sd33, ed33, &
           sm31 , em31 , sm32 , em32 , sm33 , em33 , &
           sp31 , ep31 , sp32 , ep32 , sp33 , ep33 , &
           sp31x, ep31x, sp32x, ep32x, sp33x, ep33x, &
           sp31y, ep31y, sp32y, ep32y, sp33y, ep33y, &
           sm31x, em31x, sm32x, em32x, sm33x, em33x, &
           sm31y, em31y, sm32y, em32y, sm33y, em33y )
    USE module_domain_type
    USE module_configure, ONLY : model_config_rec, grid_config_rec_type, in_use_for_config, model_to_grid_config_rec
    USE module_scalar_tables 
    IMPLICIT NONE
    

    TYPE(domain)               , POINTER          :: grid
    INTEGER , INTENT(IN)            :: id
    INTEGER , INTENT(IN)            :: setinitval_in   
    INTEGER , INTENT(IN)            :: sd31, ed31, sd32, ed32, sd33, ed33
    INTEGER , INTENT(IN)            :: sm31, em31, sm32, em32, sm33, em33
    INTEGER , INTENT(IN)            :: sp31, ep31, sp32, ep32, sp33, ep33
    INTEGER , INTENT(IN)            :: sp31x, ep31x, sp32x, ep32x, sp33x, ep33x
    INTEGER , INTENT(IN)            :: sp31y, ep31y, sp32y, ep32y, sp33y, ep33y
    INTEGER , INTENT(IN)            :: sm31x, em31x, sm32x, em32x, sm33x, em33x
    INTEGER , INTENT(IN)            :: sm31y, em31y, sm32y, em32y, sm33y, em33y

    
    
    
    
    INTEGER , INTENT(IN)            :: tl_in

    
    
    LOGICAL , INTENT(IN)            :: inter_domain_in, okay_to_alloc_in

    INTEGER(KIND=8) , INTENT(INOUT)         :: num_bytes_allocated
  END SUBROUTINE allocs_24
  SUBROUTINE allocs_25( grid,   id, setinitval_in ,  tl_in , inter_domain_in , okay_to_alloc_in, num_bytes_allocated ,  &
           sd31, ed31, sd32, ed32, sd33, ed33, &
           sm31 , em31 , sm32 , em32 , sm33 , em33 , &
           sp31 , ep31 , sp32 , ep32 , sp33 , ep33 , &
           sp31x, ep31x, sp32x, ep32x, sp33x, ep33x, &
           sp31y, ep31y, sp32y, ep32y, sp33y, ep33y, &
           sm31x, em31x, sm32x, em32x, sm33x, em33x, &
           sm31y, em31y, sm32y, em32y, sm33y, em33y )
    USE module_domain_type
    USE module_configure, ONLY : model_config_rec, grid_config_rec_type, in_use_for_config, model_to_grid_config_rec
    USE module_scalar_tables 
    IMPLICIT NONE
    

    TYPE(domain)               , POINTER          :: grid
    INTEGER , INTENT(IN)            :: id
    INTEGER , INTENT(IN)            :: setinitval_in   
    INTEGER , INTENT(IN)            :: sd31, ed31, sd32, ed32, sd33, ed33
    INTEGER , INTENT(IN)            :: sm31, em31, sm32, em32, sm33, em33
    INTEGER , INTENT(IN)            :: sp31, ep31, sp32, ep32, sp33, ep33
    INTEGER , INTENT(IN)            :: sp31x, ep31x, sp32x, ep32x, sp33x, ep33x
    INTEGER , INTENT(IN)            :: sp31y, ep31y, sp32y, ep32y, sp33y, ep33y
    INTEGER , INTENT(IN)            :: sm31x, em31x, sm32x, em32x, sm33x, em33x
    INTEGER , INTENT(IN)            :: sm31y, em31y, sm32y, em32y, sm33y, em33y

    
    
    
    
    INTEGER , INTENT(IN)            :: tl_in

    
    
    LOGICAL , INTENT(IN)            :: inter_domain_in, okay_to_alloc_in

    INTEGER(KIND=8) , INTENT(INOUT)         :: num_bytes_allocated
  END SUBROUTINE allocs_25
  SUBROUTINE allocs_26( grid,   id, setinitval_in ,  tl_in , inter_domain_in , okay_to_alloc_in, num_bytes_allocated ,  &
           sd31, ed31, sd32, ed32, sd33, ed33, &
           sm31 , em31 , sm32 , em32 , sm33 , em33 , &
           sp31 , ep31 , sp32 , ep32 , sp33 , ep33 , &
           sp31x, ep31x, sp32x, ep32x, sp33x, ep33x, &
           sp31y, ep31y, sp32y, ep32y, sp33y, ep33y, &
           sm31x, em31x, sm32x, em32x, sm33x, em33x, &
           sm31y, em31y, sm32y, em32y, sm33y, em33y )
    USE module_domain_type
    USE module_configure, ONLY : model_config_rec, grid_config_rec_type, in_use_for_config, model_to_grid_config_rec
    USE module_scalar_tables 
    IMPLICIT NONE
    

    TYPE(domain)               , POINTER          :: grid
    INTEGER , INTENT(IN)            :: id
    INTEGER , INTENT(IN)            :: setinitval_in   
    INTEGER , INTENT(IN)            :: sd31, ed31, sd32, ed32, sd33, ed33
    INTEGER , INTENT(IN)            :: sm31, em31, sm32, em32, sm33, em33
    INTEGER , INTENT(IN)            :: sp31, ep31, sp32, ep32, sp33, ep33
    INTEGER , INTENT(IN)            :: sp31x, ep31x, sp32x, ep32x, sp33x, ep33x
    INTEGER , INTENT(IN)            :: sp31y, ep31y, sp32y, ep32y, sp33y, ep33y
    INTEGER , INTENT(IN)            :: sm31x, em31x, sm32x, em32x, sm33x, em33x
    INTEGER , INTENT(IN)            :: sm31y, em31y, sm32y, em32y, sm33y, em33y

    
    
    
    
    INTEGER , INTENT(IN)            :: tl_in

    
    
    LOGICAL , INTENT(IN)            :: inter_domain_in, okay_to_alloc_in

    INTEGER(KIND=8) , INTENT(INOUT)         :: num_bytes_allocated
  END SUBROUTINE allocs_26
  SUBROUTINE allocs_27( grid,   id, setinitval_in ,  tl_in , inter_domain_in , okay_to_alloc_in, num_bytes_allocated ,  &
           sd31, ed31, sd32, ed32, sd33, ed33, &
           sm31 , em31 , sm32 , em32 , sm33 , em33 , &
           sp31 , ep31 , sp32 , ep32 , sp33 , ep33 , &
           sp31x, ep31x, sp32x, ep32x, sp33x, ep33x, &
           sp31y, ep31y, sp32y, ep32y, sp33y, ep33y, &
           sm31x, em31x, sm32x, em32x, sm33x, em33x, &
           sm31y, em31y, sm32y, em32y, sm33y, em33y )
    USE module_domain_type
    USE module_configure, ONLY : model_config_rec, grid_config_rec_type, in_use_for_config, model_to_grid_config_rec
    USE module_scalar_tables 
    IMPLICIT NONE
    

    TYPE(domain)               , POINTER          :: grid
    INTEGER , INTENT(IN)            :: id
    INTEGER , INTENT(IN)            :: setinitval_in   
    INTEGER , INTENT(IN)            :: sd31, ed31, sd32, ed32, sd33, ed33
    INTEGER , INTENT(IN)            :: sm31, em31, sm32, em32, sm33, em33
    INTEGER , INTENT(IN)            :: sp31, ep31, sp32, ep32, sp33, ep33
    INTEGER , INTENT(IN)            :: sp31x, ep31x, sp32x, ep32x, sp33x, ep33x
    INTEGER , INTENT(IN)            :: sp31y, ep31y, sp32y, ep32y, sp33y, ep33y
    INTEGER , INTENT(IN)            :: sm31x, em31x, sm32x, em32x, sm33x, em33x
    INTEGER , INTENT(IN)            :: sm31y, em31y, sm32y, em32y, sm33y, em33y

    
    
    
    
    INTEGER , INTENT(IN)            :: tl_in

    
    
    LOGICAL , INTENT(IN)            :: inter_domain_in, okay_to_alloc_in

    INTEGER(KIND=8) , INTENT(INOUT)         :: num_bytes_allocated
  END SUBROUTINE allocs_27
  SUBROUTINE allocs_28( grid,   id, setinitval_in ,  tl_in , inter_domain_in , okay_to_alloc_in, num_bytes_allocated ,  &
           sd31, ed31, sd32, ed32, sd33, ed33, &
           sm31 , em31 , sm32 , em32 , sm33 , em33 , &
           sp31 , ep31 , sp32 , ep32 , sp33 , ep33 , &
           sp31x, ep31x, sp32x, ep32x, sp33x, ep33x, &
           sp31y, ep31y, sp32y, ep32y, sp33y, ep33y, &
           sm31x, em31x, sm32x, em32x, sm33x, em33x, &
           sm31y, em31y, sm32y, em32y, sm33y, em33y )
    USE module_domain_type
    USE module_configure, ONLY : model_config_rec, grid_config_rec_type, in_use_for_config, model_to_grid_config_rec
    USE module_scalar_tables 
    IMPLICIT NONE
    

    TYPE(domain)               , POINTER          :: grid
    INTEGER , INTENT(IN)            :: id
    INTEGER , INTENT(IN)            :: setinitval_in   
    INTEGER , INTENT(IN)            :: sd31, ed31, sd32, ed32, sd33, ed33
    INTEGER , INTENT(IN)            :: sm31, em31, sm32, em32, sm33, em33
    INTEGER , INTENT(IN)            :: sp31, ep31, sp32, ep32, sp33, ep33
    INTEGER , INTENT(IN)            :: sp31x, ep31x, sp32x, ep32x, sp33x, ep33x
    INTEGER , INTENT(IN)            :: sp31y, ep31y, sp32y, ep32y, sp33y, ep33y
    INTEGER , INTENT(IN)            :: sm31x, em31x, sm32x, em32x, sm33x, em33x
    INTEGER , INTENT(IN)            :: sm31y, em31y, sm32y, em32y, sm33y, em33y

    
    
    
    
    INTEGER , INTENT(IN)            :: tl_in

    
    
    LOGICAL , INTENT(IN)            :: inter_domain_in, okay_to_alloc_in

    INTEGER(KIND=8) , INTENT(INOUT)         :: num_bytes_allocated
  END SUBROUTINE allocs_28
  SUBROUTINE allocs_29( grid,   id, setinitval_in ,  tl_in , inter_domain_in , okay_to_alloc_in, num_bytes_allocated ,  &
           sd31, ed31, sd32, ed32, sd33, ed33, &
           sm31 , em31 , sm32 , em32 , sm33 , em33 , &
           sp31 , ep31 , sp32 , ep32 , sp33 , ep33 , &
           sp31x, ep31x, sp32x, ep32x, sp33x, ep33x, &
           sp31y, ep31y, sp32y, ep32y, sp33y, ep33y, &
           sm31x, em31x, sm32x, em32x, sm33x, em33x, &
           sm31y, em31y, sm32y, em32y, sm33y, em33y )
    USE module_domain_type
    USE module_configure, ONLY : model_config_rec, grid_config_rec_type, in_use_for_config, model_to_grid_config_rec
    USE module_scalar_tables 
    IMPLICIT NONE
    

    TYPE(domain)               , POINTER          :: grid
    INTEGER , INTENT(IN)            :: id
    INTEGER , INTENT(IN)            :: setinitval_in   
    INTEGER , INTENT(IN)            :: sd31, ed31, sd32, ed32, sd33, ed33
    INTEGER , INTENT(IN)            :: sm31, em31, sm32, em32, sm33, em33
    INTEGER , INTENT(IN)            :: sp31, ep31, sp32, ep32, sp33, ep33
    INTEGER , INTENT(IN)            :: sp31x, ep31x, sp32x, ep32x, sp33x, ep33x
    INTEGER , INTENT(IN)            :: sp31y, ep31y, sp32y, ep32y, sp33y, ep33y
    INTEGER , INTENT(IN)            :: sm31x, em31x, sm32x, em32x, sm33x, em33x
    INTEGER , INTENT(IN)            :: sm31y, em31y, sm32y, em32y, sm33y, em33y

    
    
    
    
    INTEGER , INTENT(IN)            :: tl_in

    
    
    LOGICAL , INTENT(IN)            :: inter_domain_in, okay_to_alloc_in

    INTEGER(KIND=8) , INTENT(INOUT)         :: num_bytes_allocated
  END SUBROUTINE allocs_29
  SUBROUTINE allocs_30( grid,   id, setinitval_in ,  tl_in , inter_domain_in , okay_to_alloc_in, num_bytes_allocated ,  &
           sd31, ed31, sd32, ed32, sd33, ed33, &
           sm31 , em31 , sm32 , em32 , sm33 , em33 , &
           sp31 , ep31 , sp32 , ep32 , sp33 , ep33 , &
           sp31x, ep31x, sp32x, ep32x, sp33x, ep33x, &
           sp31y, ep31y, sp32y, ep32y, sp33y, ep33y, &
           sm31x, em31x, sm32x, em32x, sm33x, em33x, &
           sm31y, em31y, sm32y, em32y, sm33y, em33y )
    USE module_domain_type
    USE module_configure, ONLY : model_config_rec, grid_config_rec_type, in_use_for_config, model_to_grid_config_rec
    USE module_scalar_tables 
    IMPLICIT NONE
    

    TYPE(domain)               , POINTER          :: grid
    INTEGER , INTENT(IN)            :: id
    INTEGER , INTENT(IN)            :: setinitval_in   
    INTEGER , INTENT(IN)            :: sd31, ed31, sd32, ed32, sd33, ed33
    INTEGER , INTENT(IN)            :: sm31, em31, sm32, em32, sm33, em33
    INTEGER , INTENT(IN)            :: sp31, ep31, sp32, ep32, sp33, ep33
    INTEGER , INTENT(IN)            :: sp31x, ep31x, sp32x, ep32x, sp33x, ep33x
    INTEGER , INTENT(IN)            :: sp31y, ep31y, sp32y, ep32y, sp33y, ep33y
    INTEGER , INTENT(IN)            :: sm31x, em31x, sm32x, em32x, sm33x, em33x
    INTEGER , INTENT(IN)            :: sm31y, em31y, sm32y, em32y, sm33y, em33y

    
    
    
    
    INTEGER , INTENT(IN)            :: tl_in

    
    
    LOGICAL , INTENT(IN)            :: inter_domain_in, okay_to_alloc_in

    INTEGER(KIND=8) , INTENT(INOUT)         :: num_bytes_allocated
  END SUBROUTINE allocs_30
  SUBROUTINE allocs_31( grid,   id, setinitval_in ,  tl_in , inter_domain_in , okay_to_alloc_in, num_bytes_allocated ,  &
           sd31, ed31, sd32, ed32, sd33, ed33, &
           sm31 , em31 , sm32 , em32 , sm33 , em33 , &
           sp31 , ep31 , sp32 , ep32 , sp33 , ep33 , &
           sp31x, ep31x, sp32x, ep32x, sp33x, ep33x, &
           sp31y, ep31y, sp32y, ep32y, sp33y, ep33y, &
           sm31x, em31x, sm32x, em32x, sm33x, em33x, &
           sm31y, em31y, sm32y, em32y, sm33y, em33y )
    USE module_domain_type
    USE module_configure, ONLY : model_config_rec, grid_config_rec_type, in_use_for_config, model_to_grid_config_rec
    USE module_scalar_tables 
    IMPLICIT NONE
    

    TYPE(domain)               , POINTER          :: grid
    INTEGER , INTENT(IN)            :: id
    INTEGER , INTENT(IN)            :: setinitval_in   
    INTEGER , INTENT(IN)            :: sd31, ed31, sd32, ed32, sd33, ed33
    INTEGER , INTENT(IN)            :: sm31, em31, sm32, em32, sm33, em33
    INTEGER , INTENT(IN)            :: sp31, ep31, sp32, ep32, sp33, ep33
    INTEGER , INTENT(IN)            :: sp31x, ep31x, sp32x, ep32x, sp33x, ep33x
    INTEGER , INTENT(IN)            :: sp31y, ep31y, sp32y, ep32y, sp33y, ep33y
    INTEGER , INTENT(IN)            :: sm31x, em31x, sm32x, em32x, sm33x, em33x
    INTEGER , INTENT(IN)            :: sm31y, em31y, sm32y, em32y, sm33y, em33y

    
    
    
    
    INTEGER , INTENT(IN)            :: tl_in

    
    
    LOGICAL , INTENT(IN)            :: inter_domain_in, okay_to_alloc_in

    INTEGER(KIND=8) , INTENT(INOUT)         :: num_bytes_allocated
  END SUBROUTINE allocs_31
END INTERFACE



      

      TYPE(domain)               , POINTER          :: grid
      INTEGER , INTENT(IN)            :: id
      INTEGER , INTENT(IN)            :: setinitval_in   
      INTEGER , INTENT(IN)            :: sd31, ed31, sd32, ed32, sd33, ed33
      INTEGER , INTENT(IN)            :: sm31, em31, sm32, em32, sm33, em33
      INTEGER , INTENT(IN)            :: sp31, ep31, sp32, ep32, sp33, ep33
      INTEGER , INTENT(IN)            :: sp31x, ep31x, sp32x, ep32x, sp33x, ep33x
      INTEGER , INTENT(IN)            :: sp31y, ep31y, sp32y, ep32y, sp33y, ep33y
      INTEGER , INTENT(IN)            :: sm31x, em31x, sm32x, em32x, sm33x, em33x
      INTEGER , INTENT(IN)            :: sm31y, em31y, sm32y, em32y, sm33y, em33y

      
      
      
      
      INTEGER , INTENT(IN)            :: tl_in
  
      
      
      LOGICAL , INTENT(IN)            :: inter_domain_in, okay_to_alloc_in

      
      INTEGER(KIND=8)  num_bytes_allocated
      INTEGER  idum1, idum2


      IF ( grid%id .EQ. 1 ) CALL wrf_message ( &
          'DYNAMICS OPTION: Eulerian Mass Coordinate ')


      CALL set_scalar_indices_from_config( id , idum1 , idum2 )

      num_bytes_allocated = 0 

      

CALL allocs_0( grid,   id, setinitval_in ,  tl_in , inter_domain_in , okay_to_alloc_in, num_bytes_allocated ,  &
           sd31, ed31, sd32, ed32, sd33, ed33, &
           sm31 , em31 , sm32 , em32 , sm33 , em33 , &
           sp31 , ep31 , sp32 , ep32 , sp33 , ep33 , &
           sp31x, ep31x, sp32x, ep32x, sp33x, ep33x, &
           sp31y, ep31y, sp32y, ep32y, sp33y, ep33y, &
           sm31x, em31x, sm32x, em32x, sm33x, em33x, &
           sm31y, em31y, sm32y, em32y, sm33y, em33y )
CALL allocs_1( grid,   id, setinitval_in ,  tl_in , inter_domain_in , okay_to_alloc_in, num_bytes_allocated ,  &
           sd31, ed31, sd32, ed32, sd33, ed33, &
           sm31 , em31 , sm32 , em32 , sm33 , em33 , &
           sp31 , ep31 , sp32 , ep32 , sp33 , ep33 , &
           sp31x, ep31x, sp32x, ep32x, sp33x, ep33x, &
           sp31y, ep31y, sp32y, ep32y, sp33y, ep33y, &
           sm31x, em31x, sm32x, em32x, sm33x, em33x, &
           sm31y, em31y, sm32y, em32y, sm33y, em33y )
CALL allocs_2( grid,   id, setinitval_in ,  tl_in , inter_domain_in , okay_to_alloc_in, num_bytes_allocated ,  &
           sd31, ed31, sd32, ed32, sd33, ed33, &
           sm31 , em31 , sm32 , em32 , sm33 , em33 , &
           sp31 , ep31 , sp32 , ep32 , sp33 , ep33 , &
           sp31x, ep31x, sp32x, ep32x, sp33x, ep33x, &
           sp31y, ep31y, sp32y, ep32y, sp33y, ep33y, &
           sm31x, em31x, sm32x, em32x, sm33x, em33x, &
           sm31y, em31y, sm32y, em32y, sm33y, em33y )
CALL allocs_3( grid,   id, setinitval_in ,  tl_in , inter_domain_in , okay_to_alloc_in, num_bytes_allocated ,  &
           sd31, ed31, sd32, ed32, sd33, ed33, &
           sm31 , em31 , sm32 , em32 , sm33 , em33 , &
           sp31 , ep31 , sp32 , ep32 , sp33 , ep33 , &
           sp31x, ep31x, sp32x, ep32x, sp33x, ep33x, &
           sp31y, ep31y, sp32y, ep32y, sp33y, ep33y, &
           sm31x, em31x, sm32x, em32x, sm33x, em33x, &
           sm31y, em31y, sm32y, em32y, sm33y, em33y )
CALL allocs_4( grid,   id, setinitval_in ,  tl_in , inter_domain_in , okay_to_alloc_in, num_bytes_allocated ,  &
           sd31, ed31, sd32, ed32, sd33, ed33, &
           sm31 , em31 , sm32 , em32 , sm33 , em33 , &
           sp31 , ep31 , sp32 , ep32 , sp33 , ep33 , &
           sp31x, ep31x, sp32x, ep32x, sp33x, ep33x, &
           sp31y, ep31y, sp32y, ep32y, sp33y, ep33y, &
           sm31x, em31x, sm32x, em32x, sm33x, em33x, &
           sm31y, em31y, sm32y, em32y, sm33y, em33y )
CALL allocs_5( grid,   id, setinitval_in ,  tl_in , inter_domain_in , okay_to_alloc_in, num_bytes_allocated ,  &
           sd31, ed31, sd32, ed32, sd33, ed33, &
           sm31 , em31 , sm32 , em32 , sm33 , em33 , &
           sp31 , ep31 , sp32 , ep32 , sp33 , ep33 , &
           sp31x, ep31x, sp32x, ep32x, sp33x, ep33x, &
           sp31y, ep31y, sp32y, ep32y, sp33y, ep33y, &
           sm31x, em31x, sm32x, em32x, sm33x, em33x, &
           sm31y, em31y, sm32y, em32y, sm33y, em33y )
CALL allocs_6( grid,   id, setinitval_in ,  tl_in , inter_domain_in , okay_to_alloc_in, num_bytes_allocated ,  &
           sd31, ed31, sd32, ed32, sd33, ed33, &
           sm31 , em31 , sm32 , em32 , sm33 , em33 , &
           sp31 , ep31 , sp32 , ep32 , sp33 , ep33 , &
           sp31x, ep31x, sp32x, ep32x, sp33x, ep33x, &
           sp31y, ep31y, sp32y, ep32y, sp33y, ep33y, &
           sm31x, em31x, sm32x, em32x, sm33x, em33x, &
           sm31y, em31y, sm32y, em32y, sm33y, em33y )
CALL allocs_7( grid,   id, setinitval_in ,  tl_in , inter_domain_in , okay_to_alloc_in, num_bytes_allocated ,  &
           sd31, ed31, sd32, ed32, sd33, ed33, &
           sm31 , em31 , sm32 , em32 , sm33 , em33 , &
           sp31 , ep31 , sp32 , ep32 , sp33 , ep33 , &
           sp31x, ep31x, sp32x, ep32x, sp33x, ep33x, &
           sp31y, ep31y, sp32y, ep32y, sp33y, ep33y, &
           sm31x, em31x, sm32x, em32x, sm33x, em33x, &
           sm31y, em31y, sm32y, em32y, sm33y, em33y )
CALL allocs_8( grid,   id, setinitval_in ,  tl_in , inter_domain_in , okay_to_alloc_in, num_bytes_allocated ,  &
           sd31, ed31, sd32, ed32, sd33, ed33, &
           sm31 , em31 , sm32 , em32 , sm33 , em33 , &
           sp31 , ep31 , sp32 , ep32 , sp33 , ep33 , &
           sp31x, ep31x, sp32x, ep32x, sp33x, ep33x, &
           sp31y, ep31y, sp32y, ep32y, sp33y, ep33y, &
           sm31x, em31x, sm32x, em32x, sm33x, em33x, &
           sm31y, em31y, sm32y, em32y, sm33y, em33y )
CALL allocs_9( grid,   id, setinitval_in ,  tl_in , inter_domain_in , okay_to_alloc_in, num_bytes_allocated ,  &
           sd31, ed31, sd32, ed32, sd33, ed33, &
           sm31 , em31 , sm32 , em32 , sm33 , em33 , &
           sp31 , ep31 , sp32 , ep32 , sp33 , ep33 , &
           sp31x, ep31x, sp32x, ep32x, sp33x, ep33x, &
           sp31y, ep31y, sp32y, ep32y, sp33y, ep33y, &
           sm31x, em31x, sm32x, em32x, sm33x, em33x, &
           sm31y, em31y, sm32y, em32y, sm33y, em33y )
CALL allocs_10( grid,   id, setinitval_in ,  tl_in , inter_domain_in , okay_to_alloc_in, num_bytes_allocated ,  &
           sd31, ed31, sd32, ed32, sd33, ed33, &
           sm31 , em31 , sm32 , em32 , sm33 , em33 , &
           sp31 , ep31 , sp32 , ep32 , sp33 , ep33 , &
           sp31x, ep31x, sp32x, ep32x, sp33x, ep33x, &
           sp31y, ep31y, sp32y, ep32y, sp33y, ep33y, &
           sm31x, em31x, sm32x, em32x, sm33x, em33x, &
           sm31y, em31y, sm32y, em32y, sm33y, em33y )
CALL allocs_11( grid,   id, setinitval_in ,  tl_in , inter_domain_in , okay_to_alloc_in, num_bytes_allocated ,  &
           sd31, ed31, sd32, ed32, sd33, ed33, &
           sm31 , em31 , sm32 , em32 , sm33 , em33 , &
           sp31 , ep31 , sp32 , ep32 , sp33 , ep33 , &
           sp31x, ep31x, sp32x, ep32x, sp33x, ep33x, &
           sp31y, ep31y, sp32y, ep32y, sp33y, ep33y, &
           sm31x, em31x, sm32x, em32x, sm33x, em33x, &
           sm31y, em31y, sm32y, em32y, sm33y, em33y )
CALL allocs_12( grid,   id, setinitval_in ,  tl_in , inter_domain_in , okay_to_alloc_in, num_bytes_allocated ,  &
           sd31, ed31, sd32, ed32, sd33, ed33, &
           sm31 , em31 , sm32 , em32 , sm33 , em33 , &
           sp31 , ep31 , sp32 , ep32 , sp33 , ep33 , &
           sp31x, ep31x, sp32x, ep32x, sp33x, ep33x, &
           sp31y, ep31y, sp32y, ep32y, sp33y, ep33y, &
           sm31x, em31x, sm32x, em32x, sm33x, em33x, &
           sm31y, em31y, sm32y, em32y, sm33y, em33y )
CALL allocs_13( grid,   id, setinitval_in ,  tl_in , inter_domain_in , okay_to_alloc_in, num_bytes_allocated ,  &
           sd31, ed31, sd32, ed32, sd33, ed33, &
           sm31 , em31 , sm32 , em32 , sm33 , em33 , &
           sp31 , ep31 , sp32 , ep32 , sp33 , ep33 , &
           sp31x, ep31x, sp32x, ep32x, sp33x, ep33x, &
           sp31y, ep31y, sp32y, ep32y, sp33y, ep33y, &
           sm31x, em31x, sm32x, em32x, sm33x, em33x, &
           sm31y, em31y, sm32y, em32y, sm33y, em33y )
CALL allocs_14( grid,   id, setinitval_in ,  tl_in , inter_domain_in , okay_to_alloc_in, num_bytes_allocated ,  &
           sd31, ed31, sd32, ed32, sd33, ed33, &
           sm31 , em31 , sm32 , em32 , sm33 , em33 , &
           sp31 , ep31 , sp32 , ep32 , sp33 , ep33 , &
           sp31x, ep31x, sp32x, ep32x, sp33x, ep33x, &
           sp31y, ep31y, sp32y, ep32y, sp33y, ep33y, &
           sm31x, em31x, sm32x, em32x, sm33x, em33x, &
           sm31y, em31y, sm32y, em32y, sm33y, em33y )
CALL allocs_15( grid,   id, setinitval_in ,  tl_in , inter_domain_in , okay_to_alloc_in, num_bytes_allocated ,  &
           sd31, ed31, sd32, ed32, sd33, ed33, &
           sm31 , em31 , sm32 , em32 , sm33 , em33 , &
           sp31 , ep31 , sp32 , ep32 , sp33 , ep33 , &
           sp31x, ep31x, sp32x, ep32x, sp33x, ep33x, &
           sp31y, ep31y, sp32y, ep32y, sp33y, ep33y, &
           sm31x, em31x, sm32x, em32x, sm33x, em33x, &
           sm31y, em31y, sm32y, em32y, sm33y, em33y )
CALL allocs_16( grid,   id, setinitval_in ,  tl_in , inter_domain_in , okay_to_alloc_in, num_bytes_allocated ,  &
           sd31, ed31, sd32, ed32, sd33, ed33, &
           sm31 , em31 , sm32 , em32 , sm33 , em33 , &
           sp31 , ep31 , sp32 , ep32 , sp33 , ep33 , &
           sp31x, ep31x, sp32x, ep32x, sp33x, ep33x, &
           sp31y, ep31y, sp32y, ep32y, sp33y, ep33y, &
           sm31x, em31x, sm32x, em32x, sm33x, em33x, &
           sm31y, em31y, sm32y, em32y, sm33y, em33y )
CALL allocs_17( grid,   id, setinitval_in ,  tl_in , inter_domain_in , okay_to_alloc_in, num_bytes_allocated ,  &
           sd31, ed31, sd32, ed32, sd33, ed33, &
           sm31 , em31 , sm32 , em32 , sm33 , em33 , &
           sp31 , ep31 , sp32 , ep32 , sp33 , ep33 , &
           sp31x, ep31x, sp32x, ep32x, sp33x, ep33x, &
           sp31y, ep31y, sp32y, ep32y, sp33y, ep33y, &
           sm31x, em31x, sm32x, em32x, sm33x, em33x, &
           sm31y, em31y, sm32y, em32y, sm33y, em33y )
CALL allocs_18( grid,   id, setinitval_in ,  tl_in , inter_domain_in , okay_to_alloc_in, num_bytes_allocated ,  &
           sd31, ed31, sd32, ed32, sd33, ed33, &
           sm31 , em31 , sm32 , em32 , sm33 , em33 , &
           sp31 , ep31 , sp32 , ep32 , sp33 , ep33 , &
           sp31x, ep31x, sp32x, ep32x, sp33x, ep33x, &
           sp31y, ep31y, sp32y, ep32y, sp33y, ep33y, &
           sm31x, em31x, sm32x, em32x, sm33x, em33x, &
           sm31y, em31y, sm32y, em32y, sm33y, em33y )
CALL allocs_19( grid,   id, setinitval_in ,  tl_in , inter_domain_in , okay_to_alloc_in, num_bytes_allocated ,  &
           sd31, ed31, sd32, ed32, sd33, ed33, &
           sm31 , em31 , sm32 , em32 , sm33 , em33 , &
           sp31 , ep31 , sp32 , ep32 , sp33 , ep33 , &
           sp31x, ep31x, sp32x, ep32x, sp33x, ep33x, &
           sp31y, ep31y, sp32y, ep32y, sp33y, ep33y, &
           sm31x, em31x, sm32x, em32x, sm33x, em33x, &
           sm31y, em31y, sm32y, em32y, sm33y, em33y )
CALL allocs_20( grid,   id, setinitval_in ,  tl_in , inter_domain_in , okay_to_alloc_in, num_bytes_allocated ,  &
           sd31, ed31, sd32, ed32, sd33, ed33, &
           sm31 , em31 , sm32 , em32 , sm33 , em33 , &
           sp31 , ep31 , sp32 , ep32 , sp33 , ep33 , &
           sp31x, ep31x, sp32x, ep32x, sp33x, ep33x, &
           sp31y, ep31y, sp32y, ep32y, sp33y, ep33y, &
           sm31x, em31x, sm32x, em32x, sm33x, em33x, &
           sm31y, em31y, sm32y, em32y, sm33y, em33y )
CALL allocs_21( grid,   id, setinitval_in ,  tl_in , inter_domain_in , okay_to_alloc_in, num_bytes_allocated ,  &
           sd31, ed31, sd32, ed32, sd33, ed33, &
           sm31 , em31 , sm32 , em32 , sm33 , em33 , &
           sp31 , ep31 , sp32 , ep32 , sp33 , ep33 , &
           sp31x, ep31x, sp32x, ep32x, sp33x, ep33x, &
           sp31y, ep31y, sp32y, ep32y, sp33y, ep33y, &
           sm31x, em31x, sm32x, em32x, sm33x, em33x, &
           sm31y, em31y, sm32y, em32y, sm33y, em33y )
CALL allocs_22( grid,   id, setinitval_in ,  tl_in , inter_domain_in , okay_to_alloc_in, num_bytes_allocated ,  &
           sd31, ed31, sd32, ed32, sd33, ed33, &
           sm31 , em31 , sm32 , em32 , sm33 , em33 , &
           sp31 , ep31 , sp32 , ep32 , sp33 , ep33 , &
           sp31x, ep31x, sp32x, ep32x, sp33x, ep33x, &
           sp31y, ep31y, sp32y, ep32y, sp33y, ep33y, &
           sm31x, em31x, sm32x, em32x, sm33x, em33x, &
           sm31y, em31y, sm32y, em32y, sm33y, em33y )
CALL allocs_23( grid,   id, setinitval_in ,  tl_in , inter_domain_in , okay_to_alloc_in, num_bytes_allocated ,  &
           sd31, ed31, sd32, ed32, sd33, ed33, &
           sm31 , em31 , sm32 , em32 , sm33 , em33 , &
           sp31 , ep31 , sp32 , ep32 , sp33 , ep33 , &
           sp31x, ep31x, sp32x, ep32x, sp33x, ep33x, &
           sp31y, ep31y, sp32y, ep32y, sp33y, ep33y, &
           sm31x, em31x, sm32x, em32x, sm33x, em33x, &
           sm31y, em31y, sm32y, em32y, sm33y, em33y )
CALL allocs_24( grid,   id, setinitval_in ,  tl_in , inter_domain_in , okay_to_alloc_in, num_bytes_allocated ,  &
           sd31, ed31, sd32, ed32, sd33, ed33, &
           sm31 , em31 , sm32 , em32 , sm33 , em33 , &
           sp31 , ep31 , sp32 , ep32 , sp33 , ep33 , &
           sp31x, ep31x, sp32x, ep32x, sp33x, ep33x, &
           sp31y, ep31y, sp32y, ep32y, sp33y, ep33y, &
           sm31x, em31x, sm32x, em32x, sm33x, em33x, &
           sm31y, em31y, sm32y, em32y, sm33y, em33y )
CALL allocs_25( grid,   id, setinitval_in ,  tl_in , inter_domain_in , okay_to_alloc_in, num_bytes_allocated ,  &
           sd31, ed31, sd32, ed32, sd33, ed33, &
           sm31 , em31 , sm32 , em32 , sm33 , em33 , &
           sp31 , ep31 , sp32 , ep32 , sp33 , ep33 , &
           sp31x, ep31x, sp32x, ep32x, sp33x, ep33x, &
           sp31y, ep31y, sp32y, ep32y, sp33y, ep33y, &
           sm31x, em31x, sm32x, em32x, sm33x, em33x, &
           sm31y, em31y, sm32y, em32y, sm33y, em33y )
CALL allocs_26( grid,   id, setinitval_in ,  tl_in , inter_domain_in , okay_to_alloc_in, num_bytes_allocated ,  &
           sd31, ed31, sd32, ed32, sd33, ed33, &
           sm31 , em31 , sm32 , em32 , sm33 , em33 , &
           sp31 , ep31 , sp32 , ep32 , sp33 , ep33 , &
           sp31x, ep31x, sp32x, ep32x, sp33x, ep33x, &
           sp31y, ep31y, sp32y, ep32y, sp33y, ep33y, &
           sm31x, em31x, sm32x, em32x, sm33x, em33x, &
           sm31y, em31y, sm32y, em32y, sm33y, em33y )
CALL allocs_27( grid,   id, setinitval_in ,  tl_in , inter_domain_in , okay_to_alloc_in, num_bytes_allocated ,  &
           sd31, ed31, sd32, ed32, sd33, ed33, &
           sm31 , em31 , sm32 , em32 , sm33 , em33 , &
           sp31 , ep31 , sp32 , ep32 , sp33 , ep33 , &
           sp31x, ep31x, sp32x, ep32x, sp33x, ep33x, &
           sp31y, ep31y, sp32y, ep32y, sp33y, ep33y, &
           sm31x, em31x, sm32x, em32x, sm33x, em33x, &
           sm31y, em31y, sm32y, em32y, sm33y, em33y )
CALL allocs_28( grid,   id, setinitval_in ,  tl_in , inter_domain_in , okay_to_alloc_in, num_bytes_allocated ,  &
           sd31, ed31, sd32, ed32, sd33, ed33, &
           sm31 , em31 , sm32 , em32 , sm33 , em33 , &
           sp31 , ep31 , sp32 , ep32 , sp33 , ep33 , &
           sp31x, ep31x, sp32x, ep32x, sp33x, ep33x, &
           sp31y, ep31y, sp32y, ep32y, sp33y, ep33y, &
           sm31x, em31x, sm32x, em32x, sm33x, em33x, &
           sm31y, em31y, sm32y, em32y, sm33y, em33y )
CALL allocs_29( grid,   id, setinitval_in ,  tl_in , inter_domain_in , okay_to_alloc_in, num_bytes_allocated ,  &
           sd31, ed31, sd32, ed32, sd33, ed33, &
           sm31 , em31 , sm32 , em32 , sm33 , em33 , &
           sp31 , ep31 , sp32 , ep32 , sp33 , ep33 , &
           sp31x, ep31x, sp32x, ep32x, sp33x, ep33x, &
           sp31y, ep31y, sp32y, ep32y, sp33y, ep33y, &
           sm31x, em31x, sm32x, em32x, sm33x, em33x, &
           sm31y, em31y, sm32y, em32y, sm33y, em33y )
CALL allocs_30( grid,   id, setinitval_in ,  tl_in , inter_domain_in , okay_to_alloc_in, num_bytes_allocated ,  &
           sd31, ed31, sd32, ed32, sd33, ed33, &
           sm31 , em31 , sm32 , em32 , sm33 , em33 , &
           sp31 , ep31 , sp32 , ep32 , sp33 , ep33 , &
           sp31x, ep31x, sp32x, ep32x, sp33x, ep33x, &
           sp31y, ep31y, sp32y, ep32y, sp33y, ep33y, &
           sm31x, em31x, sm32x, em32x, sm33x, em33x, &
           sm31y, em31y, sm32y, em32y, sm33y, em33y )
CALL allocs_31( grid,   id, setinitval_in ,  tl_in , inter_domain_in , okay_to_alloc_in, num_bytes_allocated ,  &
           sd31, ed31, sd32, ed32, sd33, ed33, &
           sm31 , em31 , sm32 , em32 , sm33 , em33 , &
           sp31 , ep31 , sp32 , ep32 , sp33 , ep33 , &
           sp31x, ep31x, sp32x, ep32x, sp33x, ep33x, &
           sp31y, ep31y, sp32y, ep32y, sp33y, ep33y, &
           sm31x, em31x, sm32x, em32x, sm33x, em33x, &
           sm31y, em31y, sm32y, em32y, sm33y, em33y )



      IF ( .NOT. grid%have_displayed_alloc_stats ) THEN
        
        
        WRITE(wrf_err_message,*)&
            'alloc_space_field: domain ',id,', ',num_bytes_allocated,' bytes allocated'
        CALL  wrf_debug( 0, wrf_err_message )
        grid%have_displayed_alloc_stats = .TRUE.   
      ENDIF


      grid%alloced_sd31=sd31
      grid%alloced_ed31=ed31
      grid%alloced_sd32=sd32
      grid%alloced_ed32=ed32
      grid%alloced_sd33=sd33
      grid%alloced_ed33=ed33
      grid%alloced_sm31=sm31
      grid%alloced_em31=em31
      grid%alloced_sm32=sm32
      grid%alloced_em32=em32
      grid%alloced_sm33=sm33
      grid%alloced_em33=em33
      grid%alloced_sm31x=sm31x
      grid%alloced_em31x=em31x
      grid%alloced_sm32x=sm32x
      grid%alloced_em32x=em32x
      grid%alloced_sm33x=sm33x
      grid%alloced_em33x=em33x
      grid%alloced_sm31y=sm31y
      grid%alloced_em31y=em31y
      grid%alloced_sm32y=sm32y
      grid%alloced_em32y=em32y
      grid%alloced_sm33y=sm33y
      grid%alloced_em33y=em33y

      grid%allocated=.TRUE.

   END SUBROUTINE alloc_space_field

   
   
   
   
   

   SUBROUTINE ensure_space_field ( grid,   id, setinitval_in ,  tl_in , inter_domain_in , okay_to_alloc_in,  &
                                  sd31, ed31, sd32, ed32, sd33, ed33, &
                                  sm31 , em31 , sm32 , em32 , sm33 , em33 , &
                                  sp31 , ep31 , sp32 , ep32 , sp33 , ep33 , &
                                  sp31x, ep31x, sp32x, ep32x, sp33x, ep33x, &
                                  sp31y, ep31y, sp32y, ep32y, sp33y, ep33y, &
                                  sm31x, em31x, sm32x, em32x, sm33x, em33x, &
                                  sm31y, em31y, sm32y, em32y, sm33y, em33y )

      IMPLICIT NONE

      

      TYPE(domain)               , POINTER          :: grid
      INTEGER , INTENT(IN)            :: id
      INTEGER , INTENT(IN)            :: setinitval_in   
      INTEGER , INTENT(IN)            :: sd31, ed31, sd32, ed32, sd33, ed33
      INTEGER , INTENT(IN)            :: sm31, em31, sm32, em32, sm33, em33
      INTEGER , INTENT(IN)            :: sp31, ep31, sp32, ep32, sp33, ep33
      INTEGER , INTENT(IN)            :: sp31x, ep31x, sp32x, ep32x, sp33x, ep33x
      INTEGER , INTENT(IN)            :: sp31y, ep31y, sp32y, ep32y, sp33y, ep33y
      INTEGER , INTENT(IN)            :: sm31x, em31x, sm32x, em32x, sm33x, em33x
      INTEGER , INTENT(IN)            :: sm31y, em31y, sm32y, em32y, sm33y, em33y

      
      
      
      
      INTEGER , INTENT(IN)            :: tl_in
  
      
      
      LOGICAL , INTENT(IN)            :: inter_domain_in, okay_to_alloc_in
      LOGICAL                         :: size_changed

      size_changed=         .not. ( &
         grid%alloced_sd31 .eq. sd31 .and. grid%alloced_ed31 .eq. ed31 .and. &
         grid%alloced_sd32 .eq. sd32 .and. grid%alloced_ed32 .eq. ed32 .and. &
         grid%alloced_sd33 .eq. sd33 .and. grid%alloced_ed33 .eq. ed33 .and. &
         grid%alloced_sm31 .eq. sm31 .and. grid%alloced_em31 .eq. em31 .and. &
         grid%alloced_sm32 .eq. sm32 .and. grid%alloced_em32 .eq. em32 .and. &
         grid%alloced_sm33 .eq. sm33 .and. grid%alloced_em33 .eq. em33 .and. &
         grid%alloced_sm31x .eq. sm31x .and. grid%alloced_em31x .eq. em31x .and. &
         grid%alloced_sm32x .eq. sm32x .and. grid%alloced_em32x .eq. em32x .and. &
         grid%alloced_sm33x .eq. sm33x .and. grid%alloced_em33x .eq. em33x .and. &
         grid%alloced_sm31y .eq. sm31y .and. grid%alloced_em31y .eq. em31y .and. &
         grid%alloced_sm32y .eq. sm32y .and. grid%alloced_em32y .eq. em32y .and. &
         grid%alloced_sm33y .eq. sm33y .and. grid%alloced_em33y .eq. em33y &
      )
      if(.not. grid%allocated .or. size_changed) then
         if(.not. grid%allocated) then
            call wrf_debug(1,'ensure_space_field: calling alloc_space_field because a grid was not allocated.')
         else
            if(size_changed) &
                 call wrf_debug(1,'ensure_space_field: deallocating and reallocating a grid because grid size changed.')
         end if
         if(grid%allocated) &
              call dealloc_space_field( grid )
         call alloc_space_field ( grid,   id, setinitval_in ,  tl_in , inter_domain_in , okay_to_alloc_in,  &
                                  sd31, ed31, sd32, ed32, sd33, ed33, &
                                  sm31 , em31 , sm32 , em32 , sm33 , em33 , &
                                  sp31 , ep31 , sp32 , ep32 , sp33 , ep33 , &
                                  sp31x, ep31x, sp32x, ep32x, sp33x, ep33x, &
                                  sp31y, ep31y, sp32y, ep32y, sp33y, ep33y, &
                                  sm31x, em31x, sm32x, em32x, sm33x, em33x, &
                                  sm31y, em31y, sm32y, em32y, sm33y, em33y )
      end if

   END SUBROUTINE ensure_space_field






   SUBROUTINE dealloc_space_domain ( id )
      
      IMPLICIT NONE

      

      INTEGER , INTENT(IN)            :: id

      

      TYPE(domain) , POINTER          :: grid
      LOGICAL                         :: found

      

      grid => head_grid
      old_grid => head_grid
      found = .FALSE.

      
      
      

      find_grid : DO WHILE ( ASSOCIATED(grid) ) 
         IF ( grid%id == id ) THEN
            found = .TRUE.
            old_grid%next => grid%next
            CALL domain_destroy( grid )
            EXIT find_grid
         END IF
         old_grid => grid
         grid     => grid%next
      END DO find_grid

      IF ( .NOT. found ) THEN
         WRITE ( wrf_err_message , * ) 'module_domain: ', &
           'dealloc_space_domain: Could not de-allocate grid id ',id
         CALL wrf_error_fatal3("<stdin>",2893,&
TRIM( wrf_err_message ) ) 
      END IF

   END SUBROUTINE dealloc_space_domain








   SUBROUTINE domain_destroy ( grid )
      
      IMPLICIT NONE

      

      TYPE(domain) , POINTER          :: grid

      CALL dealloc_space_field ( grid )
      CALL dealloc_linked_lists( grid )
      DEALLOCATE( grid%parents )
      DEALLOCATE( grid%nests )
      
      CALL domain_clock_destroy( grid )
      CALL domain_alarms_destroy( grid )
      IF ( ASSOCIATED( grid%i_start ) ) THEN
        DEALLOCATE( grid%i_start ) 
      ENDIF
      IF ( ASSOCIATED( grid%i_end ) ) THEN
        DEALLOCATE( grid%i_end )
      ENDIF
      IF ( ASSOCIATED( grid%j_start ) ) THEN
        DEALLOCATE( grid%j_start )
      ENDIF
      IF ( ASSOCIATED( grid%j_end ) ) THEN
        DEALLOCATE( grid%j_end )
      ENDIF
      IF ( ASSOCIATED( grid%itsloc ) ) THEN
        DEALLOCATE( grid%itsloc )
      ENDIF 
      IF ( ASSOCIATED( grid%jtsloc ) ) THEN
        DEALLOCATE( grid%jtsloc )
      ENDIF 
      IF ( ASSOCIATED( grid%id_tsloc ) ) THEN
        DEALLOCATE( grid%id_tsloc )
      ENDIF 
      IF ( ASSOCIATED( grid%lattsloc ) ) THEN
        DEALLOCATE( grid%lattsloc )
      ENDIF 
      IF ( ASSOCIATED( grid%lontsloc ) ) THEN
        DEALLOCATE( grid%lontsloc )
      ENDIF 
      IF ( ASSOCIATED( grid%nametsloc ) ) THEN
        DEALLOCATE( grid%nametsloc )
      ENDIF 
      IF ( ASSOCIATED( grid%desctsloc ) ) THEN
        DEALLOCATE( grid%desctsloc )
      ENDIF 
      IF ( ASSOCIATED( grid%ts_filename ) ) THEN
        DEALLOCATE( grid%ts_filename )
      ENDIF 

      IF ( ASSOCIATED( grid%track_time_in ) ) THEN
        DEALLOCATE( grid%track_time_in )
      ENDIF
 
      IF ( ASSOCIATED( grid%track_lat_in ) ) THEN
        DEALLOCATE( grid%track_lat_in )
      ENDIF
 
      IF ( ASSOCIATED( grid%track_lon_in ) ) THEN
        DEALLOCATE( grid%track_lon_in )
      ENDIF
 
      IF ( ASSOCIATED( grid%track_i ) ) THEN
        DEALLOCATE( grid%track_i )
      ENDIF
 
      IF ( ASSOCIATED( grid%track_j ) ) THEN
        DEALLOCATE( grid%track_j )
      ENDIF

      IF ( ASSOCIATED( grid%track_time_domain ) ) THEN
        DEALLOCATE( grid%track_time_domain )
      ENDIF
 
      IF ( ASSOCIATED( grid%track_lat_domain ) ) THEN
        DEALLOCATE( grid%track_lat_domain )
      ENDIF
 
      IF ( ASSOCIATED( grid%track_lon_domain ) ) THEN
        DEALLOCATE( grid%track_lon_domain )
      ENDIF

      DEALLOCATE( grid )
      NULLIFY( grid )

   END SUBROUTINE domain_destroy

   SUBROUTINE dealloc_linked_lists ( grid )
      IMPLICIT NONE
      TYPE(domain), POINTER :: grid
      TYPE(fieldlist), POINTER :: p, q
      p => grid%head_statevars
      DO WHILE ( ASSOCIATED( p ) )
        if (p%varname.eq."chem_ic")  exit
         q => p ; p => p%next ; DEALLOCATE(q)
      ENDDO
      NULLIFY(grid%head_statevars) ; NULLIFY( grid%tail_statevars)

      IF ( .NOT. grid%is_intermediate ) THEN
        ALLOCATE( grid%head_statevars )
        NULLIFY( grid%head_statevars%next)
        grid%tail_statevars => grid%head_statevars
      ENDIF

   END SUBROUTINE dealloc_linked_lists

   RECURSIVE SUBROUTINE show_nest_subtree ( grid )
      TYPE(domain), POINTER :: grid
      INTEGER myid
      INTEGER kid
      IF ( .NOT. ASSOCIATED( grid ) ) RETURN
      myid = grid%id
      DO kid = 1, max_nests
        IF ( ASSOCIATED( grid%nests(kid)%ptr ) ) THEN
          IF ( grid%nests(kid)%ptr%id .EQ. myid ) THEN
            CALL wrf_error_fatal3("<stdin>",3023,&
'show_nest_subtree: nest hierarchy corrupted' )
          ENDIF
          CALL show_nest_subtree( grid%nests(kid)%ptr )
        ENDIF
      ENDDO
   END SUBROUTINE show_nest_subtree
   







   SUBROUTINE dealloc_space_field ( grid )
      
      IMPLICIT NONE

      

      TYPE(domain)              , POINTER :: grid








INTERFACE
  SUBROUTINE deallocs_0( grid )
    USE module_wrf_error
    USE module_domain_type
    IMPLICIT NONE
    TYPE( domain ), POINTER :: grid
  END SUBROUTINE
  SUBROUTINE deallocs_1( grid )
    USE module_wrf_error
    USE module_domain_type
    IMPLICIT NONE
    TYPE( domain ), POINTER :: grid
  END SUBROUTINE
  SUBROUTINE deallocs_2( grid )
    USE module_wrf_error
    USE module_domain_type
    IMPLICIT NONE
    TYPE( domain ), POINTER :: grid
  END SUBROUTINE
  SUBROUTINE deallocs_3( grid )
    USE module_wrf_error
    USE module_domain_type
    IMPLICIT NONE
    TYPE( domain ), POINTER :: grid
  END SUBROUTINE
  SUBROUTINE deallocs_4( grid )
    USE module_wrf_error
    USE module_domain_type
    IMPLICIT NONE
    TYPE( domain ), POINTER :: grid
  END SUBROUTINE
  SUBROUTINE deallocs_5( grid )
    USE module_wrf_error
    USE module_domain_type
    IMPLICIT NONE
    TYPE( domain ), POINTER :: grid
  END SUBROUTINE
  SUBROUTINE deallocs_6( grid )
    USE module_wrf_error
    USE module_domain_type
    IMPLICIT NONE
    TYPE( domain ), POINTER :: grid
  END SUBROUTINE
  SUBROUTINE deallocs_7( grid )
    USE module_wrf_error
    USE module_domain_type
    IMPLICIT NONE
    TYPE( domain ), POINTER :: grid
  END SUBROUTINE
  SUBROUTINE deallocs_8( grid )
    USE module_wrf_error
    USE module_domain_type
    IMPLICIT NONE
    TYPE( domain ), POINTER :: grid
  END SUBROUTINE
  SUBROUTINE deallocs_9( grid )
    USE module_wrf_error
    USE module_domain_type
    IMPLICIT NONE
    TYPE( domain ), POINTER :: grid
  END SUBROUTINE
  SUBROUTINE deallocs_10( grid )
    USE module_wrf_error
    USE module_domain_type
    IMPLICIT NONE
    TYPE( domain ), POINTER :: grid
  END SUBROUTINE
  SUBROUTINE deallocs_11( grid )
    USE module_wrf_error
    USE module_domain_type
    IMPLICIT NONE
    TYPE( domain ), POINTER :: grid
  END SUBROUTINE
END INTERFACE
CALL deallocs_0( grid )
CALL deallocs_1( grid )
CALL deallocs_2( grid )
CALL deallocs_3( grid )
CALL deallocs_4( grid )
CALL deallocs_5( grid )
CALL deallocs_6( grid )
CALL deallocs_7( grid )
CALL deallocs_8( grid )
CALL deallocs_9( grid )
CALL deallocs_10( grid )
CALL deallocs_11( grid )



   END SUBROUTINE dealloc_space_field



   RECURSIVE SUBROUTINE find_grid_by_id ( id, in_grid, result_grid )
      IMPLICIT NONE
      INTEGER, INTENT(IN) :: id
      TYPE(domain), POINTER     :: in_grid 
      TYPE(domain), POINTER     :: result_grid






      TYPE(domain), POINTER     :: grid_ptr
      INTEGER                   :: kid
      LOGICAL                   :: found
      found = .FALSE.
      NULLIFY(result_grid)
      IF ( ASSOCIATED( in_grid ) ) THEN
        IF ( in_grid%id .EQ. id ) THEN
           result_grid => in_grid
        ELSE
           grid_ptr => in_grid
           DO WHILE ( ASSOCIATED( grid_ptr ) .AND. .NOT. found )
              DO kid = 1, max_nests
                 IF ( ASSOCIATED( grid_ptr%nests(kid)%ptr ) .AND. .NOT. found ) THEN
                    CALL find_grid_by_id ( id, grid_ptr%nests(kid)%ptr, result_grid )
                    IF ( ASSOCIATED( result_grid ) ) THEN
                      IF ( result_grid%id .EQ. id ) found = .TRUE.
                    ENDIF
                 ENDIF
              ENDDO
              IF ( .NOT. found ) grid_ptr => grid_ptr%sibling
           ENDDO
        ENDIF
      ENDIF
      RETURN
   END SUBROUTINE find_grid_by_id


   FUNCTION first_loc_integer ( array , search ) RESULT ( loc ) 
 
      IMPLICIT NONE

      

      INTEGER , INTENT(IN) , DIMENSION(:) :: array
      INTEGER , INTENT(IN)                :: search

      

      INTEGER                             :: loc






      
      

      INTEGER :: loop

      loc = -1
      find : DO loop = 1 , SIZE(array)
         IF ( search == array(loop) ) THEN         
            loc = loop
            EXIT find
         END IF
      END DO find

   END FUNCTION first_loc_integer

   SUBROUTINE init_module_domain
   END SUBROUTINE init_module_domain










      FUNCTION domain_get_current_time ( grid ) RESULT ( current_time ) 
        IMPLICIT NONE




        TYPE(domain), INTENT(IN) :: grid
        
        TYPE(WRFU_Time) :: current_time
        
        INTEGER :: rc
        CALL WRFU_ClockGet( grid%domain_clock, CurrTime=current_time, &
                            rc=rc )
        IF ( rc /= WRFU_SUCCESS ) THEN
          CALL wrf_error_fatal3("<stdin>",3243,&
            'domain_get_current_time:  WRFU_ClockGet failed' )
        ENDIF
      END FUNCTION domain_get_current_time


      FUNCTION domain_get_start_time ( grid ) RESULT ( start_time ) 
        IMPLICIT NONE




        TYPE(domain), INTENT(IN) :: grid
        
        TYPE(WRFU_Time) :: start_time
        
        INTEGER :: rc
        CALL WRFU_ClockGet( grid%domain_clock, StartTime=start_time, &
                            rc=rc )
        IF ( rc /= WRFU_SUCCESS ) THEN
          CALL wrf_error_fatal3("<stdin>",3263,&
            'domain_get_start_time:  WRFU_ClockGet failed' )
        ENDIF
      END FUNCTION domain_get_start_time


      FUNCTION domain_get_stop_time ( grid ) RESULT ( stop_time ) 
        IMPLICIT NONE




        TYPE(domain), INTENT(IN) :: grid
        
        TYPE(WRFU_Time) :: stop_time
        
        INTEGER :: rc
        CALL WRFU_ClockGet( grid%domain_clock, StopTime=stop_time, &
                            rc=rc )
        IF ( rc /= WRFU_SUCCESS ) THEN
          CALL wrf_error_fatal3("<stdin>",3283,&
            'domain_get_stop_time:  WRFU_ClockGet failed' )
        ENDIF
      END FUNCTION domain_get_stop_time


      FUNCTION domain_get_time_step ( grid ) RESULT ( time_step ) 
        IMPLICIT NONE




        TYPE(domain), INTENT(IN) :: grid
        
        TYPE(WRFU_TimeInterval) :: time_step
        
        INTEGER :: rc
        CALL WRFU_ClockGet( grid%domain_clock, timeStep=time_step, &
                            rc=rc )
        IF ( rc /= WRFU_SUCCESS ) THEN
          CALL wrf_error_fatal3("<stdin>",3303,&
            'domain_get_time_step:  WRFU_ClockGet failed' )
        ENDIF
      END FUNCTION domain_get_time_step


      FUNCTION domain_get_advanceCount ( grid ) RESULT ( advanceCount ) 
        IMPLICIT NONE





        TYPE(domain), INTENT(IN) :: grid
        
        INTEGER :: advanceCount
        
        INTEGER(WRFU_KIND_I8) :: advanceCountLcl
        INTEGER :: rc
        CALL WRFU_ClockGet( grid%domain_clock, &
                            advanceCount=advanceCountLcl, &
                            rc=rc )
        IF ( rc /= WRFU_SUCCESS ) THEN
          CALL wrf_error_fatal3("<stdin>",3326,&
            'domain_get_advanceCount:  WRFU_ClockGet failed' )
        ENDIF
        advanceCount = advanceCountLcl
      END FUNCTION domain_get_advanceCount


      SUBROUTINE domain_alarms_destroy ( grid )
        IMPLICIT NONE





        TYPE(domain), INTENT(INOUT) :: grid
        
        INTEGER                     :: alarmid

        IF ( ASSOCIATED( grid%alarms ) .AND. &
             ASSOCIATED( grid%alarms_created ) ) THEN
          DO alarmid = 1, MAX_WRF_ALARMS
            IF ( grid%alarms_created( alarmid ) ) THEN
              CALL WRFU_AlarmDestroy( grid%alarms( alarmid ) )
              grid%alarms_created( alarmid ) = .FALSE.
            ENDIF
          ENDDO
          DEALLOCATE( grid%alarms )
          NULLIFY( grid%alarms )
          DEALLOCATE( grid%alarms_created )
          NULLIFY( grid%alarms_created )
        ENDIF
      END SUBROUTINE domain_alarms_destroy


      SUBROUTINE domain_clock_destroy ( grid )
        IMPLICIT NONE




        TYPE(domain), INTENT(INOUT) :: grid
        IF ( ASSOCIATED( grid%domain_clock ) ) THEN
          IF ( grid%domain_clock_created ) THEN
            CALL WRFU_ClockDestroy( grid%domain_clock )
            grid%domain_clock_created = .FALSE.
          ENDIF
          DEALLOCATE( grid%domain_clock )
          NULLIFY( grid%domain_clock )
        ENDIF
      END SUBROUTINE domain_clock_destroy


      FUNCTION domain_last_time_step ( grid ) RESULT ( LAST_TIME ) 
        IMPLICIT NONE





        TYPE(domain), INTENT(IN) :: grid
        
        LOGICAL :: LAST_TIME
        LAST_TIME =   domain_get_stop_time( grid ) .EQ. &
                    ( domain_get_current_time( grid ) + &
                      domain_get_time_step( grid ) )
      END FUNCTION domain_last_time_step



      FUNCTION domain_clockisstoptime ( grid ) RESULT ( is_stop_time ) 
        IMPLICIT NONE





        TYPE(domain), INTENT(IN) :: grid
        
        LOGICAL :: is_stop_time
        INTEGER :: rc
        is_stop_time = WRFU_ClockIsStopTime( grid%domain_clock , rc=rc )
        IF ( rc /= WRFU_SUCCESS ) THEN
          CALL wrf_error_fatal3("<stdin>",3408,&
            'domain_clockisstoptime:  WRFU_ClockIsStopTime() failed' )
        ENDIF
      END FUNCTION domain_clockisstoptime



      FUNCTION domain_clockisstopsubtime ( grid ) RESULT ( is_stop_subtime ) 
        IMPLICIT NONE





        TYPE(domain), INTENT(IN) :: grid
        
        LOGICAL :: is_stop_subtime
        INTEGER :: rc
        TYPE(WRFU_TimeInterval) :: timeStep
        TYPE(WRFU_Time) :: currentTime
        LOGICAL :: positive_timestep
        is_stop_subtime = .FALSE.
        CALL domain_clock_get( grid, time_step=timeStep, &
                                     current_time=currentTime )
        positive_timestep = ESMF_TimeIntervalIsPositive( timeStep )
        IF ( positive_timestep ) THEN


          IF ( ESMF_TimeGE( currentTime, grid%stop_subtime ) ) THEN
            is_stop_subtime = .TRUE.
          ENDIF
        ELSE


          IF ( ESMF_TimeLE( currentTime, grid%stop_subtime ) ) THEN
            is_stop_subtime = .TRUE.
          ENDIF
        ENDIF
      END FUNCTION domain_clockisstopsubtime




      FUNCTION domain_get_sim_start_time ( grid ) RESULT ( simulationStartTime ) 
        IMPLICIT NONE












        TYPE(domain), INTENT(IN) :: grid
        
        TYPE(WRFU_Time) :: simulationStartTime
        
        INTEGER :: rc
        INTEGER :: simulation_start_year,   simulation_start_month, &
                   simulation_start_day,    simulation_start_hour , &
                   simulation_start_minute, simulation_start_second
        CALL nl_get_simulation_start_year   ( 1, simulation_start_year   )
        CALL nl_get_simulation_start_month  ( 1, simulation_start_month  )
        CALL nl_get_simulation_start_day    ( 1, simulation_start_day    )
        CALL nl_get_simulation_start_hour   ( 1, simulation_start_hour   )
        CALL nl_get_simulation_start_minute ( 1, simulation_start_minute )
        CALL nl_get_simulation_start_second ( 1, simulation_start_second )
        CALL WRFU_TimeSet( simulationStartTime,       &
                           YY=simulation_start_year,  &
                           MM=simulation_start_month, &
                           DD=simulation_start_day,   &
                           H=simulation_start_hour,   &
                           M=simulation_start_minute, &
                           S=simulation_start_second, &
                           rc=rc )
        IF ( rc /= WRFU_SUCCESS ) THEN
          CALL nl_get_start_year   ( 1, simulation_start_year   )
          CALL nl_get_start_month  ( 1, simulation_start_month  )
          CALL nl_get_start_day    ( 1, simulation_start_day    )
          CALL nl_get_start_hour   ( 1, simulation_start_hour   )
          CALL nl_get_start_minute ( 1, simulation_start_minute )
          CALL nl_get_start_second ( 1, simulation_start_second )
          CALL wrf_debug( 150, "WARNING:  domain_get_sim_start_time using head_grid start time from namelist" )
          CALL WRFU_TimeSet( simulationStartTime,       &
                             YY=simulation_start_year,  &
                             MM=simulation_start_month, &
                             DD=simulation_start_day,   &
                             H=simulation_start_hour,   &
                             M=simulation_start_minute, &
                             S=simulation_start_second, &
                             rc=rc )
        ENDIF
        RETURN
      END FUNCTION domain_get_sim_start_time

      FUNCTION domain_get_time_since_sim_start ( grid ) RESULT ( time_since_sim_start ) 
        IMPLICIT NONE









        TYPE(domain), INTENT(IN) :: grid
        
        TYPE(WRFU_TimeInterval) :: time_since_sim_start
        
        TYPE(WRFU_Time) :: lcl_currtime, lcl_simstarttime
        lcl_simstarttime = domain_get_sim_start_time( grid )
        lcl_currtime = domain_get_current_time ( grid )
        time_since_sim_start = lcl_currtime - lcl_simstarttime
      END FUNCTION domain_get_time_since_sim_start




      SUBROUTINE domain_clock_get( grid, current_time,                &
                                         current_timestr,             &
                                         current_timestr_frac,        &
                                         start_time, start_timestr,   &
                                         stop_time, stop_timestr,     &
                                         time_step, time_stepstr,     &
                                         time_stepstr_frac,           &
                                         advanceCount,                &
                                         currentDayOfYearReal,        &
                                         minutesSinceSimulationStart, &
                                         timeSinceSimulationStart,    &
                                         simulationStartTime,         &
                                         simulationStartTimeStr )
        IMPLICIT NONE
        TYPE(domain),            INTENT(IN)              :: grid
        TYPE(WRFU_Time),         INTENT(  OUT), OPTIONAL :: current_time
        CHARACTER (LEN=*),       INTENT(  OUT), OPTIONAL :: current_timestr
        CHARACTER (LEN=*),       INTENT(  OUT), OPTIONAL :: current_timestr_frac
        TYPE(WRFU_Time),         INTENT(  OUT), OPTIONAL :: start_time
        CHARACTER (LEN=*),       INTENT(  OUT), OPTIONAL :: start_timestr
        TYPE(WRFU_Time),         INTENT(  OUT), OPTIONAL :: stop_time
        CHARACTER (LEN=*),       INTENT(  OUT), OPTIONAL :: stop_timestr
        TYPE(WRFU_TimeInterval), INTENT(  OUT), OPTIONAL :: time_step
        CHARACTER (LEN=*),       INTENT(  OUT), OPTIONAL :: time_stepstr
        CHARACTER (LEN=*),       INTENT(  OUT), OPTIONAL :: time_stepstr_frac
        INTEGER,                 INTENT(  OUT), OPTIONAL :: advanceCount
        
        
        REAL,                    INTENT(  OUT), OPTIONAL :: currentDayOfYearReal
        
        
        TYPE(WRFU_Time),         INTENT(  OUT), OPTIONAL :: simulationStartTime
        CHARACTER (LEN=*),       INTENT(  OUT), OPTIONAL :: simulationStartTimeStr
        
        
        TYPE(WRFU_TimeInterval), INTENT(  OUT), OPTIONAL :: timeSinceSimulationStart
        
        REAL,                    INTENT(  OUT), OPTIONAL :: minutesSinceSimulationStart






        
        TYPE(WRFU_Time) :: lcl_currtime, lcl_stoptime, lcl_starttime
        TYPE(WRFU_Time) :: lcl_simulationStartTime
        TYPE(WRFU_TimeInterval) :: lcl_time_step, lcl_timeSinceSimulationStart
        INTEGER :: days, seconds, Sn, Sd, rc
        CHARACTER (LEN=256) :: tmp_str
        CHARACTER (LEN=256) :: frac_str
        REAL(WRFU_KIND_R8) :: currentDayOfYearR8
        IF ( PRESENT( start_time ) ) THEN
          start_time = domain_get_start_time ( grid )
        ENDIF
        IF ( PRESENT( start_timestr ) ) THEN
          lcl_starttime = domain_get_start_time ( grid )
          CALL wrf_timetoa ( lcl_starttime, start_timestr )
        ENDIF
        IF ( PRESENT( time_step ) ) THEN
          time_step = domain_get_time_step ( grid )
        ENDIF
        IF ( PRESENT( time_stepstr ) ) THEN
          lcl_time_step = domain_get_time_step ( grid )
          CALL WRFU_TimeIntervalGet( lcl_time_step, &
                                     timeString=time_stepstr, rc=rc )
          IF ( rc /= WRFU_SUCCESS ) THEN
            CALL wrf_error_fatal3("<stdin>",3598,&
              'domain_clock_get:  WRFU_TimeIntervalGet() failed' )
          ENDIF
        ENDIF
        IF ( PRESENT( time_stepstr_frac ) ) THEN
          lcl_time_step = domain_get_time_step ( grid )
          CALL WRFU_TimeIntervalGet( lcl_time_step, timeString=tmp_str, &
                                     Sn=Sn, Sd=Sd, rc=rc )
          IF ( rc /= WRFU_SUCCESS ) THEN
            CALL wrf_error_fatal3("<stdin>",3607,&
              'domain_clock_get:  WRFU_TimeIntervalGet() failed' )
          ENDIF
          CALL fraction_to_string( Sn, Sd, frac_str )
          time_stepstr_frac = TRIM(tmp_str)//TRIM(frac_str)
        ENDIF
        IF ( PRESENT( advanceCount ) ) THEN
          advanceCount = domain_get_advanceCount ( grid )
        ENDIF
        
        
        
        
        
        
        IF ( PRESENT( current_time ) ) THEN
          current_time = domain_get_current_time ( grid )
        ENDIF
        IF ( PRESENT( current_timestr ) ) THEN
          lcl_currtime = domain_get_current_time ( grid )
          CALL wrf_timetoa ( lcl_currtime, current_timestr )
        ENDIF
        
        IF ( PRESENT( current_timestr_frac ) ) THEN
          lcl_currtime = domain_get_current_time ( grid )
          CALL wrf_timetoa ( lcl_currtime, tmp_str )
          CALL WRFU_TimeGet( lcl_currtime, Sn=Sn, Sd=Sd, rc=rc )
          IF ( rc /= WRFU_SUCCESS ) THEN
            CALL wrf_error_fatal3("<stdin>",3635,&
              'domain_clock_get:  WRFU_TimeGet() failed' )
          ENDIF
          CALL fraction_to_string( Sn, Sd, frac_str )
          current_timestr_frac = TRIM(tmp_str)//TRIM(frac_str)
        ENDIF
        IF ( PRESENT( stop_time ) ) THEN
          stop_time = domain_get_stop_time ( grid )
        ENDIF
        IF ( PRESENT( stop_timestr ) ) THEN
          lcl_stoptime = domain_get_stop_time ( grid )
          CALL wrf_timetoa ( lcl_stoptime, stop_timestr )
        ENDIF
        IF ( PRESENT( currentDayOfYearReal ) ) THEN
          lcl_currtime = domain_get_current_time ( grid )
          CALL WRFU_TimeGet( lcl_currtime, dayOfYear_r8=currentDayOfYearR8, &
                             rc=rc )
          IF ( rc /= WRFU_SUCCESS ) THEN
            CALL wrf_error_fatal3("<stdin>",3653,&
                   'domain_clock_get:  WRFU_TimeGet(dayOfYear_r8) failed' )
          ENDIF
          currentDayOfYearReal = REAL( currentDayOfYearR8 ) - 1.0
        ENDIF
        IF ( PRESENT( simulationStartTime ) ) THEN
          simulationStartTime = domain_get_sim_start_time( grid )
        ENDIF
        IF ( PRESENT( simulationStartTimeStr ) ) THEN
          lcl_simulationStartTime = domain_get_sim_start_time( grid )
          CALL wrf_timetoa ( lcl_simulationStartTime, simulationStartTimeStr )
        ENDIF
        IF ( PRESENT( timeSinceSimulationStart ) ) THEN
          timeSinceSimulationStart = domain_get_time_since_sim_start( grid )
        ENDIF
        IF ( PRESENT( minutesSinceSimulationStart ) ) THEN
          lcl_timeSinceSimulationStart = domain_get_time_since_sim_start( grid )
          CALL WRFU_TimeIntervalGet( lcl_timeSinceSimulationStart, &
                                     D=days, S=seconds, Sn=Sn, Sd=Sd, rc=rc )
          IF ( rc /= WRFU_SUCCESS ) THEN
            CALL wrf_error_fatal3("<stdin>",3673,&
                   'domain_clock_get:  WRFU_TimeIntervalGet() failed' )
          ENDIF
          
          minutesSinceSimulationStart = ( REAL( days ) * 24. * 60. ) + &
                                        ( REAL( seconds ) / 60. )
          IF ( Sd /= 0 ) THEN
            minutesSinceSimulationStart = minutesSinceSimulationStart + &
                                          ( ( REAL( Sn ) / REAL( Sd ) ) / 60. )
          ENDIF
        ENDIF
        RETURN
      END SUBROUTINE domain_clock_get

      FUNCTION domain_clockisstarttime ( grid ) RESULT ( is_start_time ) 
        IMPLICIT NONE





        TYPE(domain), INTENT(IN) :: grid
        
        LOGICAL :: is_start_time
        TYPE(WRFU_Time) :: start_time, current_time
        CALL domain_clock_get( grid, current_time=current_time, &
                                     start_time=start_time )
        is_start_time = ( current_time == start_time )
      END FUNCTION domain_clockisstarttime

      FUNCTION domain_clockissimstarttime ( grid ) RESULT ( is_sim_start_time ) 
        IMPLICIT NONE





        TYPE(domain), INTENT(IN) :: grid
        
        LOGICAL :: is_sim_start_time
        TYPE(WRFU_Time) :: simulationStartTime, current_time
        CALL domain_clock_get( grid, current_time=current_time, &
                                     simulationStartTime=simulationStartTime )
        is_sim_start_time = ( current_time == simulationStartTime )
      END FUNCTION domain_clockissimstarttime




      SUBROUTINE domain_clock_create( grid, StartTime, &
                                            StopTime,  &
                                            TimeStep )
        IMPLICIT NONE
        TYPE(domain),            INTENT(INOUT) :: grid
        TYPE(WRFU_Time),         INTENT(IN   ) :: StartTime
        TYPE(WRFU_Time),         INTENT(IN   ) :: StopTime
        TYPE(WRFU_TimeInterval), INTENT(IN   ) :: TimeStep





        
        INTEGER :: rc
        grid%domain_clock = WRFU_ClockCreate( TimeStep= TimeStep,  &
                                              StartTime=StartTime, &
                                              StopTime= StopTime,  &
                                              rc=rc )
        IF ( rc /= WRFU_SUCCESS ) THEN
          CALL wrf_error_fatal3("<stdin>",3742,&
            'domain_clock_create:  WRFU_ClockCreate() failed' )
        ENDIF
        grid%domain_clock_created = .TRUE.
        RETURN
      END SUBROUTINE domain_clock_create



      SUBROUTINE domain_alarm_create( grid, alarm_id, interval, &
                                            begin_time, end_time )
        USE module_utility
        IMPLICIT NONE
        TYPE(domain), POINTER :: grid
        INTEGER, INTENT(IN) :: alarm_id
        TYPE(WRFU_TimeInterval), INTENT(IN), OPTIONAL :: interval
        TYPE(WRFU_TimeInterval), INTENT(IN), OPTIONAL :: begin_time
        TYPE(WRFU_TimeInterval), INTENT(IN), OPTIONAL :: end_time





        
        INTEGER :: rc




        LOGICAL :: interval_only, all_args, no_args
        TYPE(WRFU_Time) :: startTime
        interval_only = .FALSE.
        all_args = .FALSE.
        no_args = .FALSE.
        IF ( ( .NOT. PRESENT( begin_time ) ) .AND. &
             ( .NOT. PRESENT( end_time   ) ) .AND. &
             (       PRESENT( interval   ) ) ) THEN
           interval_only = .TRUE.
        ELSE IF ( ( .NOT. PRESENT( begin_time ) ) .AND. &
                  ( .NOT. PRESENT( end_time   ) ) .AND. &
                  ( .NOT. PRESENT( interval   ) ) ) THEN
           no_args = .TRUE.
        ELSE IF ( (       PRESENT( begin_time ) ) .AND. &
                  (       PRESENT( end_time   ) ) .AND. &
                  (       PRESENT( interval   ) ) ) THEN
           all_args = .TRUE.
        ELSE
           CALL wrf_error_fatal3("<stdin>",3789,&
             'ERROR in domain_alarm_create:  bad argument list' )
        ENDIF
        CALL domain_clock_get( grid, start_time=startTime )
        IF ( interval_only ) THEN
           grid%io_intervals( alarm_id ) = interval
           grid%alarms( alarm_id ) = &
             WRFU_AlarmCreate( clock=grid%domain_clock, &
                               RingInterval=interval,   &
                               rc=rc )
        ELSE IF ( no_args ) THEN
           grid%alarms( alarm_id ) = &
             WRFU_AlarmCreate( clock=grid%domain_clock, &
                               RingTime=startTime,      &
                               rc=rc )
        ELSE IF ( all_args ) THEN
           grid%io_intervals( alarm_id ) = interval
           grid%alarms( alarm_id ) = &
             WRFU_AlarmCreate( clock=grid%domain_clock,         &
                               RingTime=startTime + begin_time, &
                               RingInterval=interval,           &
                               StopTime=startTime + end_time,   &
                               rc=rc )
        ENDIF
        IF ( rc /= WRFU_SUCCESS ) THEN
          CALL wrf_error_fatal3("<stdin>",3814,&
            'domain_alarm_create:  WRFU_AlarmCreate() failed' )
        ENDIF
        CALL WRFU_AlarmRingerOff( grid%alarms( alarm_id ) , rc=rc )
        IF ( rc /= WRFU_SUCCESS ) THEN
          CALL wrf_error_fatal3("<stdin>",3819,&
            'domain_alarm_create:  WRFU_AlarmRingerOff() failed' )
        ENDIF
        grid%alarms_created( alarm_id ) = .TRUE.
      END SUBROUTINE domain_alarm_create



      SUBROUTINE domain_clock_set( grid, current_timestr, &
                                         stop_timestr,    &
                                         time_step_seconds )
        IMPLICIT NONE
        TYPE(domain),      INTENT(INOUT)           :: grid
        CHARACTER (LEN=*), INTENT(IN   ), OPTIONAL :: current_timestr
        CHARACTER (LEN=*), INTENT(IN   ), OPTIONAL :: stop_timestr
        INTEGER,           INTENT(IN   ), OPTIONAL :: time_step_seconds






        
        TYPE(WRFU_Time) :: lcl_currtime, lcl_stoptime
        TYPE(WRFU_TimeInterval) :: tmpTimeInterval
        INTEGER :: rc
        IF ( PRESENT( current_timestr ) ) THEN
          CALL wrf_atotime( current_timestr(1:19), lcl_currtime )
          CALL WRFU_ClockSet( grid%domain_clock, currTime=lcl_currtime, &
                              rc=rc )
          IF ( rc /= WRFU_SUCCESS ) THEN
            CALL wrf_error_fatal3("<stdin>",3850,&
              'domain_clock_set:  WRFU_ClockSet(CurrTime) failed' )
          ENDIF
        ENDIF
        IF ( PRESENT( stop_timestr ) ) THEN
          CALL wrf_atotime( stop_timestr(1:19), lcl_stoptime )
          CALL WRFU_ClockSet( grid%domain_clock, stopTime=lcl_stoptime, &
                              rc=rc )
          IF ( rc /= WRFU_SUCCESS ) THEN
            CALL wrf_error_fatal3("<stdin>",3859,&
              'domain_clock_set:  WRFU_ClockSet(StopTime) failed' )
          ENDIF
        ENDIF
        IF ( PRESENT( time_step_seconds ) ) THEN
          CALL WRFU_TimeIntervalSet( tmpTimeInterval, &
                                     S=time_step_seconds, rc=rc )
          IF ( rc /= WRFU_SUCCESS ) THEN
            CALL wrf_error_fatal3("<stdin>",3867,&
              'domain_clock_set:  WRFU_TimeIntervalSet failed' )
          ENDIF
          CALL WRFU_ClockSet ( grid%domain_clock,        &
                               timeStep=tmpTimeInterval, &
                               rc=rc )
          IF ( rc /= WRFU_SUCCESS ) THEN
            CALL wrf_error_fatal3("<stdin>",3874,&
              'domain_clock_set:  WRFU_ClockSet(TimeStep) failed' )
          ENDIF
        ENDIF
        RETURN
      END SUBROUTINE domain_clock_set


      
      
      SUBROUTINE domain_clockprint ( level, grid, pre_str )
        IMPLICIT NONE
        INTEGER,           INTENT( IN) :: level
        TYPE(domain),      INTENT( IN) :: grid
        CHARACTER (LEN=*), INTENT( IN) :: pre_str
        CALL wrf_clockprint ( level, grid%domain_clock, pre_str )
        RETURN
      END SUBROUTINE domain_clockprint


      
      
      SUBROUTINE domain_clockadvance ( grid )
        IMPLICIT NONE
        TYPE(domain), INTENT(INOUT) :: grid
        INTEGER :: rc
        CALL domain_clockprint ( 250, grid, &
          'DEBUG domain_clockadvance():  before WRFU_ClockAdvance,' )
        CALL WRFU_ClockAdvance( grid%domain_clock, rc=rc )
        IF ( rc /= WRFU_SUCCESS ) THEN
          CALL wrf_error_fatal3("<stdin>",3904,&
            'domain_clockadvance:  WRFU_ClockAdvance() failed' )
        ENDIF
        CALL domain_clockprint ( 250, grid, &
          'DEBUG domain_clockadvance():  after WRFU_ClockAdvance,' )
        
        
        CALL domain_clock_get( grid, minutesSinceSimulationStart=grid%xtime )
        CALL domain_clock_get( grid, currentDayOfYearReal=grid%julian )
        RETURN
      END SUBROUTINE domain_clockadvance



      
      
      SUBROUTINE domain_setgmtetc ( grid, start_of_simulation )
        IMPLICIT NONE
        TYPE (domain), INTENT(INOUT) :: grid
        LOGICAL,       INTENT(  OUT) :: start_of_simulation
        
        CHARACTER (LEN=132)          :: message
        TYPE(WRFU_Time)              :: simStartTime
        INTEGER                      :: hr, mn, sec, ms, rc
        CALL domain_clockprint(150, grid, &
          'DEBUG domain_setgmtetc():  get simStartTime from clock,')
        CALL domain_clock_get( grid, simulationStartTime=simStartTime, &
                                     simulationStartTimeStr=message )
        CALL WRFU_TimeGet( simStartTime, YY=grid%julyr, dayOfYear=grid%julday, &
                           H=hr, M=mn, S=sec, MS=ms, rc=rc)
        IF ( rc /= WRFU_SUCCESS ) THEN
          CALL wrf_error_fatal3("<stdin>",3935,&
            'domain_setgmtetc:  WRFU_TimeGet() failed' )
        ENDIF
        WRITE( wrf_err_message , * ) 'DEBUG domain_setgmtetc():  simulation start time = [',TRIM( message ),']'
        CALL wrf_debug( 150, TRIM(wrf_err_message) )
        grid%gmt=hr+real(mn)/60.+real(sec)/3600.+real(ms)/(1000*3600)
        WRITE( wrf_err_message , * ) 'DEBUG domain_setgmtetc():  julyr,hr,mn,sec,ms,julday = ', &
                                     grid%julyr,hr,mn,sec,ms,grid%julday
        CALL wrf_debug( 150, TRIM(wrf_err_message) )
        WRITE( wrf_err_message , * ) 'DEBUG domain_setgmtetc():  gmt = ',grid%gmt
        CALL wrf_debug( 150, TRIM(wrf_err_message) )
        start_of_simulation = domain_ClockIsSimStartTime(grid)
        RETURN
      END SUBROUTINE domain_setgmtetc
     


      
      
      SUBROUTINE set_current_grid_ptr( grid_ptr )
        IMPLICIT NONE
        TYPE(domain), POINTER :: grid_ptr






        current_grid_set = .TRUE.
        current_grid => grid_ptr

      END SUBROUTINE set_current_grid_ptr








      LOGICAL FUNCTION Is_alarm_tstep( grid_clock, alarm )

        IMPLICIT NONE

        TYPE (WRFU_Clock), INTENT(in)  :: grid_clock
        TYPE (WRFU_Alarm), INTENT(in)  :: alarm

        LOGICAL :: pred1, pred2, pred3

        Is_alarm_tstep = .FALSE.

        IF ( ASSOCIATED( alarm%alarmint ) ) THEN
          IF ( alarm%alarmint%Enabled ) THEN
            IF ( alarm%alarmint%RingIntervalSet ) THEN
              pred1 = .FALSE. ; pred2 = .FALSE. ; pred3 = .FALSE.
              IF ( alarm%alarmint%StopTimeSet ) THEN
                 PRED1 = ( grid_clock%clockint%CurrTime + grid_clock%clockint%TimeStep > &
                      alarm%alarmint%StopTime )
              ENDIF
              IF ( alarm%alarmint%RingTimeSet ) THEN
                 PRED2 = ( ( alarm%alarmint%RingTime - &
                      grid_clock%clockint%TimeStep <= &
                      grid_clock%clockint%CurrTime )     &
                      .AND. ( grid_clock%clockint%CurrTime < alarm%alarmint%RingTime ) )
              ENDIF
              IF ( alarm%alarmint%RingIntervalSet ) THEN
                 PRED3 = ( alarm%alarmint%PrevRingTime + &
                      alarm%alarmint%RingInterval <= &
                      grid_clock%clockint%CurrTime + grid_clock%clockint%TimeStep )
              ENDIF
              IF ( ( .NOT. ( pred1 ) ) .AND. &
                   ( ( pred2 ) .OR. ( pred3 ) ) ) THEN
                 Is_alarm_tstep = .TRUE.
              ENDIF
            ELSE IF ( alarm%alarmint%RingTimeSet ) THEN
              IF ( alarm%alarmint%RingTime -&
                   grid_clock%clockint%TimeStep <= &
                   grid_clock%clockint%CurrTime ) THEN
                 Is_alarm_tstep = .TRUE.
              ENDIF
            ENDIF
          ENDIF
        ENDIF

      END FUNCTION Is_alarm_tstep








      
      SUBROUTINE domain_time_test_print ( pre_str, name_str, res_str )
        IMPLICIT NONE
        CHARACTER (LEN=*), INTENT(IN) :: pre_str
        CHARACTER (LEN=*), INTENT(IN) :: name_str
        CHARACTER (LEN=*), INTENT(IN) :: res_str
        CHARACTER (LEN=512) :: out_str
        WRITE (out_str,                                            &
          FMT="('DOMAIN_TIME_TEST ',A,':  ',A,' = ',A)") &
          TRIM(pre_str), TRIM(name_str), TRIM(res_str)
        CALL wrf_debug( 0, TRIM(out_str) )
      END SUBROUTINE domain_time_test_print

      
      SUBROUTINE test_adjust_io_timestr( TI_h, TI_m, TI_s, &
        CT_yy,  CT_mm,  CT_dd,  CT_h,  CT_m,  CT_s,        &
        ST_yy,  ST_mm,  ST_dd,  ST_h,  ST_m,  ST_s,        &
        res_str, testname )
        INTEGER, INTENT(IN) :: TI_H
        INTEGER, INTENT(IN) :: TI_M
        INTEGER, INTENT(IN) :: TI_S
        INTEGER, INTENT(IN) :: CT_YY
        INTEGER, INTENT(IN) :: CT_MM  
        INTEGER, INTENT(IN) :: CT_DD  
        INTEGER, INTENT(IN) :: CT_H
        INTEGER, INTENT(IN) :: CT_M
        INTEGER, INTENT(IN) :: CT_S
        INTEGER, INTENT(IN) :: ST_YY
        INTEGER, INTENT(IN) :: ST_MM  
        INTEGER, INTENT(IN) :: ST_DD  
        INTEGER, INTENT(IN) :: ST_H
        INTEGER, INTENT(IN) :: ST_M
        INTEGER, INTENT(IN) :: ST_S
        CHARACTER (LEN=*), INTENT(IN) :: res_str
        CHARACTER (LEN=*), INTENT(IN) :: testname
        
        TYPE(WRFU_TimeInterval) :: TI
        TYPE(WRFU_Time) :: CT, ST
        LOGICAL :: test_passed
        INTEGER :: rc
        CHARACTER(LEN=WRFU_MAXSTR) :: TI_str, CT_str, ST_str, computed_str
        
        CALL WRFU_TimeIntervalSet( TI, H=TI_H, M=TI_M, S=TI_S, rc=rc )
        CALL wrf_check_error( WRFU_SUCCESS, rc, &
                              'FAIL:  '//TRIM(testname)//'WRFU_TimeIntervalSet() ', &
                              "module_domain.F" , &
                              2583  )
        CALL WRFU_TimeIntervalGet( TI, timeString=TI_str, rc=rc )
        CALL wrf_check_error( WRFU_SUCCESS, rc, &
                              'FAIL:  '//TRIM(testname)//'WRFU_TimeGet() ', &
                              "module_domain.F" , &
                              2588  )
        
        CALL WRFU_TimeSet( CT, YY=CT_YY, MM=CT_MM, DD=CT_DD , &
                                H=CT_H,   M=CT_M,   S=CT_S, rc=rc )
        CALL wrf_check_error( WRFU_SUCCESS, rc, &
                              'FAIL:  '//TRIM(testname)//'WRFU_TimeSet() ', &
                              "module_domain.F" , &
                              2595  )
        CALL WRFU_TimeGet( CT, timeString=CT_str, rc=rc )
        CALL wrf_check_error( WRFU_SUCCESS, rc, &
                              'FAIL:  '//TRIM(testname)//'WRFU_TimeGet() ', &
                              "module_domain.F" , &
                              2600  )
        
        CALL WRFU_TimeSet( ST, YY=ST_YY, MM=ST_MM, DD=ST_DD , &
                                H=ST_H,   M=ST_M,   S=ST_S, rc=rc )
        CALL wrf_check_error( WRFU_SUCCESS, rc, &
                              'FAIL:  '//TRIM(testname)//'WRFU_TimeSet() ', &
                              "module_domain.F" , &
                              2607  )
        CALL WRFU_TimeGet( ST, timeString=ST_str, rc=rc )
        CALL wrf_check_error( WRFU_SUCCESS, rc, &
                              'FAIL:  '//TRIM(testname)//'WRFU_TimeGet() ', &
                              "module_domain.F" , &
                              2612  )
        
        CALL adjust_io_timestr ( TI, CT, ST, computed_str )
        
        test_passed = .FALSE.
        IF ( LEN_TRIM(res_str) == LEN_TRIM(computed_str) ) THEN
          IF ( res_str(1:LEN_TRIM(res_str)) == computed_str(1:LEN_TRIM(computed_str)) ) THEN
            test_passed = .TRUE.
          ENDIF
        ENDIF
        
        IF ( test_passed ) THEN
          WRITE(*,FMT='(A)') 'PASS:  '//TRIM(testname)
        ELSE
          WRITE(*,*) 'FAIL:  ',TRIM(testname),':  adjust_io_timestr(',    &
            TRIM(TI_str),',',TRIM(CT_str),',',TRIM(ST_str),')  expected <', &
            TRIM(res_str),'>  but computed <',TRIM(computed_str),'>'
        ENDIF
      END SUBROUTINE test_adjust_io_timestr

      
      
      
      
      
      SUBROUTINE domain_time_test ( grid, pre_str )
        IMPLICIT NONE
        TYPE(domain),      INTENT(IN) :: grid
        CHARACTER (LEN=*), INTENT(IN) :: pre_str
        
        LOGICAL, SAVE :: one_time_tests_done = .FALSE.
        REAL :: minutesSinceSimulationStart
        INTEGER :: advance_count, rc
        REAL :: currentDayOfYearReal
        TYPE(WRFU_TimeInterval) :: timeSinceSimulationStart
        TYPE(WRFU_Time) :: simulationStartTime
        CHARACTER (LEN=512) :: res_str
        LOGICAL :: self_test_domain
        
        
        
        
        
        
        CALL nl_get_self_test_domain( 1, self_test_domain )
        IF ( self_test_domain ) THEN
          CALL domain_clock_get( grid, advanceCount=advance_count )
          WRITE ( res_str, FMT="(I8.8)" ) advance_count
          CALL domain_time_test_print( pre_str, 'advanceCount', res_str )
          CALL domain_clock_get( grid, currentDayOfYearReal=currentDayOfYearReal )
          WRITE ( res_str, FMT='(F10.6)' ) currentDayOfYearReal
          CALL domain_time_test_print( pre_str, 'currentDayOfYearReal', res_str )
          CALL domain_clock_get( grid, minutesSinceSimulationStart=minutesSinceSimulationStart )
          WRITE ( res_str, FMT='(F10.6)' ) minutesSinceSimulationStart
          CALL domain_time_test_print( pre_str, 'minutesSinceSimulationStart', res_str )
          CALL domain_clock_get( grid, current_timestr=res_str )
          CALL domain_time_test_print( pre_str, 'current_timestr', res_str )
          CALL domain_clock_get( grid, current_timestr_frac=res_str )
          CALL domain_time_test_print( pre_str, 'current_timestr_frac', res_str )
          CALL domain_clock_get( grid, timeSinceSimulationStart=timeSinceSimulationStart )
          CALL WRFU_TimeIntervalGet( timeSinceSimulationStart, timeString=res_str, rc=rc )
          IF ( rc /= WRFU_SUCCESS ) THEN
            CALL wrf_error_fatal3("<stdin>",4165,&
              'domain_time_test:  WRFU_TimeIntervalGet() failed' )
          ENDIF
          CALL domain_time_test_print( pre_str, 'timeSinceSimulationStart', res_str )
          
          
          IF ( .NOT. one_time_tests_done ) THEN
            one_time_tests_done = .TRUE.
            CALL domain_clock_get( grid, simulationStartTimeStr=res_str )
            CALL domain_time_test_print( pre_str, 'simulationStartTime', res_str )
            CALL domain_clock_get( grid, start_timestr=res_str )
            CALL domain_time_test_print( pre_str, 'start_timestr', res_str )
            CALL domain_clock_get( grid, stop_timestr=res_str )
            CALL domain_time_test_print( pre_str, 'stop_timestr', res_str )
            CALL domain_clock_get( grid, time_stepstr=res_str )
            CALL domain_time_test_print( pre_str, 'time_stepstr', res_str )
            CALL domain_clock_get( grid, time_stepstr_frac=res_str )
            CALL domain_time_test_print( pre_str, 'time_stepstr_frac', res_str )
            
            
            
            
            
            
            CALL test_adjust_io_timestr( TI_h=3, TI_m=0, TI_s=0,          &
              CT_yy=2000,  CT_mm=1,  CT_dd=26,  CT_h=0,  CT_m=0,  CT_s=0, &
              ST_yy=2000,  ST_mm=1,  ST_dd=24,  ST_h=12, ST_m=0,  ST_s=0, &
              res_str='2000-01-26_00:00:00', testname='adjust_io_timestr_1' )
            
            
            
            
            
          ENDIF
        ENDIF
        RETURN
      END SUBROUTINE domain_time_test






END MODULE module_domain









SUBROUTINE get_current_time_string( time_str )
  USE module_domain
  IMPLICIT NONE
  CHARACTER (LEN=*), INTENT(OUT) :: time_str
  
  INTEGER :: debug_level_lcl

  time_str = ''
  IF ( current_grid_set ) THEN








    IF ( current_grid%time_set ) THEN

      
      CALL get_wrf_debug_level( debug_level_lcl )
      CALL set_wrf_debug_level ( 0 )
      current_grid_set = .FALSE.
      CALL domain_clock_get( current_grid, current_timestr_frac=time_str )
      
      CALL set_wrf_debug_level ( debug_level_lcl )
      current_grid_set = .TRUE.

    ENDIF
  ENDIF

END SUBROUTINE get_current_time_string






SUBROUTINE get_current_grid_name( grid_str )
  USE module_domain
  IMPLICIT NONE
  CHARACTER (LEN=*), INTENT(OUT) :: grid_str
  grid_str = ''
  IF ( current_grid_set ) THEN
    WRITE(grid_str,FMT="('d',I2.2)") current_grid%id
  ENDIF
END SUBROUTINE get_current_grid_name




   SUBROUTINE get_ijk_from_grid_ext (  grid ,                   &
                           ids, ide, jds, jde, kds, kde,    &
                           ims, ime, jms, jme, kms, kme,    &
                           ips, ipe, jps, jpe, kps, kpe,    &
                           imsx, imex, jmsx, jmex, kmsx, kmex,    &
                           ipsx, ipex, jpsx, jpex, kpsx, kpex,    &
                           imsy, imey, jmsy, jmey, kmsy, kmey,    &
                           ipsy, ipey, jpsy, jpey, kpsy, kpey )
    USE module_domain
    IMPLICIT NONE
    TYPE( domain ), INTENT (IN)  :: grid
    INTEGER, INTENT(OUT) ::                                 &
                           ids, ide, jds, jde, kds, kde,    &
                           ims, ime, jms, jme, kms, kme,    &
                           ips, ipe, jps, jpe, kps, kpe,    &
                           imsx, imex, jmsx, jmex, kmsx, kmex,    &
                           ipsx, ipex, jpsx, jpex, kpsx, kpex,    &
                           imsy, imey, jmsy, jmey, kmsy, kmey,    &
                           ipsy, ipey, jpsy, jpey, kpsy, kpey

     CALL get_ijk_from_grid2 (  grid ,                   &
                           ids, ide, jds, jde, kds, kde,    &
                           ims, ime, jms, jme, kms, kme,    &
                           ips, ipe, jps, jpe, kps, kpe )
     data_ordering : SELECT CASE ( model_data_order )
       CASE  ( DATA_ORDER_XYZ )
           imsx = grid%sm31x ; imex = grid%em31x ; jmsx = grid%sm32x ; jmex = grid%em32x ; kmsx = grid%sm33x ; kmex = grid%em33x ;
           ipsx = grid%sp31x ; ipex = grid%ep31x ; jpsx = grid%sp32x ; jpex = grid%ep32x ; kpsx = grid%sp33x ; kpex = grid%ep33x ;
           imsy = grid%sm31y ; imey = grid%em31y ; jmsy = grid%sm32y ; jmey = grid%em32y ; kmsy = grid%sm33y ; kmey = grid%em33y ;
           ipsy = grid%sp31y ; ipey = grid%ep31y ; jpsy = grid%sp32y ; jpey = grid%ep32y ; kpsy = grid%sp33y ; kpey = grid%ep33y ;
       CASE  ( DATA_ORDER_YXZ )
           imsx = grid%sm32x ; imex = grid%em32x ; jmsx = grid%sm31x ; jmex = grid%em31x ; kmsx = grid%sm33x ; kmex = grid%em33x ;
           ipsx = grid%sp32x ; ipex = grid%ep32x ; jpsx = grid%sp31x ; jpex = grid%ep31x ; kpsx = grid%sp33x ; kpex = grid%ep33x ;
           imsy = grid%sm32y ; imey = grid%em32y ; jmsy = grid%sm31y ; jmey = grid%em31y ; kmsy = grid%sm33y ; kmey = grid%em33y ;
           ipsy = grid%sp32y ; ipey = grid%ep32y ; jpsy = grid%sp31y ; jpey = grid%ep31y ; kpsy = grid%sp33y ; kpey = grid%ep33y ;
       CASE  ( DATA_ORDER_ZXY )
           imsx = grid%sm32x ; imex = grid%em32x ; jmsx = grid%sm33x ; jmex = grid%em33x ; kmsx = grid%sm31x ; kmex = grid%em31x ;
           ipsx = grid%sp32x ; ipex = grid%ep32x ; jpsx = grid%sp33x ; jpex = grid%ep33x ; kpsx = grid%sp31x ; kpex = grid%ep31x ;
           imsy = grid%sm32y ; imey = grid%em32y ; jmsy = grid%sm33y ; jmey = grid%em33y ; kmsy = grid%sm31y ; kmey = grid%em31y ;
           ipsy = grid%sp32y ; ipey = grid%ep32y ; jpsy = grid%sp33y ; jpey = grid%ep33y ; kpsy = grid%sp31y ; kpey = grid%ep31y ;
       CASE  ( DATA_ORDER_ZYX )
           imsx = grid%sm33x ; imex = grid%em33x ; jmsx = grid%sm32x ; jmex = grid%em32x ; kmsx = grid%sm31x ; kmex = grid%em31x ;
           ipsx = grid%sp33x ; ipex = grid%ep33x ; jpsx = grid%sp32x ; jpex = grid%ep32x ; kpsx = grid%sp31x ; kpex = grid%ep31x ;
           imsy = grid%sm33y ; imey = grid%em33y ; jmsy = grid%sm32y ; jmey = grid%em32y ; kmsy = grid%sm31y ; kmey = grid%em31y ;
           ipsy = grid%sp33y ; ipey = grid%ep33y ; jpsy = grid%sp32y ; jpey = grid%ep32y ; kpsy = grid%sp31y ; kpey = grid%ep31y ;
       CASE  ( DATA_ORDER_XZY )
           imsx = grid%sm31x ; imex = grid%em31x ; jmsx = grid%sm33x ; jmex = grid%em33x ; kmsx = grid%sm32x ; kmex = grid%em32x ;
           ipsx = grid%sp31x ; ipex = grid%ep31x ; jpsx = grid%sp33x ; jpex = grid%ep33x ; kpsx = grid%sp32x ; kpex = grid%ep32x ;
           imsy = grid%sm31y ; imey = grid%em31y ; jmsy = grid%sm33y ; jmey = grid%em33y ; kmsy = grid%sm32y ; kmey = grid%em32y ;
           ipsy = grid%sp31y ; ipey = grid%ep31y ; jpsy = grid%sp33y ; jpey = grid%ep33y ; kpsy = grid%sp32y ; kpey = grid%ep32y ;
       CASE  ( DATA_ORDER_YZX )
           imsx = grid%sm33x ; imex = grid%em33x ; jmsx = grid%sm31x ; jmex = grid%em31x ; kmsx = grid%sm32x ; kmex = grid%em32x ;
           ipsx = grid%sp33x ; ipex = grid%ep33x ; jpsx = grid%sp31x ; jpex = grid%ep31x ; kpsx = grid%sp32x ; kpex = grid%ep32x ;
           imsy = grid%sm33y ; imey = grid%em33y ; jmsy = grid%sm31y ; jmey = grid%em31y ; kmsy = grid%sm32y ; kmey = grid%em32y ;
           ipsy = grid%sp33y ; ipey = grid%ep33y ; jpsy = grid%sp31y ; jpey = grid%ep31y ; kpsy = grid%sp32y ; kpey = grid%ep32y ;
     END SELECT data_ordering
   END SUBROUTINE get_ijk_from_grid_ext




   SUBROUTINE get_ijk_from_subgrid_ext (  grid ,                &
                           ids0, ide0, jds0, jde0, kds0, kde0,    &
                           ims0, ime0, jms0, jme0, kms0, kme0,    &
                           ips0, ipe0, jps0, jpe0, kps0, kpe0    )
    USE module_domain
    IMPLICIT NONE
    TYPE( domain ), INTENT (IN)  :: grid
    INTEGER, INTENT(OUT) ::                                 &
                           ids0, ide0, jds0, jde0, kds0, kde0,    &
                           ims0, ime0, jms0, jme0, kms0, kme0,    &
                           ips0, ipe0, jps0, jpe0, kps0, kpe0
   
    INTEGER              ::                                 &
                           ids, ide, jds, jde, kds, kde,    &
                           ims, ime, jms, jme, kms, kme,    &
                           ips, ipe, jps, jpe, kps, kpe
     CALL get_ijk_from_grid (  grid ,                         &
                             ids, ide, jds, jde, kds, kde,    &
                             ims, ime, jms, jme, kms, kme,    &
                             ips, ipe, jps, jpe, kps, kpe    )
     ids0 = ids
     ide0 = ide * grid%sr_x
     ims0 = (ims-1)*grid%sr_x+1
     ime0 = ime * grid%sr_x
     ips0 = (ips-1)*grid%sr_x+1
     ipe0 = ipe * grid%sr_x

     jds0 = jds
     jde0 = jde * grid%sr_y
     jms0 = (jms-1)*grid%sr_y+1
     jme0 = jme * grid%sr_y
     jps0 = (jps-1)*grid%sr_y+1
     jpe0 = jpe * grid%sr_y

     kds0 = kds
     kde0 = kde
     kms0 = kms
     kme0 = kme
     kps0 = kps
     kpe0 = kpe
   RETURN
   END SUBROUTINE get_ijk_from_subgrid_ext


   SUBROUTINE get_dims_from_grid_id (  id   &
                          ,ds, de           &
                          ,ms, me           &
                          ,ps, pe           &
                          ,mxs, mxe         &
                          ,pxs, pxe         &
                          ,mys, mye         &
                          ,pys, pye )
    USE module_domain, ONLY : domain, head_grid, find_grid_by_id
    IMPLICIT NONE
    TYPE( domain ), POINTER  :: grid
    INTEGER, INTENT(IN ) :: id
    INTEGER, DIMENSION(3), INTENT(INOUT) ::                   &
                           ds, de           &
                          ,ms, me           &
                          ,ps, pe           &
                          ,mxs, mxe         &
                          ,pxs, pxe         &
                          ,mys, mye         &
                          ,pys, pye

     
     CHARACTER*256 mess

     NULLIFY( grid )
     CALL find_grid_by_id ( id, head_grid, grid )

     IF ( ASSOCIATED(grid) ) THEN
           ds(1) = grid%sd31 ; de(1) = grid%ed31 ; ds(2) = grid%sd32 ; de(2) = grid%ed32 ; ds(3) = grid%sd33 ; de(3) = grid%ed33 ;
           ms(1) = grid%sm31 ; me(1) = grid%em31 ; ms(2) = grid%sm32 ; me(2) = grid%em32 ; ms(3) = grid%sm33 ; me(3) = grid%em33 ;
           ps(1) = grid%sp31 ; pe(1) = grid%ep31 ; ps(2) = grid%sp32 ; pe(2) = grid%ep32 ; ps(3) = grid%sp33 ; pe(3) = grid%ep33 ;
           mxs(1) = grid%sm31x ; mxe(1) = grid%em31x 
           mxs(2) = grid%sm32x ; mxe(2) = grid%em32x 
           mxs(3) = grid%sm33x ; mxe(3) = grid%em33x 
           pxs(1) = grid%sp31x ; pxe(1) = grid%ep31x 
           pxs(2) = grid%sp32x ; pxe(2) = grid%ep32x 
           pxs(3) = grid%sp33x ; pxe(3) = grid%ep33x
           mys(1) = grid%sm31y ; mye(1) = grid%em31y 
           mys(2) = grid%sm32y ; mye(2) = grid%em32y 
           mys(3) = grid%sm33y ; mye(3) = grid%em33y 
           pys(1) = grid%sp31y ; pye(1) = grid%ep31y 
           pys(2) = grid%sp32y ; pye(2) = grid%ep32y 
           pys(3) = grid%sp33y ; pye(3) = grid%ep33y 
     ELSE
        WRITE(mess,*)'internal error: get_ijk_from_grid_id: no such grid id:',id
        CALL wrf_error_fatal3("<stdin>",4419,&
TRIM(mess))
     ENDIF

   END SUBROUTINE get_dims_from_grid_id


   SUBROUTINE get_ijk_from_grid_id (  id ,                   &
                           ids, ide, jds, jde, kds, kde,    &
                           ims, ime, jms, jme, kms, kme,    &
                           ips, ipe, jps, jpe, kps, kpe,    &
                           imsx, imex, jmsx, jmex, kmsx, kmex,    &
                           ipsx, ipex, jpsx, jpex, kpsx, kpex,    &
                           imsy, imey, jmsy, jmey, kmsy, kmey,    &
                           ipsy, ipey, jpsy, jpey, kpsy, kpey )
    USE module_domain, ONLY : domain, head_grid, find_grid_by_id, get_ijk_from_grid
    IMPLICIT NONE
    TYPE( domain ), POINTER  :: grid
    INTEGER, INTENT(IN ) :: id
    INTEGER, INTENT(OUT) ::                                 &
                           ids, ide, jds, jde, kds, kde,    &
                           ims, ime, jms, jme, kms, kme,    &
                           ips, ipe, jps, jpe, kps, kpe,    &
                           imsx, imex, jmsx, jmex, kmsx, kmex,    &
                           ipsx, ipex, jpsx, jpex, kpsx, kpex,    &
                           imsy, imey, jmsy, jmey, kmsy, kmey,    &
                           ipsy, ipey, jpsy, jpey, kpsy, kpey
     
     CHARACTER*256 mess

     NULLIFY( grid )
     CALL find_grid_by_id ( id, head_grid, grid )

     IF ( ASSOCIATED(grid) ) THEN
     CALL get_ijk_from_grid (  grid ,                   &
                           ids, ide, jds, jde, kds, kde,    &
                           ims, ime, jms, jme, kms, kme,    &
                           ips, ipe, jps, jpe, kps, kpe,    &
                           imsx, imex, jmsx, jmex, kmsx, kmex,    &
                           ipsx, ipex, jpsx, jpex, kpsx, kpex,    &
                           imsy, imey, jmsy, jmey, kmsy, kmey,    &
                           ipsy, ipey, jpsy, jpey, kpsy, kpey )
     ELSE
        WRITE(mess,*)'internal error: get_ijk_from_grid_id: no such grid id:',id
        CALL wrf_error_fatal3("<stdin>",4463,&
TRIM(mess))
     ENDIF

   END SUBROUTINE get_ijk_from_grid_id



   SUBROUTINE modify_io_masks ( id )
     USE module_domain, ONLY : domain, modify_io_masks1, head_grid, find_grid_by_id
     IMPLICIT NONE
     INTEGER, INTENT(IN) :: id
     TYPE(domain), POINTER :: grid
     CALL find_grid_by_id( id, head_grid, grid )
     IF ( ASSOCIATED( grid ) ) CALL modify_io_masks1( grid, id ) 
     RETURN 
   END SUBROUTINE modify_io_masks



