module mgxs_interface

  use, intrinsic :: ISO_C_BINDING

  use hdf5_interface

  implicit none

  interface
    subroutine calculate_xs_c(i_mat, gin, sqrtkT, uvw, total_xs, abs_xs, &
         nu_fiss_xs) bind(C)
      use ISO_C_BINDING
      implicit none
      integer(C_INT), value, intent(in) :: i_mat
      integer(C_INT), value, intent(in) :: gin
      real(C_DOUBLE), value, intent(in) :: sqrtkT
      real(C_DOUBLE),        intent(in) :: uvw(1:3)
      real(C_DOUBLE),     intent(inout) :: total_xs
      real(C_DOUBLE),     intent(inout) :: abs_xs
      real(C_DOUBLE),     intent(inout) :: nu_fiss_xs
    end subroutine calculate_xs_c

    subroutine get_name_c(index, name_len, name) bind(C)
      use ISO_C_BINDING
      implicit none
      integer(C_INT), value,  intent(in)    :: index
      integer(C_INT), value,  intent(in)    :: name_len
      character(kind=C_CHAR), intent(inout) :: name(name_len)
    end subroutine get_name_c

    function get_awr_c(index) result(awr) bind(C)
      use ISO_C_BINDING
      implicit none
      integer(C_INT), value, intent(in) :: index
      real(C_DOUBLE)                    :: awr
    end function get_awr_c

  end interface

  ! Number of energy groups
  integer(C_INT), bind(C) :: num_energy_groups

  ! Number of delayed groups
  integer(C_INT), bind(C) :: num_delayed_groups

  ! Energy group structure with decreasing energy
  real(8), allocatable :: energy_bins(:)

  ! Midpoint of the energy group structure
  real(C_DOUBLE), allocatable :: energy_bin_avg(:)

  ! Energy group structure with increasing energy
  real(C_DOUBLE), allocatable, target :: rev_energy_bins(:)

contains

  function rev_energy_bins_ptr() result(ptr) bind(C)
    type(C_PTR) :: ptr
    ptr = C_LOC(rev_energy_bins(1))
  end function

end module mgxs_interface
