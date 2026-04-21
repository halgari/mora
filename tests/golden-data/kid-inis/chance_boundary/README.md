Three-rule Chance-boundary: 0 (skip), 100 (always), 50 (deterministic
RNG). This scenario is the most sensitive to any drift in the Xoshiro +
MSVC uniform_real_distribution port; a single wrong bit in the RNG
stack flips an unpredictable subset of the 50% rule's matches.
