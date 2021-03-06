//! \file nuclide.h
//! \brief Nuclide type and other associated types/data

#ifndef OPENMC_NUCLIDE_H
#define OPENMC_NUCLIDE_H

#include <array>
#include <memory> // for unique_ptr
#include <unordered_map>
#include <vector>

#include <hdf5.h>

#include "openmc/constants.h"
#include "openmc/endf.h"
#include "openmc/reaction.h"
#include "openmc/reaction_product.h"
#include "openmc/urr.h"
#include "openmc/wmp.h"

namespace openmc {

//==============================================================================
// Constants
//==============================================================================

constexpr double CACHE_INVALID {-1.0};

//==============================================================================
//! Cached microscopic cross sections for a particular nuclide at the current
//! energy
//==============================================================================

struct NuclideMicroXS {
  // Microscopic cross sections in barns
  double total;            //!< total cross section
  double absorption;       //!< absorption (disappearance)
  double fission;          //!< fission
  double nu_fission;       //!< neutron production from fission

  double elastic;          //!< If sab_frac is not 1 or 0, then this value is
                           //!<   averaged over bound and non-bound nuclei
  double thermal;          //!< Bound thermal elastic & inelastic scattering
  double thermal_elastic;  //!< Bound thermal elastic scattering
  double photon_prod;      //!< microscopic photon production xs

  // Cross sections for depletion reactions (note that these are not stored in
  // macroscopic cache)
  double reaction[DEPLETION_RX.size()];

  // Indicies and factors needed to compute cross sections from the data tables
  int index_grid;        //!< Index on nuclide energy grid
  int index_temp;        //!< Temperature index for nuclide
  double interp_factor;  //!< Interpolation factor on nuc. energy grid
  int index_sab {-1};    //!< Index in sab_tables
  int index_temp_sab;    //!< Temperature index for sab_tables
  double sab_frac;       //!< Fraction of atoms affected by S(a,b)
  bool use_ptable;       //!< In URR range with probability tables?

  // Energy and temperature last used to evaluate these cross sections.  If
  // these values have changed, then the cross sections must be re-evaluated.
  double last_E {0.0};      //!< Last evaluated energy
  double last_sqrtkT {0.0}; //!< Last temperature in sqrt(Boltzmann constant
                            //!< * temperature (eV))
};

//==============================================================================
// MATERIALMACROXS contains cached macroscopic cross sections for the material a
// particle is traveling through
//==============================================================================

struct MaterialMacroXS {
  double total;         //!< macroscopic total xs
  double absorption;    //!< macroscopic absorption xs
  double fission;       //!< macroscopic fission xs
  double nu_fission;    //!< macroscopic production xs
  double photon_prod;   //!< macroscopic photon production xs

  // Photon cross sections
  double coherent;        //!< macroscopic coherent xs
  double incoherent;      //!< macroscopic incoherent xs
  double photoelectric;   //!< macroscopic photoelectric xs
  double pair_production; //!< macroscopic pair production xs
};

//==============================================================================
// Data for a nuclide
//==============================================================================

class Nuclide {
public:
  // Types, aliases
  using EmissionMode = ReactionProduct::EmissionMode;
  struct EnergyGrid {
    std::vector<int> grid_index;
    std::vector<double> energy;
  };

  // Constructors
  Nuclide(hid_t group, const std::vector<double>& temperature, int i_nuclide);

  //! Initialize logarithmic grid for energy searches
  void init_grid();

  void calculate_xs(int i_sab, double E, int i_log_union,
    double sqrtkT, double sab_frac);

  void calculate_sab_xs(int i_sab, double E, double sqrtkT, double sab_frac);

  // Methods
  double nu(double E, EmissionMode mode, int group=0) const;
  void calculate_elastic_xs() const;

  //! Determines the microscopic 0K elastic cross section at a trial relative
  //! energy used in resonance scattering
  double elastic_xs_0K(double E) const;

  //! \brief Determines cross sections in the unresolved resonance range
  //! from probability tables.
  void calculate_urr_xs(int i_temp, double E) const;

  // Data members
  std::string name_; //!< Name of nuclide, e.g. "U235"
  int Z_; //!< Atomic number
  int A_; //!< Mass number
  int metastable_; //!< Metastable state
  double awr_; //!< Atomic weight ratio
  int i_nuclide_; //!< Index in the nuclides array

  // Temperature dependent cross section data
  std::vector<double> kTs_; //!< temperatures in eV (k*T)
  std::vector<EnergyGrid> grid_; //!< Energy grid at each temperature
  std::vector<xt::xtensor<double, 2>> xs_; //!< Cross sections at each temperature

  // Multipole data
  std::unique_ptr<WindowedMultipole> multipole_;

  // Fission data
  bool fissionable_ {false}; //!< Whether nuclide is fissionable
  bool has_partial_fission_ {false}; //!< has partial fission reactions?
  std::vector<Reaction*> fission_rx_; //!< Fission reactions
  int n_precursor_ {0}; //!< Number of delayed neutron precursors
  std::unique_ptr<Function1D> total_nu_; //!< Total neutron yield
  std::unique_ptr<Function1D> fission_q_prompt_; //!< Prompt fission energy release
  std::unique_ptr<Function1D> fission_q_recov_; //!< Recoverable fission energy release

  // Resonance scattering information
  bool resonant_ {false};
  std::vector<double> energy_0K_;
  std::vector<double> elastic_0K_;
  std::vector<double> xs_cdf_;

  // Unresolved resonance range information
  bool urr_present_ {false};
  int urr_inelastic_ {C_NONE};
  std::vector<UrrData> urr_data_;

  std::vector<std::unique_ptr<Reaction>> reactions_; //!< Reactions
  std::array<size_t, 892> reaction_index_; //!< Index of each reaction
  std::vector<int> index_inelastic_scatter_;

private:
  void create_derived();

  static int XS_TOTAL;
  static int XS_ABSORPTION;
  static int XS_FISSION;
  static int XS_NU_FISSION;
  static int XS_PHOTON_PROD;
};

//==============================================================================
// Non-member functions
//==============================================================================

//! Checks for the right version of nuclear data within HDF5 files
void check_data_version(hid_t file_id);

extern "C" bool multipole_in_range(const Nuclide* nuc, double E);

//==============================================================================
// Global variables
//==============================================================================

namespace data {

// Minimum/maximum transport energy for each particle type. Order corresponds to
// that of the ParticleType enum
extern std::array<double, 2> energy_min;
extern std::array<double, 2> energy_max;

extern std::vector<std::unique_ptr<Nuclide>> nuclides;
extern std::unordered_map<std::string, int> nuclide_map;

} // namespace data

namespace simulation {

// Cross section caches
extern NuclideMicroXS* micro_xs;
extern "C" MaterialMacroXS material_xs;
#pragma omp threadprivate(micro_xs, material_xs)

} // namespace simulation

//==============================================================================
// Fortran compatibility
//==============================================================================

extern "C" void set_micro_xs();
extern "C" void nuclide_calculate_urr_xs(bool use_mp, int i_nuclide,
                                         int i_temp, double E);

} // namespace openmc

#endif // OPENMC_NUCLIDE_H
