// Flowing shells (mode 24): nested cube frames whose points do not stay put.
// Once per hop every point lets go of its shell, falls inward and settles in
// the same place on the next shell down; the innermost shell's points shoot
// out as a fountain and build the outer shell anew. Every shell carries the
// same lattice (the nested cubes' pinwheel of frame strips), so after a hop
// the picture is the one it started from and the loop closes.
//
// Push constants: shape = outer half-size, size ratio, frame width,
// roundness (cubeExponent reads it); points = points per shell, shells,
// pixel radius, colour mode (0 palette per shell, 1 outer to inner colour,
// 2 one colour); look = tilt, glow, brightness, shell turn rate;
// style.rgb = outer colour, w = 24; wave.rgb = inner colour, w = hops per
// flow cycle (negative runs the story backwards); motion = share of the hop
// at rest, spread of the departures, fall sag, scatter; loop = cycles,
// fountain height, cascade (0 all shells at once .. 1 outer first), order
// (0 random .. 1 top of the shell first). The palette sits in the polytope
// buffer's first five vec4.

int shellCount() {
    return max(int(pc.points.y + 0.5), 2);
}

float shellSize(float level) {
    return pc.shape.x * pow(pc.shape.y, level);
}

float shellSpan() {
    return (pc.shape.x * sqrt(3.0) / pow(3.0, 1.0 / cubeExponent()) + 0.5 * pc.loop.y * pc.shape.x) * 1.10;
}

// Every shell turns about its own axis, a whole number of times per cycle.
vec3 turnShell(vec3 direction, int shell) {
    float identity = float(shell) * 3.17 + 0.41;
    vec3 axis = vec3(hashScalar(identity + 1.3), hashScalar(identity + 7.1),
                     hashScalar(identity + 13.7)) * 2.0 - 1.0;
    axis = normalize(axis + vec3(0.001, 0.002, 0.003));
    float rate = (1.0 + floor(2.0 * hashScalar(identity + 23.9)))
               * (hashScalar(identity + 31.7) < 0.5 ? -1.0 : 1.0);
    return rotateAroundAxis(direction, axis, timePhase(rate * pc.look.w));
}

vec3 shellPalette(int shell) {
    return polytopeData[shell % 5].rgb;
}
