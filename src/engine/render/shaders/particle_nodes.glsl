// Node types of particle graphs (ParticleGraph in engine/material/particlegraph.h), read like
// material_nodes.glsl: every function named node_<id> is a node, its parameters the ports,
// "// @" lines above it the title, defaults and settings (see engine/material/nodelibrary.h).
//
// These functions are put into particles.comp, after the author's functions, so they can
// use random(), curlNoise(), the Particle and Step structs... Inputs can read the particle
// being simulated with expr(): `p` (Particle), `s` (Step) and `time` (seconds) are in scope.
//
// Force nodes take the acceleration so far (`acceleration`, units / s^2) and give it back with
// their own added (`result`), so forces chain: Scene Forces -> Vortex -> Particle Output.
// `center` and `axis` inputs can come from an Object node, so a force follows an entity.
// Reach: `radius` 0 = everywhere; otherwise the force fades out over the outer `falloff`
// part of the radius.
//
// Author: the force functions (the stubs give back the acceleration unchanged).

// @node Gravity (Forces)
// @param acceleration 0 0 0
// @param direction 0 -1 0
// @param strength 9.81
void node_gravity(vec3 acceleration, vec3 direction, float strength, out vec3 result)
{
    // TODO (Author): `strength` along `direction`.
    result = acceleration;
}

// @node Wind (Forces)
// @param acceleration 0 0 0
// @param velocity expr(p.velocity.xyz)
// @param direction 1 0 0
// @param strength 3
// @param drag 1 range(0, 20)
void node_wind(vec3 acceleration, vec3 velocity, vec3 direction, float strength, float drag, out vec3 result)
{
    // TODO (Author): drag toward a velocity of `strength` along `direction`.
    result = acceleration;
}

// @node Attractor (Forces)
// @param acceleration 0 0 0
// @param position expr(p.position.xyz)
// @param center 0 0 0
// @param strength 5
// @param radius 0
// @param falloff 0.5 range(0, 1)
void node_attractor(vec3 acceleration, vec3 position, vec3 center, float strength, float radius, float falloff,
                    out vec3 result)
{
    // TODO (Author): toward `center` (negative strength: away), within the reach.
    result = acceleration;
}

// @node Turbulence (Forces)
// @param acceleration 0 0 0
// @param position expr(p.position.xyz)
// @param time expr(time)
// @param strength 2
// @param frequency 1
// @param speed 0.5
void node_turbulence(vec3 acceleration, vec3 position, float time, float strength, float frequency, float speed,
                     out vec3 result)
{
    // TODO (Author): curl noise at `position` x `frequency`, drifting at `speed`.
    result = acceleration;
}

// @node Vortex (Forces)
// @param acceleration 0 0 0
// @param position expr(p.position.xyz)
// @param velocity expr(p.velocity.xyz)
// @param time expr(time)
// @param center 0 0 0
// @param axis 0 1 0
// @param strength 3
// @param pull 1
// @param lift 1
// @param noise 0
// @param frequency 1
// @param speed 0.5
// @param radius 0
// @param falloff 0.5 range(0, 1)
void node_vortex(vec3 acceleration, vec3 position, vec3 velocity, float time, vec3 center, vec3 axis, float strength,
                 float pull, float lift, float noise, float frequency, float speed, float radius, float falloff,
                 out vec3 result)
{
    // TODO (Author): spin around the line through `center` along `axis`, pull toward it,
    // lift along it, local swirls of `noise`; within the reach (from the axis).
    result = acceleration;
}

// @node Add (Math)
// @param a 0 0 0
// @param b 0 0 0
void node_add(vec3 a, vec3 b, out vec3 result)
{
    result = a + b;
}

// @node Scale (Math)
// @param value 0 0 0
// @param factor 1
void node_scale(vec3 value, float factor, out vec3 result)
{
    result = value * factor;
}
