#pragma once

// Independent 4-sigma linear reference for the dry compressible WRF equations.
// This is a source transcription of dyn_em, not a call into the production RHS,
// Jacobian, JVP, or Newton solver. It reduces a y-uniform periodic-x Fourier mode.
#include "tile_test_fixture.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <limits>
#include <stdexcept>

namespace wrf::sdirk3::test::stable_wave_reference {

using Complex = std::complex<double>;
constexpr int mass_levels = 4;
constexpr int w_levels = mass_levels + 1;
constexpr int state_size = 17;
constexpr int u_offset = 0;
constexpr int w_offset = 4;       // Fortran W levels k=2..kde; bottom k=1 is fixed.
constexpr int phi_offset = 8;     // Fortran PH levels k=2..kde; bottom k=1 is fixed.
constexpr int theta_offset = 12;
constexpr int mu_offset = 16;
using State = std::array<Complex, state_size>;

struct RhsParts {
    State total{};
    std::array<Complex, 4> w_pressure{};
    std::array<Complex, 4> w_buoyancy{};
    std::array<Complex, 5> omega{};
    std::array<Complex, 4> pressure_perturbation{};
    std::array<Complex, 4> alpha_perturbation{};
};

struct EnergyParts {
    double kinetic_u = 0.0;
    double kinetic_w = 0.0;
    double acoustic = 0.0;
    double available_potential = 0.0;
    // Candidate free-surface contribution. Kept separate because the top-row
    // boundary work must be diagnosed before asserting conservation.
    double top_surface_candidate = 0.0;
    double bulk() const {
        return kinetic_u + kinetic_w + acoustic + available_potential;
    }
};

struct Column {
    double dx = 5000.0;
    double dy = 5000.0;
    double gravity = 9.81;
    double rd = 287.0;
    double cp = 1004.5;
    double cv = 717.5;
    double p0 = 100000.0;
    double t0 = 300.0;
    double mub = 80000.0;
    double mu_bar = 4000.0;
    double eta_delta = 0.25;
    // Mass levels bottom-to-top. These are read from the exact balanced fixture.
    std::array<double, mass_levels> theta{};
    std::array<double, mass_levels> p_base{};
    std::array<double, mass_levels> p_bar{};
    std::array<double, mass_levels> alpha_base{};
    std::array<double, mass_levels> alpha_bar{};
    std::array<double, mass_levels> layer_mass{};
    std::array<double, mass_levels> layer_height{};
    std::array<double, mass_levels> layer_density{};
    std::array<double, mass_levels> theta_z{};
    double max_geometry_alpha_relative_error = 0.0;
    // W-face geopotential includes PHB + perturbation PH. Perturbation-only
    // values are retained separately for the pressure-coordinate PGF term.
    std::array<double, w_levels> phi_total_w{};
    std::array<double, w_levels> phi_pert_w{};
    std::array<double, w_levels> phi_base_w{};
    // Fixture values: rdnw=rdn=-4, dnw=-.25, fnm=fnp=.5, c1=1,c2=0.
    std::array<double, mass_levels> rdnw{};
    std::array<double, mass_levels> rdn{};
    std::array<double, mass_levels> fnm{};
    std::array<double, mass_levels> fnp{};
    std::array<double, w_levels> c1f{};
    std::array<double, w_levels> c2f{};
    std::array<double, mass_levels> c1h{};
    std::array<double, mass_levels> c2h{};

    static Column fromStableFixture(const TileCase& tile, const torch::Tensor& background) {
        TORCH_CHECK(background.defined() && background.dim() == 1 &&
                    background.numel() == total &&
                    background.scalar_type() == torch::kFloat64,
                    "stable-wave reference requires the exact packed FP64 fixture background");
        TORCH_CHECK(tile.u.size() == su && tile.v.size() == sv &&
                    tile.w.size() == sw && tile.theta.size() == st &&
                    tile.mu.size() == sm,
                    "stable-wave reference is the four-layer TileCase geometry");
        TORCH_CHECK(tile.spacing > 0.0f && std::isfinite(tile.spacing),
                    "stable-wave reference requires finite positive dx");

        Column out;
        out.dx = tile.spacing;
        out.dy = tile.spacing;
        const auto grid = tile.solver.getGridInfo();
        TORCH_CHECK(grid && grid->g > 0.0f && std::isfinite(grid->g),
                    "stable-wave reference requires the fixture gravity");
        out.gravity = grid->g;

        auto packed = background.contiguous();
        const int64_t ph_begin = su + sv + sw;
        const int64_t theta_begin = su + sv + 2*sw;
        const int64_t mu_begin = total - sm;
        const auto phi_pert = packed.slice(0, ph_begin, ph_begin + sw).view({ny,nw,nx});
        const auto theta_pert = packed.slice(0, theta_begin, theta_begin + st).view({ny,nz,nx});
        const auto mu_pert = packed.slice(0, mu_begin, total).view({ny,nx});

        const auto mub_t = tile.baseMass().to(torch::kFloat64).contiguous();
        const auto pbase_t = tile.basePressure().to(torch::kFloat64).contiguous();
        const auto thbase_t = tile.baseTheta().to(torch::kFloat64).contiguous();
        const auto phbase_t = tile.basePhi().to(torch::kFloat64).contiguous();
        TORCH_CHECK(mub_t.sizes() == torch::IntArrayRef({ny,nx}) &&
                    pbase_t.sizes() == torch::IntArrayRef({ny,nz,nx}) &&
                    thbase_t.sizes() == torch::IntArrayRef({ny,nz,nx}) &&
                    phbase_t.sizes() == torch::IntArrayRef({ny,nw,nx}),
                    "stable-wave reference fixture base-array shapes changed");
        const auto muv = mu_pert.accessor<double,2>();
        const auto mub = mub_t.accessor<double,2>();
        const auto pb = pbase_t.accessor<double,3>();
        const auto thb = thbase_t.accessor<double,3>();
        const auto thp = theta_pert.accessor<double,3>();
        const auto php = phi_pert.accessor<double,3>();
        const auto phb = phbase_t.accessor<double,3>();

        out.mub = mub[0][0];
        out.mu_bar = muv[0][0];
        TORCH_CHECK(std::abs(out.mub-80000.0) < 1e-3 &&
                    std::abs(out.mu_bar-4000.0) < 1e-3,
                    "reference requires the exact 80-kPa base plus 4-kPa fixture mass perturbation");
        TORCH_CHECK(std::all_of(tile.metric.begin(), tile.metric.begin()+mass_levels,
                                [](float x){ return x == -4.0f; }) &&
                    std::all_of(tile.one.begin(), tile.one.begin()+mass_levels,
                                [](float x){ return x == 1.0f; }) &&
                    std::all_of(tile.zero.begin(), tile.zero.begin()+mass_levels,
                                [](float x){ return x == 0.0f; }),
                    "reference requires uniform four-layer sigma coefficients");
        for (int k=0; k<mass_levels; ++k) {
            out.rdnw[k] = tile.metric[k];
            out.rdn[k] = tile.metric[k];
            out.fnm[k] = tile.half[k];
            out.fnp[k] = tile.half[k];
            out.c1h[k] = tile.one[k];
            out.c2h[k] = tile.zero[k];
            out.c1f[k] = tile.one[k];
            out.c2f[k] = tile.zero[k];
        }
        out.c1f[mass_levels] = tile.one[mass_levels];
        out.c2f[mass_levels] = tile.zero[mass_levels];

        const auto uniform = [](double value, double reference, double tolerance) {
            return std::isfinite(value) && std::abs(value-reference) <= tolerance;
        };
        constexpr double uniform_tol = 2e-5;
        for (int k=0; k<mass_levels; ++k) {
            const double eta = 1.0-(static_cast<double>(k)+0.5)/mass_levels;
            const double mu_value = muv[0][0];
            out.p_base[k] = pb[0][k][0];
            out.theta[k] = thb[0][k][0] + thp[0][k][0];
            out.p_bar[k] = out.p_base[k] + eta*mu_value;
            TORCH_CHECK(uniform(muv[0][0],mu_value,uniform_tol) &&
                        uniform(thp[0][k][0],10.0+2.0*k,uniform_tol) &&
                        uniform(out.theta[k],310.0+2.0*k,uniform_tol),
                        "background is not the exact stable theta=310,312,314,316 fixture");
            for (int j=0; j<ny; ++j) {
                TORCH_CHECK(uniform(muv[j][0],mu_value,uniform_tol),
                            "stable reference requires y-uniform background MU");
                for (int i=0; i<nx; ++i) {
                    TORCH_CHECK(uniform(muv[j][i],mu_value,uniform_tol) &&
                                uniform(thb[j][k][i]+thp[j][k][i],out.theta[k],uniform_tol) &&
                                uniform(pb[j][k][i],out.p_base[k],uniform_tol),
                                "stable reference requires x/y-uniform background mass, theta and pressure");
                }
            }
            out.layer_mass[k] = out.c1h[k]*(out.mub+out.mu_bar)+out.c2h[k];
            out.alpha_base[k] = out.rd*out.t0/out.p0 *
                std::pow(out.p_base[k]/out.p0,-out.cv/out.cp);
            out.alpha_bar[k] = out.rd*out.theta[k]/out.p0 *
                std::pow(out.p_bar[k]/out.p0,-out.cv/out.cp);
            const double temperature = out.theta[k] *
                std::pow(out.p_bar[k]/out.p0,out.rd/out.cp);
            out.layer_density[k] = out.p_bar[k]/(out.rd*temperature);
        }

        for (int f=0; f<w_levels; ++f) {
            out.phi_pert_w[f] = php[0][f][0];
            out.phi_base_w[f] = phb[0][f][0];
            out.phi_total_w[f] = out.phi_base_w[f]+out.phi_pert_w[f];
            for (int j=0; j<ny; ++j)
                for (int i=0; i<nx; ++i)
                    TORCH_CHECK(uniform(php[j][f][i],out.phi_pert_w[f],uniform_tol) &&
                                uniform(phb[j][f][i],out.phi_base_w[f],uniform_tol),
                                "stable reference requires x/y-uniform background geopotential");
        }
        std::array<double,mass_levels> z_center{};
        for (int k=0; k<mass_levels; ++k) {
            z_center[k] = 0.5*(out.phi_total_w[k]+out.phi_total_w[k+1])/out.gravity;
            out.layer_height[k] =
                (out.phi_total_w[k+1]-out.phi_total_w[k])/out.gravity;
            TORCH_CHECK(out.layer_height[k] > 0.0 && std::isfinite(out.layer_height[k]),
                        "stable reference requires upward-increasing full geopotential");
        }
        for (int k=0; k<mass_levels; ++k) {
            if (k==0) out.theta_z[k]=(out.theta[1]-out.theta[0])/(z_center[1]-z_center[0]);
            else if (k==mass_levels-1)
                out.theta_z[k]=(out.theta[k]-out.theta[k-1])/(z_center[k]-z_center[k-1]);
            else out.theta_z[k]=(out.theta[k+1]-out.theta[k-1])/(z_center[k+1]-z_center[k-1]);
        }
        TORCH_CHECK(out.theta_z.front() > 0.0 && out.theta_z.back() > 0.0,
                    "reference background is not statically stable");
        // Independent verification of the exact fixture PH against Fortran's
        // nonhydrostatic alpha/PH constraint, using no production EOS helper.
        for (int k=0; k<mass_levels; ++k) {
            const double dphi=out.phi_pert_w[k+1]-out.phi_pert_w[k];
            const double alpha_pert=-(out.c1h[k]*out.alpha_base[k]*out.mu_bar+
                                      out.rdnw[k]*dphi)/out.layer_mass[k];
            const double alpha_geometry=out.alpha_base[k]+alpha_pert;
            const double relative=std::abs(alpha_geometry-out.alpha_bar[k])/
                std::max(std::abs(out.alpha_bar[k]),std::numeric_limits<double>::min());
            out.max_geometry_alpha_relative_error=
                std::max(out.max_geometry_alpha_relative_error,relative);
        }
        TORCH_CHECK(out.max_geometry_alpha_relative_error <=
                        16.0*std::numeric_limits<float>::epsilon(),
                    "stable reference PH is inconsistent with source alpha geometry: relative error=",
                    out.max_geometry_alpha_relative_error);
        return out;
    }

    static constexpr int U(int k) { return u_offset+k; }
    static constexpr int W(int k) { return w_offset+k; }       // k=0..3: Fortran W k=2..5.
    static constexpr int PHI(int k) { return phi_offset+k; }   // k=0..3: Fortran PH k=2..5.
    static constexpr int THETA(int k) { return theta_offset+k; }

    double term4HydrostaticResidualMax() const {
        // HPG term 4 is D_x(php) times [rdnw*D_eta(p')-c1h*MU']. Its
        // background factor must vanish for this source-balanced column.
        double maximum = 0.0;
        for (int k=0; k<mass_levels; ++k) {
            const double eta_lo = 1.0-static_cast<double>(k)/mass_levels;
            const double eta_hi = 1.0-static_cast<double>(k+1)/mass_levels;
            const double p_lo = eta_lo*mu_bar;
            const double p_hi = eta_hi*mu_bar;
            const double residual = rdnw[k]*(p_hi-p_lo)-c1h[k]*mu_bar;
            maximum=std::max(maximum,std::abs(residual));
        }
        return maximum;
    }

    double term4HydrostaticResidualRelative() const {
        return term4HydrostaticResidualMax()/std::max(std::abs(mu_bar),1.0);
    }

    double scalarOrder3BackgroundCorrectionMax() const {
        // Fortran flux3's sign-dependent part. The stable theta profile is
        // exactly linear on uniform eta, so this directional term is zero.
        const double q0=theta[0], q1=theta[1], q2=theta[2], q3=theta[3];
        return std::abs(((q3-q0)-3.0*(q2-q1))/12.0);
    }

    RhsParts rhs(const State& q, int fourier_mode=1) const {
        TORCH_CHECK(fourier_mode > 0 && fourier_mode < nx/2,
                    "reference mode must be nonzero and below Nyquist");
        TORCH_CHECK(term4HydrostaticResidualMax() < 1e-6 &&
                    scalarOrder3BackgroundCorrectionMax() < 1e-12,
                    "source background does not cancel HPG term 4 / scalar upwind correction");
        const double kappa = 2.0*std::sin(std::acos(-1.0)*fourier_mode/nx)*static_cast<double>(1.0f/static_cast<float>(dx));
        const Complex ik(0.0,kappa);
        const Complex dmu = q[mu_offset];
        std::array<Complex,mass_levels> dalpha{}, dp{}, dphi_mass{}, divv{};
        std::array<Complex,w_levels> dphi_face{};
        std::array<double,w_levels> phi_full = phi_total_w;
        std::array<Complex,w_levels> omega{};
        dphi_face[0]=Complex{};  // Fixed surface geopotential perturbation.
        for (int f=1; f<w_levels; ++f) dphi_face[f]=q[PHI(f-1)];

        RhsParts result;
        for (int k=0; k<mass_levels; ++k) {
            const Complex dphi = dphi_face[k+1]-dphi_face[k];
            dalpha[k]=-(c1h[k]*alpha_bar[k]*dmu+rdnw[k]*dphi)/layer_mass[k];
            dp[k]=(cp/cv)*p_bar[k]*(q[THETA(k)]/theta[k]-dalpha[k]/alpha_bar[k]);
            dphi_mass[k]=0.5*(dphi_face[k]+dphi_face[k+1]);
            // Source-exact balanced base: c1h*MUbar is constant in x and
            // the vertical pressure-slope residual multiplying D_x(php) is 0.
            result.total[U(k)]=-ik*(dphi_mass[k]+alpha_bar[k]*dp[k]);
            result.alpha_perturbation[k]=dalpha[k];
            result.pressure_perturbation[k]=dp[k];
        }

        Complex mu_dot{};
        for (int k=0; k<mass_levels; ++k)
            mu_dot -= ik*(eta_delta*layer_mass[k])*q[U(k)];
        result.total[mu_offset]=mu_dot;

        // calc_ww_cp: integrate the same layerwise horizontal mass divergence
        // used for MU; bottom and top Omega are exactly zero.
        omega[0]=Complex{};
        for (int k=0; k<mass_levels; ++k) {
            const Complex horizontal_div=ik*layer_mass[k]*q[U(k)];
            divv[k]=horizontal_div;
            omega[k+1]=omega[k]+eta_delta*(c1h[k]*mu_dot+divv[k]);
        }
        result.omega=omega;

        // pg_buoy_w: 3 interior W rows use rdn*(p[k]-p[k-1])-MU;
        // Fortran's kde row uses its explicit 2*rdnw*(-p_last)-MU closure.
        for (int r=0; r<3; ++r) {
            const int k=r+1;  // mass pressure difference for Fortran W k=r+2.
            const double mass_w=c1f[r+1]*(mub+mu_bar)+c2f[r+1];
            result.w_pressure[r]=gravity/mass_w*rdn[k]*(dp[k]-dp[k-1]);
            result.w_buoyancy[r]=-gravity/mass_w*c1f[r+1]*dmu;
            result.total[W(r)]=result.w_pressure[r]+result.w_buoyancy[r];
        }
        const double top_mass=c1f[mass_levels]*(mub+mu_bar)+c2f[mass_levels];
        result.w_pressure[3]=gravity/top_mass*(2.0*rdnw[mass_levels-1]*(-dp[mass_levels-1]));
        result.w_buoyancy[3]=-gravity/top_mass*c1f[mass_levels]*dmu;
        result.total[W(3)]=result.w_pressure[3]+result.w_buoyancy[3];

        // WRF v_sca_adv_order=3. On this exact linear theta/eta profile the
        // source's sign-dependent flux3 correction vanishes; all active
        // background face values are the arithmetic midpoint.
        std::array<double,w_levels> theta_face{};
        theta_face[0]=theta[0];
        for (int f=1; f<mass_levels; ++f)
            theta_face[f]=0.5*(theta[f-1]+theta[f]);
        theta_face[mass_levels]=theta[mass_levels-1];
        for (int k=0; k<mass_levels; ++k) {
            const double lower_delta=k==0?0.0:theta_face[k]-theta[k];
            const double upper_delta=k==mass_levels-1?0.0:theta_face[k+1]-theta[k];
            result.total[THETA(k)]=(1.0/(eta_delta*layer_mass[k]))*
                (omega[k+1]*upper_delta-omega[k]*lower_delta);
        }

        // Dedicated native-wave contract: Fortran phi_adv_z=2. The ordinary
        // C++ path implements this centered face-Omega stencil; this test does
        // not claim equivalence to the separately defined default option 1.
        std::array<double,mass_levels> phi_eta{};
        for(int k=0;k<mass_levels;++k)
            phi_eta[k]=rdnw[k]*(phi_full[k+1]-phi_full[k]);
        for(int r=0;r<4;++r) {
            const double mass_w=c1f[r+1]*(mub+mu_bar)+c2f[r+1];
            Complex phi_adv{};
            if(r<3) phi_adv=-omega[r+1]*(fnm[r+1]*phi_eta[r+1]+fnp[r+1]*phi_eta[r]);
            result.total[PHI(r)]=gravity*q[W(r)]+phi_adv/mass_w;
        }
        return result;
    }

    torch::Tensor matrixTensor(int fourier_mode=1) const {
        auto matrix=torch::empty({state_size,state_size},
            torch::TensorOptions().dtype(torch::kComplexDouble).device(torch::kCPU));
        auto* values=matrix.data_ptr<c10::complex<double>>();
        for (int col=0; col<state_size; ++col) {
            State basis{};
            basis[col]=Complex(1.0,0.0);
            const State image=rhs(basis,fourier_mode).total;
            for (int row=0; row<state_size; ++row)
                values[row*state_size+col]=c10::complex<double>(image[row].real(),image[row].imag());
        }
        return matrix;
    }

    EnergyParts physicalEnergy(const State& q, int fourier_mode=1) const {
        const double kappa=2.0*std::sin(std::acos(-1.0)*fourier_mode/nx)*static_cast<double>(1.0f/static_cast<float>(dx));
        (void)kappa; // Physical norm does not depend on the derivative symbol.
        std::array<Complex,mass_levels> dalpha{},dp{},phi_mass{},theta_eulerian{},p_eulerian{};
        std::array<Complex,w_levels> phi_face{};
        phi_face[0]=Complex{};
        for (int f=1; f<w_levels; ++f) phi_face[f]=q[PHI(f-1)];
        for (int k=0;k<mass_levels;++k) {
            dalpha[k]=-(c1h[k]*alpha_bar[k]*q[mu_offset]+rdnw[k]*
                (phi_face[k+1]-phi_face[k]))/layer_mass[k];
            dp[k]=(cp/cv)*p_bar[k]*(q[THETA(k)]/theta[k]-dalpha[k]/alpha_bar[k]);
            phi_mass[k]=0.5*(phi_face[k]+phi_face[k+1]);
            const Complex zeta=phi_mass[k]/gravity;
            p_eulerian[k]=dp[k]+layer_density[k]*gravity*zeta;
            theta_eulerian[k]=q[THETA(k)]-zeta*theta_z[k];
        }
        const double area=dx*dy*nx*ny;
        EnergyParts e;
        // Real Fourier mode q(x)=Re(qhat exp(i*k*x)): <Re(qhat)^2>=|qhat|^2/2;
        // the physical quadratic energy contributes its additional 1/2 factor.
        for (int k=0;k<mass_levels;++k) {
            const double cell_mass=layer_mass[k]*eta_delta/gravity;
            e.kinetic_u += 0.25*area*cell_mass*std::norm(q[U(k)]);
            const double gamma=cp/cv;
            e.acoustic += 0.25*area*layer_height[k]*
                std::norm(p_eulerian[k])/(gamma*p_bar[k]);
            const double n2=gravity/theta[k]*theta_z[k];
            TORCH_CHECK(n2>0.0 && std::isfinite(n2),
                        "available-potential energy requires N^2>0 in every layer");
            e.available_potential += 0.25*area*layer_height[k]*layer_density[k]*gravity*
                std::norm(theta_eulerian[k])/(theta[k]*theta_z[k]);
        }
        for (int r=0;r<4;++r) {
            const int face=r+1; // active Fortran W levels k=2..kde=5
            const double dual_mass=face<mass_levels
                ? 0.5*(layer_mass[face-1]+layer_mass[face])*eta_delta/gravity
                : 0.5*layer_mass[mass_levels-1]*eta_delta/gravity;
            e.kinetic_w+=0.25*area*dual_mass*std::norm(q[W(r)]);
        }
        const Complex zeta_top=phi_face[mass_levels]/gravity;
        const double rho_top=layer_density[mass_levels-1];
        e.top_surface_candidate=0.25*area*rho_top*gravity*std::norm(zeta_top);
        return e;
    }
};

} // namespace wrf::sdirk3::test::stable_wave_reference
