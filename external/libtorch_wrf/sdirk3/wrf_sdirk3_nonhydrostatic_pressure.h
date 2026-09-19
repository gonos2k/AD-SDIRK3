#pragma once

#include <torch/torch.h>

#include <string>

namespace wrf::sdirk3::nonhydrostatic_pressure {

// Physical-core layout (there are no halos in this helper):
//   p_pert/php       [ny, nz, nx]
//   dpn_u            [ny, nz+1, nx+1]
//   dpn_v            [ny+1, nz+1, nx]
//   term4_u         [ny, nz,   nx+1]
//   term4_v         [ny+1, nz, nx]
//
// x is periodic, and scalar fields on y faces use WRF's symmetric policy:
// q(-j)=q(j-1), q(ny+j)=q(ny-j-1).  Thus the two outer V faces duplicate
// the adjacent mass row.  This is the native physical-core contract; halo
// exchange and packed-tile extension belong to a caller.
struct Result {
    torch::Tensor dpn_u;
    torch::Tensor dpn_v;

    // dpx/dpy are the positive-oriented Fortran pressure-gradient scalars
    // before the outer update: ru_tend -= cqu*dpx, rv_tend -= cqv*dpy.
    // tendency_u/v are the same term with cqu=cqv=1, including that minus.
    // [ny,nz,nx+1] and [ny+1,nz,nx]: mass-level term-4 scalars.
    torch::Tensor term4_u;
    torch::Tensor term4_v;
    torch::Tensor tendency_u;
    torch::Tensor tendency_v;
};

inline void require(bool ok, const std::string& message) {
    TORCH_CHECK(ok, "nonhydrostatic-pressure kernel: ", message);
}

inline void require_same(const torch::Tensor& reference, const torch::Tensor& value,
                         const char* name) {
    require(value.defined(), std::string(name) + " is undefined");
    require(value.device() == reference.device(), std::string(name) + " device mismatch");
    require(value.scalar_type() == reference.scalar_type(), std::string(name) + " dtype mismatch");
}

// Mass-to-U averaging.  roll(...,+1) supplies i-1 and the final column is
// the periodic alias of the i=0 face.  The formula is also valid for nx=1.
inline torch::Tensor mass_to_u_periodic(const torch::Tensor& mass) {
    const int64_t xdim = mass.dim() - 1;
    const auto faces = 0.5 * (mass + torch::roll(mass, 1, {xdim}));
    return torch::cat({faces, faces.slice(xdim, 0, 1)}, xdim);
}

// Mass-to-V averaging.  The first and last V faces are symmetric copies;
// interior faces average the two adjacent mass rows.  This is valid for ny=1.
inline torch::Tensor mass_to_v_symmetric(const torch::Tensor& mass) {
    const auto ny = mass.size(0);
    if (ny == 1) return torch::cat({mass, mass}, 0);
    const auto interior = 0.5 * (mass.slice(0, 1, ny) + mass.slice(0, 0, ny - 1));
    return torch::cat({mass.slice(0, 0, 1), interior, mass.slice(0, ny - 1, ny)}, 0);
}

inline torch::Tensor make_dpn_u(const torch::Tensor& p_pert,
                                double cf1, double cf2, double cf3,
                                const torch::Tensor& interior_fnm,
                                const torch::Tensor& interior_fnp,
                                bool top_lid, double cfn, double cfn1) {
    const auto nz = p_pert.size(1);
    const auto p_u = mass_to_u_periodic(p_pert);
    const auto bottom = cf1 * p_u.select(1, 0)
                      + cf2 * p_u.select(1, 1)
                      + cf3 * p_u.select(1, 2);
    const auto weights_m = interior_fnm.view({1, nz - 1, 1});
    const auto weights_p = interior_fnp.view({1, nz - 1, 1});
    const auto interior = weights_m * p_u.slice(1, 1, nz)
                        + weights_p * p_u.slice(1, 0, nz - 1);
    const auto top = top_lid
        ? cfn * p_u.select(1, nz - 1) + cfn1 * p_u.select(1, nz - 2)
        : torch::zeros_like(p_u.select(1, nz - 1));
    return torch::cat({bottom.unsqueeze(1), interior, top.unsqueeze(1)}, 1);
}

inline torch::Tensor make_dpn_v(const torch::Tensor& p_pert,
                                double cf1, double cf2, double cf3,
                                const torch::Tensor& interior_fnm,
                                const torch::Tensor& interior_fnp,
                                bool top_lid, double cfn, double cfn1) {
    const auto nz = p_pert.size(1);
    const auto p_v = mass_to_v_symmetric(p_pert);
    const auto bottom = cf1 * p_v.select(1, 0)
                      + cf2 * p_v.select(1, 1)
                      + cf3 * p_v.select(1, 2);
    const auto weights_m = interior_fnm.view({1, nz - 1, 1});
    const auto weights_p = interior_fnp.view({1, nz - 1, 1});
    const auto interior = weights_m * p_v.slice(1, 1, nz)
                        + weights_p * p_v.slice(1, 0, nz - 1);
    const auto top = top_lid
        ? cfn * p_v.select(1, nz - 1) + cfn1 * p_v.select(1, nz - 2)
        : torch::zeros_like(p_v.select(1, nz - 1));
    return torch::cat({bottom.unsqueeze(1), interior, top.unsqueeze(1)}, 1);
}

// Pure tensor implementation of WRF's NH dpn interpolation and term 4.
//
// rdnw is the signed Fortran metric (negative for WRF's downward eta
// coordinate).  The repository's production C++ storage uses |rdnw|, so a
// production caller must pass -rdnw_magnitude here.  Keeping the sign in this
// API makes the Fortran identity visible:
//   vertical = rdnw*(dpn[k+1]-dpn[k]) - c1h[k]*mu_face,
//   where mu_face=.5*(mu_pert_left+mu_pert_right).  mu_pert is WRF's
//   perturbation column mass mu_2; full mass mub+mu_2 is not accepted here.
//
// php_mass is Fortran's php, already equal to
// .5*(phb[k]+phb[k+1]+ph[k]+ph[k+1]); this helper intentionally does not
// recompute that separate vertical average.
//
// interior_fnm/fnp have length nz-1.  Entry [k-1] is Fortran fnm/fnp(k) for
// W level k=2..nz (zero-based W level 1..nz-1).  All tensor operands share
// p_pert's device and dtype.  c1h and rdnw have length nz.
inline Result native_nonhydrostatic_pressure_core(
    const torch::Tensor& p_pert,
    const torch::Tensor& php_mass,
    const torch::Tensor& msfux,
    const torch::Tensor& msfuy,
    const torch::Tensor& msfvx,
    const torch::Tensor& msfvy,
    const torch::Tensor& mu_pert,
    const torch::Tensor& c1h,
    const torch::Tensor& rdnw,
    double rdx, double rdy,
    double cf1, double cf2, double cf3,
    const torch::Tensor& interior_fnm,
    const torch::Tensor& interior_fnp,
    bool top_lid, double cfn, double cfn1) {
    require(p_pert.defined() && p_pert.dim() == 3, "p_pert must be rank-3 [ny,nz,nx]");
    const auto ny = p_pert.size(0);
    const auto nz = p_pert.size(1);
    const auto nx = p_pert.size(2);
    require(ny >= 1 && nx >= 1 && nz >= 3, "require ny,nx >= 1 and nz >= 3");

    for (const auto* operand : {&php_mass, &msfux, &msfuy, &msfvx, &msfvy,
                                &mu_pert, &c1h, &rdnw, &interior_fnm, &interior_fnp})
        require_same(p_pert, *operand, "operand");

    require(php_mass.sizes() == torch::IntArrayRef({ny, nz, nx}),
            "php_mass shape must be [ny,nz,nx]");
    require(msfux.sizes() == torch::IntArrayRef({ny, nx + 1}),
            "msfux shape must be [ny,nx+1]");
    require(msfuy.sizes() == torch::IntArrayRef({ny, nx + 1}),
            "msfuy shape must be [ny,nx+1]");
    require(msfvx.sizes() == torch::IntArrayRef({ny + 1, nx}),
            "msfvx shape must be [ny+1,nx]");
    require(msfvy.sizes() == torch::IntArrayRef({ny + 1, nx}),
            "msfvy shape must be [ny+1,nx]");
    require(mu_pert.sizes() == torch::IntArrayRef({ny, nx}),
            "mu_pert shape must be [ny,nx]");
    require(c1h.dim() == 1 && c1h.numel() == nz, "c1h length must be nz");
    require(rdnw.dim() == 1 && rdnw.numel() == nz, "rdnw length must be nz");
    require(interior_fnm.dim() == 1 && interior_fnm.numel() == nz - 1,
            "interior_fnm length must be nz-1");
    require(interior_fnp.dim() == 1 && interior_fnp.numel() == nz - 1,
            "interior_fnp length must be nz-1");

    const auto dpn_u = make_dpn_u(p_pert, cf1, cf2, cf3, interior_fnm,
                                  interior_fnp, top_lid, cfn, cfn1);
    const auto dpn_v = make_dpn_v(p_pert, cf1, cf2, cf3, interior_fnm,
                                  interior_fnp, top_lid, cfn, cfn1);

    // php is already a mass-point quantity.  Fortran's term 4 takes its
    // pressure difference directly at the face; it does not average php to
    // the velocity point first.  Build the periodic/symmetric face difference
    // explicitly, then apply the same difference to the dpn W-level operator.
    const auto dphp_u_core = php_mass - torch::roll(php_mass, 1, {2});
    const auto dphp_u = torch::cat({dphp_u_core, dphp_u_core.slice(2, 0, 1)}, 2);
    const auto zero_y = torch::zeros_like(php_mass.slice(0, 0, 1));
    const auto dphp_v = torch::cat({zero_y,
                                    php_mass.slice(0, 1, ny) - php_mass.slice(0, 0, ny - 1),
                                    zero_y}, 0);

    const auto dpn_diff_u = dpn_u.slice(1, 1, nz + 1) - dpn_u.slice(1, 0, nz);
    const auto dpn_diff_v = dpn_v.slice(1, 1, nz + 1) - dpn_v.slice(1, 0, nz);
    const auto rdnw_view = rdnw.view({1, nz, 1});
    const auto c1h_view = c1h.view({1, nz, 1});
    const auto mu_u = mass_to_u_periodic(mu_pert);
    const auto mu_v = mass_to_v_symmetric(mu_pert);

    // Fortran writes -.5*(c1h*mu_left+c1h*mu_right).  Because mu_u/mu_v
    // are already the .5 stagger averages, the equivalent factor is simply
    // -c1h*mu_face; applying another .5 would halve this correction.
    const auto vertical_u = rdnw_view * dpn_diff_u
                          - c1h_view * mu_u.unsqueeze(1);
    const auto vertical_v = rdnw_view * dpn_diff_v
                          - c1h_view * mu_v.unsqueeze(1);
    const auto term4_u = (msfux / msfuy).unsqueeze(1) * rdx * dphp_u * vertical_u;
    const auto term4_v = (msfvy / msfvx).unsqueeze(1) * rdy * dphp_v * vertical_v;

    return {dpn_u, dpn_v, term4_u, term4_v, -term4_u, -term4_v};
}

}  // namespace wrf::sdirk3::nonhydrostatic_pressure
