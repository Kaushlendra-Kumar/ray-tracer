# Rendering pipeline

For each pixel and each sample: pick a random point inside the pixel (box-filter anti-aliasing), generate a camera ray, estimate the radiance arriving along it, add it to the pixel's running sum. The final pixel is the average, tone-mapped and sRGB-encoded.

## The rendering equation

Light leaving a surface point `x` in direction `ωₒ`:

```
Lₒ(x, ωₒ) = Lₑ(x, ωₒ) + ∫_Ω f(x, ωᵢ, ωₒ) Lᵢ(x, ωᵢ) cos θᵢ dωᵢ
```

`Lₑ` is emission, `f` the BRDF, and `Lᵢ` is itself the outgoing light of whatever surface is visible along `ωᵢ`, so the equation is recursive. Terms:

- **Direct illumination** — light that reaches `x` straight from an emitter (one bounce from light to eye).
- **Indirect illumination** — light that arrives after bouncing off other surfaces first: the red tint on the Cornell box's white blocks, the glow of the emissive orbs on the floor.
- **Global illumination** — direct + indirect: the full solution of the equation.
- **Path tracing** — estimating that integral with Monte Carlo by following random paths from the camera, one direction per bounce.

## Monte Carlo estimation

An integral `∫ g(ω) dω` is estimated by drawing `ω` from a density `p(ω)` and averaging `g(ω)/p(ω)`. The estimator is **unbiased** (its expectation is the exact integral) for any `p` that is non-zero wherever `g` is; the closer `p` is to proportional to `g`, the lower the variance. Noise falls as `1/√N`: halving it costs four times the samples, which is why the convergence strip in the README doubles samples at every pass.

Along a path the renderer keeps a **throughput** `β`, the product of `f·cos/p` over all bounces so far; emission found at a vertex is added as `β·Lₑ`.

## Materials

Each material answers two questions: *sample* a new direction and return the weight `f·cos/p`, and (for diffuse) *evaluate* `f·cos` for a given direction, which light sampling needs.

| Material | Sampling | Weight `f cos / p` |
|---|---|---|
| Lambertian, `f = albedo/π` | cosine-weighted hemisphere, `p = cos/π` (Malley: uniform disk point lifted to the hemisphere) | exactly `albedo` — zero variance from the BRDF itself |
| Metal | mirror direction perturbed by `roughness · (random unit vector)`; absorbed if it points below the surface | `albedo` |
| Dielectric | reflect with probability `F` (Schlick), otherwise refract; always reflect on total internal reflection | `tint` (the `F` and `1/F` cancel) |
| Emissive | does not scatter; emits `color · intensity` from its front face (optionally both) | — |

The **rough metal** model (popularised by *Ray Tracing in One Weekend*) has no closed-form BRDF, so it cannot be evaluated for light sampling. It is therefore flagged as a sample-only lobe: the integrator never combines it with light sampling and counts emission found after it like after a mirror bounce. That keeps the estimator unbiased *for the material model it defines*; it is just not a physically derived microfacet BRDF (a GGX lobe would be the next step).

Emitters are **one-sided** by default. The first Cornell render used two-sided lights and was covered in fireflies: the ceiling light, 1 cm below the ceiling, also lit the ceiling right above it, creating an intensely bright hidden patch that diffuse paths found only rarely. Making the light emit downward only removed them.

## Next-event estimation (light sampling)

Hitting a small light by chance with BSDF sampling is rare, so at every diffuse vertex the path tracer also picks one light uniformly (probability `1/N`) and samples a point on it:

- **Point light:** the direction is fixed; contribution `f·cos · I / d²` (a delta light — it can only be found this way).
- **Sphere light:** sample a direction uniformly inside the cone the sphere subtends, `p = 1/(2π(1 − cos θₘₐₓ))`. Every sample hits the light, which is much better than sampling its whole surface (half of which faces away). `1 − cos θₘₐₓ` is computed as `x/(1+√(1−x))` with `x = r²/d²` to avoid cancellation for small or distant lights. A shading point *inside* a (two-sided) sphere light falls back to uniform area sampling of the sphere.
- **Triangle light:** sample a uniform point (`p = 1/A`) and convert to solid angle: `p_ω = d² / (|cos θ_light| · A)`.

A shadow ray then checks visibility. To avoid counting the same light twice, emission is **not** added when a BSDF-sampled ray from a diffuse vertex hits a light that light sampling could have chosen; it *is* added after camera rays, after specular bounces, and for emitters that are not in the light list (emissive planes, the sky). Each light path is thus counted by exactly one strategy, keeping the estimator unbiased.

This is not multiple importance sampling (MIS). MIS would weight both strategies everywhere and is better for glossy surfaces and large lights; it is the most valuable missing feature.

## Russian roulette

After `rr_min_depth` bounces a path survives with probability `q = clamp(max(β), 0.05, 0.95)` and its throughput is divided by `q`. The expected contribution is unchanged (`q · β/q = β`), so the estimator stays unbiased, but paths that can no longer contribute much are ended early. `max_depth` remains as a hard cap; it is the one source of (small) bias, because paths longer than it are truncated.

## Verifying unbiasedness

Three tests compare against closed-form answers:

1. **Sphere light over a plane.** A Lambertian plane (albedo `a`) lit only by a spherical emitter of radiance `Lₑ`, radius `r`, centred at height `d`: the irradiance directly below is `π Lₑ (r/d)²`, so the reflected radiance must be exactly `a · Lₑ · (r/d)² = 0.2`. Both with light sampling (within 1 %) and with pure BSDF sampling (within 3 %, much noisier).
2. **Uniform sky over a plane.** Radiance must equal `albedo × sky`.
3. **Ball inside a glowing shell.** A convex diffuse ball inside a two-sided emissive sphere of radiance `E` sees `E` in every direction, so its radiance is exactly `albedo × E` (exercises light sampling from inside a sphere light).

A third test renders the Cornell box with and without light sampling and requires the mean brightness to agree within 4 % — a direct check that the "don't double-count" rule is right. The furnace-style test first written here was only approximately solvable and was replaced by these exact ones.

## The direct-lighting integrator

`integrator: "direct"` is the Phase 2 renderer, kept as an option: at diffuse surfaces it sums one sample of every light (no indirect light — shadows are black), at mirrors and glass it recurses, splitting deterministically into Fresnel-weighted reflection and refraction. It is fast and useful for checking geometry and lights.

## BVH

A binary tree of axis-aligned boxes over all bounded primitives (spheres, triangles; planes are tested separately).

- **Build:** top-down, binned **surface area heuristic** (16 bins per axis). The SAH estimates the cost of a split as `C_trav + (A_L·N_L + A_R·N_R)/A_parent`, because the probability that a random ray hitting the parent also hits a child is proportional to the child's surface area. A node becomes a leaf when splitting does not pay off (≤16 primitives) or at ≤4 primitives. If all centroids coincide (no plane can separate them), it splits by count so leaves stay small. Depth is capped at 60 to fit the fixed traversal stack.
- **Layout:** nodes are a flat array in depth-first order; the left child is always the next node, so a node stores only the right child index, a primitive count and the split axis (56 bytes).
- **Traversal:** iterative with a 64-entry stack. The child on the near side of the split plane (chosen from the sign of the ray direction on the split axis) is visited first, and `t_max` shrinks as hits are found, so far boxes are culled. Shadow rays use any-hit traversal and stop at the first occluder.
- **Deferred shading data:** during traversal only `t` and the barycentrics are computed; normals, UVs and the face orientation are computed once for the final hit.

`test_bvh.cpp` builds random scenes of spheres and triangles and checks, for thousands of random rays (including axis-parallel ones), that BVH and brute force return the same hit within 1e-12 and that any-hit agrees with closest-hit.

## Tone mapping and output

The framebuffer holds linear radiance, unbounded above. To display it: multiply by `exposure`, apply a tone curve (`clamp`, `reinhard` `x/(1+x)`, or the ACES filmic fit), then the exact **sRGB** transfer function (linear segment below 0.0031308, power 1/2.4 above) — not a plain gamma 2.2 — and round to 8 bits. NaN or negative samples are replaced by black and counted in `invalid_samples`; every demo scene renders with zero.

## Known limitations

- **Caustics** (light → specular → diffuse → eye) can only be found by BSDF sampling from the diffuse surface, so they converge slowly and show as bright speckles at moderate sample counts (visible under the glass sphere in the convergence strip). Bidirectional methods or photon mapping solve this.
- **No MIS** (see above); small lights seen in glossy reflections are noisy.
- **Uniform light selection**: a dim light gets as many samples as a bright one.
- A NaN sample is recorded as black, a tiny bias, but it is counted and has never occurred in the demo scenes.
