# Mathematical foundations

## Conventions

- Right-handed coordinates, **+Y up**. The default camera looks down −Z.
- All scalars are `double` (`rt::Real`). On x86-64 scalar double arithmetic costs about the same as float, and the extra precision removes most self-intersection and cancellation problems without fragile epsilons.
- Colours are linear RGB stored in `Vec3`; `*` between two `Vec3` is component-wise (it is how a surface's albedo filters light).
- Directions produced by the camera and materials are unit length. Intersection routines do **not** assume this (the BVH equivalence tests use arbitrary directions), and `t` is always in units of the direction vector.
- Image pixel (0, 0) is the top-left; rows go down.

## Vectors

`dot(a, b) = |a||b| cos θ` measures alignment; `cross(a, b)` is perpendicular to both with length `|a||b| sin θ`, oriented by the right-hand rule (`x × y = z`). The tests check these identities (anti-commutativity, the scalar triple product, Lagrange's identity `|a×b|² = |a|²|b|² − (a·b)²`).

**Normalisation is guarded.** `normalize_or(v, fallback)` returns `fallback` when `|v|²` is below `1e-18` or not finite, instead of dividing by (almost) zero and producing NaN. NaNs are dangerous in a renderer because they propagate silently into every later sample of a pixel.

## Rays and intervals

A ray is `P(t) = O + t·D`. Hits are accepted only in an open interval `(t_min, t_max)`: `t_min` keeps secondary rays from re-hitting their own origin, `t_max` lets shadow rays stop at the light and lets the BVH discard boxes behind the current closest hit.

## Camera

Given eye `E`, target `T`, up `U` and vertical field of view θ:

```
w = normalize(E − T)            (points backwards)
u = normalize(U × w)            (right)
v = w × u                       (up)
viewport height = 2 tan(θ/2) · focus_distance,  width = height · aspect
```

Pixel `(i, j)` with sub-pixel offset `(sx, sy)` maps to `corner + (i+sx)·Δu + (j+sy)·Δv`. If `U` is parallel to the view direction the basis would collapse, so another up vector is chosen. With a non-zero aperture the ray origin is jittered over a disk of radius `aperture/2` in the `u,v` plane, which gives thin-lens depth of field: points at the focus distance stay sharp.

## Intersections

### Sphere

Substituting the ray into `|P − C|² = r²` gives `a t² − 2h t + c = 0` with `a = D·D`, `h = D·(C − O)`, `c = |C − O|² − r²`. Two precision problems appear for small or distant spheres, and the code avoids both (Haines et al., *Ray Tracing Gems*, ch. 7):

1. The textbook discriminant `h² − ac` subtracts two huge, nearly equal numbers. Instead we compute `a(r² − |v|²)` where `v = (C − O) − (h/a)D` is the perpendicular from the centre to the ray — a small quantity computed directly.
2. Computing both roots as `(h ± √disc)/a` loses the smaller one to cancellation. With `q = h + sign(h)√disc` the roots are `q/a` and `c/q`, neither of which subtracts nearly equal values.

A test intersects a sphere of radius 1 mm at 10 km and requires 12 significant digits; the naive formula failed it at about 1e-10 relative error before the fix.

### Plane

`t = ((P₀ − O)·N) / (D·N)`, rejected when `|D·N| < 1e-12` (ray parallel to the plane).

### Triangle (Möller–Trumbore)

Solve `O + tD = (1−b₁−b₂)V₀ + b₁V₁ + b₂V₂` by Cramer's rule. With `E₁ = V₁−V₀`, `E₂ = V₂−V₀`, `P = D×E₂`, `det = E₁·P`, `T = O−V₀`, `Q = T×E₁`:

```
b₁ = (T·P)/det,  b₂ = (D·Q)/det,  t = (E₂·Q)/det
```

A hit requires `b₁ ≥ 0, b₂ ≥ 0, b₁+b₂ ≤ 1`. `|det| < 1e-14` means the ray is parallel or the triangle degenerate. Triangles are two-sided; the outward normal follows counter-clockwise winding `E₁×E₂`. With vertex normals, the shading normal is interpolated with the barycentrics and then kept in the geometric normal's hemisphere — otherwise interpolated normals can point "into" the surface near silhouettes and light leaks.

### Axis-aligned box (slab test)

For each axis the ray is inside the slab between `t₀ = (min − O)·(1/D)` and `t₁ = (max − O)·(1/D)`; the box is hit if the intersection of the three intervals with `(t_min, t_max)` is non-empty. IEEE infinities make axis-parallel rays work for free (`1/0 = ∞`). The one trap is a parallel ray whose origin lies exactly on a slab plane: `0·∞ = NaN`. That case means the ray lies in the closed slab for all `t`, so the axis is skipped. The tests cover it for both `+0.0` and `−0.0` directions — the first version of the fix handled only `+0.0`, and the test with `−0.0` caught it.

## Optics

**Reflection:** `R = D − 2(D·N)N`.

**Refraction (Snell):** with `η = n₁/n₂` and `cos θᵢ = −D·N`,

```
sin²θₜ = η²(1 − cos²θᵢ)
if sin²θₜ > 1: total internal reflection
T = ηD + (η cos θᵢ − cos θₜ)N
```

**Fresnel (Schlick):** `F(θ) ≈ R₀ + (1 − R₀)(1 − cos θ)⁵`, `R₀ = ((1−η)/(1+η))²`. At normal incidence on glass `R₀ = 0.04`; at grazing angles `F → 1`. For a ray leaving glass, `η = n_glass` and `θ` is the incident angle inside the glass; beyond the critical angle `asin(1/1.5) ≈ 41.8°` only reflection is possible. The tests check all of these numerically, including Snell's law at oblique incidence on entry and exit.

## Orthonormal basis

Sampling generates directions around +Z and rotates them into a frame around the surface normal. The frame uses the branchless construction of Duff et al. (2017), which has no special case and no singularity except the measure-zero `n.z = −1` handled by `copysign`. Tested for orthonormality and right-handedness on random normals and on the poles.

## Numerical tolerances

| Constant | Value | Role |
|---|---|---|
| `kEpsilon` | 1e-9 | length below which normalisation falls back |
| `tol::kRayTMin` | 1e-6 | minimum hit distance for secondary rays |
| `tol::spawn_offset(p)` | 1.6e-6 · max(1, ‖p‖∞) | distance a new ray origin is pushed off a surface along the geometric normal, scaled with position so it stays meaningful far from the origin |
| Möller–Trumbore `|det|` | 1e-14 | parallel / degenerate triangle |
| Degenerate triangle area | 1e-14 | dropped at scene compile time |

Offsetting along the geometric normal (to the side the new ray travels) is what removes "shadow acne" from secondary rays; `t_min` alone is not enough for large coordinates.

One more lesson from the tests: procedural textures must not depend on the sign of rounding noise. The checkerboard on a `y = 0` plane originally used `floor(p.y)`, and hit points at `y = ±1e-17` flipped the parity in a visible arc across the floor. The pattern now adds a small fixed offset before flooring.
