# Finite C-grid acoustic Schur illustration

This is a constant-coefficient, one-dimensional mathematical prototype, not an implementation or forecast result. It uses 80 mass cells, 79 interior normal-velocity faces, wall velocities fixed at zero, and dy = 100 km. The ARK324 implicit diagonal is exactly 1767732205903/4055673282236 in `external/libtorch_wrf/sdirk3/wrf_sdirk3_imex_ark324_coeffs.h` (implicit stages 2–4); h=600 s gives a=h gamma=261.519912905 s. We choose c=340 m/s and rho=1 kg/m³ solely for illustration; these are not inferred WRF coefficients. No damping is included.

## Operators and energy

For mass cells i=0,...,79 and interior faces j=1,...,79, impose v_0=v_80=0, and set (Dv)_i=(v_{i+1}-v_i)/dy. For interior faces (Gp)_j=(p_j-p_{j-1})/dy. Thus G=-Dᵀ with equal cell and face weights; a constant pressure has zero gradient. D has full column rank, so G has only the constant pressure null mode and no alternating pressure checkerboard null mode. At the walls, zero exterior flux produces the first and last one-sided rows of D; no independent wall velocity unknown exists. The corresponding pressure Laplacian is the cell-centered zero-normal-gradient (Neumann) operator.

Take v_t=-(1/rho)Gp, p_t=-rho c² Dv. Units are [v]=m/s, [p]=Pa, [rho]=kg/m³, [c]=m/s, [D]=[G]=1/m. The discrete acoustic energy E=(dy/2)(rho ||v||²+||p||²/(rho c²)) has derivative -dy(vᵀGp+pᵀDv)=0. Equivalently z=p/(rho c) has velocity units and the generator of (v,z) is -c [[0,G],[D,0]], which is skew-symmetric. These identities hold for these explicit boundary and inner-product definitions, not automatically for WRF's weighted terrain-following operators.

## Stage solve and spectrum

For an implicit stage, the scaled linear system is A [v,z]ᵀ=[b_v,b_z]ᵀ with A=[[I, acG],[acD,I]]. Elimination gives

    S z = b_z - ac D b_v,  S = I - a²c² D G = I + a²c² DDᵀ,
    v = b_v - ac G z.

For any nonzero z, zᵀSz=||z||²+a²c²||Gz||²>0; S is symmetric positive definite even on constant z. Let q=ac/dy. S has diagonal 1+q² at the two endpoints, 1+2q² at interior cells, and off-diagonals -q². It has pressure modes cos(pi k(i+1/2)/80), k=0,...,79, and eigenvalues 1+4q² sin²(pi k/160). The constant mode is 1 and high modes are larger, not null. Its condition number is (1+4q² cos²(pi/160))/1. No free smoothing parameter occurs. Factor the derived tridiagonal SPD S, solve it, and recover v as above; this is the exact block inverse of this simplified model.

## Reproduction and scope

Run `python3 derive.py` with NumPy, SciPy, and Matplotlib; output is `results.json` and `spectrum_and_gmres.png`. The right-hand side is equal-amplitude face sine modes k=2 and k=78, with zero pressure component, normalized in scaled variables. SciPy GMRES uses rtol=1e-10 and restart 159; true residuals ||b-Ax||/||b|| are explicitly recomputed. Identity represents a vertical-only/local preconditioner *only in this horizontal-only illustration*. Identity needs four iterations (2.65e-15 true relative residual), while the exact full horizontal block inverse needs one (2.69e-16). This four-iteration result is specific to two ideal eigenmode pairs and does not predict WRF iteration counts or wall time. The plotted callback residual is SciPy's preconditioned residual, not the independently checked true residual. The calculated S condition number is 4.1613, and its k=2 and k=78 eigenvalues are 1.00487 and 4.15760. Finite matrix eigenvalues agree with the formula to 2.7e-15.

The smallest principled integration candidate is to investigate adding the *actual linearized* horizontal divergence–pressure-gradient coupling to the existing local/vertical preconditioner, via a coefficient- and boundary-consistent Schur solve or approximation, retaining existing fallback. First derive its WRF state-variable mapping, units, density/metric/stagger and boundary weights, parallel interfaces, vertical/local coupling, and correspondence to the configured fast RHS and Jacobian; demonstrate the appropriate weighted adjoint relation or account for its failure. Variable coefficients, backgrounds, advection, full three-dimensional terrain and boundary treatment can make the actual operator nonsymmetric or non-normal; the constant-coefficient SPD result alone cannot establish those properties. This prototype does not establish native WRF convergence, performance, stability, or forecast quality, and no WRF run or RK3 comparison was performed.
