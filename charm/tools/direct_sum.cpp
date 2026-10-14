// Exact O(N^2) forces for the particles in a barnes -dumpstep output.
//
//   direct_sum -G=<G> -eps=<eps> OUT.bin DUMP_pe0.bin [DUMP_pe1.bin ...]
//
// Reads every record (id, position, mass, ...) from the dump files, and
// writes OUT.bin in the same 72-byte record format with ax, ay, az, phi
// replaced by the direct sums -- same kernel, same softening, self term
// excluded -- so scripts/compare_forces.py can compare the tree against it:
//
//   a_i   =  G sum_{j != i} m_j (r_j - r_i) / (|r_j - r_i|^2 + eps^2)^(3/2)
//   phi_i = -G sum_{j != i} m_j / sqrt(|r_j - r_i|^2 + eps^2)
//
// Shares no code with the tree traversal on purpose. Cost is N^2: about a
// minute for 200k bodies on 16 cores; keep N small.
//
//   g++ -O3 -march=native -fopenmp -o direct_sum direct_sum.cpp
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

struct Record {
  int64_t id;
  double x, y, z, mass, ax, ay, az, phi;
};
static_assert(sizeof(Record) == 72, "record layout must match DataManager::dumpForces");

int main(int argc, char **argv) {
  double G = 1.0, eps = 0.05;
  std::vector<const char *> files;
  for (int i = 1; i < argc; i++) {
    if (!strncmp(argv[i], "-G=", 3)) G = atof(argv[i] + 3);
    else if (!strncmp(argv[i], "-eps=", 5)) eps = atof(argv[i] + 5);
    else files.push_back(argv[i]);
  }
  if (files.size() < 2) {
    fprintf(stderr, "usage: %s -G=<G> -eps=<eps> OUT.bin DUMP.bin [DUMP.bin ...]\n", argv[0]);
    return 1;
  }
  std::vector<Record> p;
  for (size_t k = 1; k < files.size(); k++) {
    FILE *f = fopen(files[k], "rb");
    if (!f) { perror(files[k]); return 1; }
    Record r;
    while (fread(&r, sizeof r, 1, f) == 1) p.push_back(r);
    fclose(f);
  }
  const long n = (long)p.size();
  const double eps2 = eps * eps;
  fprintf(stderr, "direct_sum: %ld particles, G=%g eps=%g\n", n, G, eps);

  std::vector<double> x(n), y(n), z(n), m(n);
  for (long i = 0; i < n; i++) { x[i] = p[i].x; y[i] = p[i].y; z[i] = p[i].z; m[i] = p[i].mass; }

#pragma omp parallel for schedule(dynamic, 64)
  for (long i = 0; i < n; i++) {
    double ax = 0, ay = 0, az = 0, phi = 0;
    for (long j = 0; j < n; j++) {
      if (j == i) continue;
      const double dx = x[j] - x[i], dy = y[j] - y[i], dz = z[j] - z[i];
      const double r2 = dx * dx + dy * dy + dz * dz + eps2;
      const double rinv = 1.0 / sqrt(r2);
      const double mr = m[j] * rinv;
      phi -= mr;
      const double mr3 = mr * rinv * rinv;
      ax += mr3 * dx; ay += mr3 * dy; az += mr3 * dz;
    }
    p[i].ax = G * ax; p[i].ay = G * ay; p[i].az = G * az; p[i].phi = G * phi;
  }

  FILE *out = fopen(files[0], "wb");
  if (!out) { perror(files[0]); return 1; }
  if (fwrite(p.data(), sizeof(Record), n, out) != (size_t)n) { perror("write"); return 1; }
  fclose(out);
  return 0;
}
