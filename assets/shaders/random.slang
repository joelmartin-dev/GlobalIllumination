module random;

// Steps the RNG and returns a floating-point value between 0 and 1 inclusive.
// Each use of the function will produce different result through stepping the seed each call
public float rand(inout uint seed) {
    // Condensed version of pcg_output_rxs_m_xs_32_32, with simple conversion to floating-point [0,1].
    // Step seed
    seed = seed * 747796405u + 1;
    // Generate pseudorandom uint from seed
    uint word = ((seed >> ((seed >> 28u) + 4u)) ^ seed) * 277803737u;
    word = (word >> 22) ^ word;
    return float(word) / 4294967295.0f;
}

// Approximate scattering by randomly choosing a direction to bias a bounce towards (vector in sphere)
public float3 randomDirection(uint rngState) {
    float theta = rand(rngState);                         // Random in [0, 2pi] (random direction on XY plane)
    float u = 2.0 * rand(rngState) - 1.0;                 // Random in [-1, 1] (random direction -Z, Z)
    float r = sqrt(1.0 - u * u);                          // Get the radius of the sphere
    return float3(r * cospi(theta), r * sinpi(theta), u); // Vector from origin to surface of sphere
}