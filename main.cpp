#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <algorithm>
#include <iostream>

#define PI 3.14159
#define N_BOUNCES 8
#define EXPOSURE 0.5
#define MIN_RAY_DIST 0.001
#define MAX_RAY_DIST 10000.0
#define ENABLE_AA true
#define ENABLE_RUSSIAN_ROULETTE false

struct vec3 {
  float x, y, z;
  vec3() { x=0.0; y=0.0, z=0.0; }
  vec3(float _x) { x=_x; y=_x; z=_x; }
  vec3(float _x, float _y, float _z) { x=_x; y=_y; z=_z; }

  vec3 operator*(float b) const { return vec3(x*b,y*b,z*b); }
  vec3 operator/(float b) const { return vec3(x/b,y/b,z/b); }

  vec3 operator+(const vec3 &b) const { return vec3(x+b.x,y+b.y,z+b.z); }
  vec3 operator-(const vec3 &b) const { return vec3(x-b.x,y-b.y,z-b.z); }
  vec3 operator*(const vec3 &b) const { return vec3(x*b.x,y*b.y,z*b.z); }
  vec3 operator/(const vec3 &b) const { return vec3(x/b.x,y/b.y,z/b.z); }

  void operator*=(const float b) { x*=b; y*=b; z*=b; }
  void operator*=(const vec3 &b) { x*=b.x; y*=b.y; z*=b.z; }
  void operator+=(const vec3 &b) { x+=b.x; y+=b.y; z+=b.z; }
};

// glsl vec3 functions
float clamp(float x){ return x<0 ? 0 : x>1 ? 1 : x; }
float dot(const vec3 &a, const vec3 &b) { return a.x*b.x+a.y*b.y+a.z*b.z; }
float length(const vec3 &a) { return sqrt(a.x*a.x+a.y*a.y+a.z*a.z); }
vec3 clamp(const vec3 &x){ return vec3(clamp(x.x), clamp(x.y), clamp(x.z)); }
vec3 normalize(const vec3 &a){ return a/length(a); }
vec3 reflect(const vec3 &a, const vec3 &b) { return a - b * 2.0f * dot(b, a); }
vec3 mix(const vec3 &a, const vec3 &b, float c) { return a * (1.0-c) + b*c; }
vec3 pow(const vec3 &a, const vec3 &b) { return vec3(pow(a.x,b.x),pow(a.y,b.y),pow(a.z,b.z)); }
vec3 mix(const vec3 &a, const vec3 &b, const vec3 &c) {
  return vec3(
    a.x * (1.0-c.x) + b.x*c.x,
    a.y * (1.0-c.y) + b.y*c.y,
    a.z * (1.0-c.z) + b.z*c.z
  );
}

inline int toCol(float x){ return int(clamp(x)*255+0.5); }
inline float randFloat() { return float(rand()) / float(RAND_MAX); }

vec3 randUnitVector() {
  float z = randFloat() * 2.0 - 1.0;
  float a = randFloat() * 2.0 * PI;
  float r = sqrt(1.0f - z * z);
  float x = r * cos(a);
  float y = r * sin(a);
  return vec3(x, y, z);
} 

// SRGB
vec3 LessThan(vec3 f, float value) {
  return vec3(
    (f.x < value) ? 1.0f : 0.0f,
    (f.y < value) ? 1.0f : 0.0f,
    (f.z < value) ? 1.0f : 0.0f
  );
}

vec3 LinearToSRGB(vec3 rgb) {
  rgb = clamp(rgb);

  return mix(
    pow(rgb, vec3(1.0f / 2.4f)) * 1.055f - 0.055f,
    rgb * 12.92f,
    LessThan(rgb, 0.0031308f)
  );
}

vec3 SRGBToLinear(vec3 rgb) {
  rgb = clamp(rgb);

  return mix(
    pow(((rgb + 0.055f) / 1.055f), vec3(2.4f)),
    rgb / 12.92f,
    LessThan(rgb, 0.04045f)
  );
}

// ACES tone mapping curve fit to go from HDR to LDR
//https://knarkowicz.wordpress.com/2016/01/06/aces-filmic-tone-mapping-curve/
vec3 ACESFilm(vec3 x) {
  float a = 2.51f;
  float b = 0.03f;
  float c = 2.43f;
  float d = 0.59f;
  float e = 0.14f;
  return clamp((x*(x*a + b)) / (x*(x*c + d) + e));
}

struct MaterialInfo {
  vec3 diffuse;
  vec3 emissive;
  vec3 specular;
  float percentSpec;
  float roughness;

  MaterialInfo() {}
  MaterialInfo(vec3 _diffuse, vec3 _emissive, vec3 _specular, float _percentSpec, float _roughness) {
    diffuse=_diffuse;
    emissive=_emissive;
    specular=_specular;
    percentSpec=_percentSpec;
    roughness=_roughness;
  }
};

struct Sphere {
  vec3 pos;
  float radius;
  MaterialInfo mat;

  Sphere() {}
  Sphere(vec3 _pos, float _radius, MaterialInfo _mat) {
    pos=_pos;
    radius=_radius;
    mat=_mat;
  }
};

struct HitInfo {
  bool didHit;
  vec3 normal;
  vec3 hitPoint;
  MaterialInfo mat;

  HitInfo() {}
  HitInfo(bool _didHit, vec3 _normal, vec3 _hitPoint, MaterialInfo _mat) {
    didHit=_didHit;
    normal=_normal;
    hitPoint=_hitPoint;
    mat=_mat;
  }
};

HitInfo sphereIntersect(vec3 rayOrigin, vec3 rayDir, Sphere sphere) {
  vec3 oc = rayOrigin - sphere.pos;
  float a = dot(rayDir, rayDir);
  float half_b = dot(oc, rayDir);
  float c = dot(oc, oc) - sphere.radius*sphere.radius;
  float discriminant = half_b*half_b - a*c;

  bool didHit = discriminant > 0.0 ? true : false;

  // Find the nearest root in the acceptable range
  float sqrtd = sqrt(discriminant);
  float root = (-half_b - sqrtd) / a;
  if (root < MIN_RAY_DIST || root > MAX_RAY_DIST) {
    root = (-half_b + sqrtd) / a;
    if (root < MIN_RAY_DIST || root > MAX_RAY_DIST) {
      didHit = false;
    }
  }

  // Normal is the vector going from the center of the sphere to the hit point
  vec3 hitPoint = rayOrigin + rayDir*root;
  vec3 normal = (hitPoint - sphere.pos) / sphere.radius;

  return HitInfo(didHit, normal, hitPoint, sphere.mat);
}

vec3 scene(vec3 rayOrigin, vec3 rayDir) {
  MaterialInfo metalYellow = MaterialInfo(vec3(0.9, 0.9, 0.5), vec3(0.0), vec3(0.9), 0.1, 0.2);
  MaterialInfo metalMagenta = metalYellow;
  MaterialInfo metalCyan  = metalYellow;
  metalMagenta.diffuse = vec3(0.9, 0.5, 0.9);
  metalMagenta.percentSpec = 0.3;
  metalMagenta.roughness = 0.2;
  metalCyan.diffuse  = vec3(0.5, 0.9, 0.9);

  MaterialInfo matteWhite = MaterialInfo(vec3(0.9), vec3(0.0), vec3(0.0), 0.0, 0.0);
  MaterialInfo matteRed   = matteWhite;
  MaterialInfo matteGreen = matteWhite;
  matteRed.diffuse   = vec3(1.0, 0.2, 0.2);
  matteGreen.diffuse = vec3(0.2, 1.0, 0.2);

  MaterialInfo lightSource = MaterialInfo(vec3(0.0), vec3(1.0, 0.9, 0.7)*1.0, vec3(0.0), 0.0, 0.0);

  int nSpheres = 10;
  Sphere spheres[16];

  // Light Sources
  spheres[0] = Sphere(vec3(0, 18, 24), 10.0, lightSource);
  spheres[1] = Sphere(vec3(0, 16, 6), 10.0, lightSource);

  // Walls
  spheres[2] = Sphere(vec3(-108,    0, 30), 100.0, matteRed);
  spheres[3] = Sphere(vec3( 108,    0, 30), 100.0, matteGreen);
  spheres[4] = Sphere(vec3(   0,    0,136), 100.0, matteWhite);
  spheres[5] = Sphere(vec3(   0, -103, 30), 100.0, matteWhite);
  spheres[6] = Sphere(vec3(   0,  125, 30), 100.0, lightSource);

  // Subjects
  spheres[7] = Sphere(vec3(-6.0, -1.6, 24.0), 2.0, metalCyan);
  spheres[8] = Sphere(vec3( 0.0, -1.6, 20.0), 2.0, metalMagenta);
  spheres[9] = Sphere(vec3( 6.0, -1.6, 24.0), 2.0, metalYellow);

  vec3 col = vec3(0.0);
  vec3 throughput = vec3(1.0);

  // Test for ray intersection against all spheres in scene
  // Set the ray color to the closest hit object in the scene
  // Note, without IBL, non-enclosed spaces will often appear
  // dark as rays will quickly bounce out of the scene
  for (int nBounce=0; nBounce <= N_BOUNCES; nBounce++) {
    float closestHit = MAX_RAY_DIST;
    HitInfo hitInfo;

    for (int i=0; i<nSpheres; i++) {
      HitInfo h = sphereIntersect(rayOrigin, rayDir, spheres[i]);
      float rayDist = length(rayOrigin-h.hitPoint);
      if (h.didHit && rayDist < closestHit && rayDist > MIN_RAY_DIST) {
        closestHit = rayDist;
        hitInfo = h;
      }
    }

    // No objects hit
    // Return skybox color
    if (closestHit == MAX_RAY_DIST) {
      vec3 skyboxColor = vec3(0.5, 0.8, 0.9);
      col += skyboxColor * throughput;
      return col;
    }

    // Bounce ray
    rayOrigin = hitInfo.hitPoint;

    // Decide if ray will be diffuse or specular
    bool isSpecRay = (randFloat() < hitInfo.mat.percentSpec) ? true : false;
    vec3 diffuseRayDir = normalize(hitInfo.normal + randUnitVector());
    float specDirMix = hitInfo.mat.roughness * hitInfo.mat.roughness;
    vec3 specRayDir = normalize(mix(reflect(rayDir, hitInfo.normal), diffuseRayDir, specDirMix));
    rayDir = isSpecRay ? specRayDir: diffuseRayDir;

    // Add emissive lighting
    col += hitInfo.mat.emissive * throughput;

    // Propogate strength of light through bounces
    throughput *= isSpecRay ? hitInfo.mat.specular : hitInfo.mat.diffuse;

    // As the throughput gets smaller, the ray is more likely to get terminated early.
    // Survivors have their value boosted to make up for fewer samples being in the average.
    if (ENABLE_RUSSIAN_ROULETTE) {                
      float p = std::max(throughput.x, std::max(throughput.y, throughput.z));
      if (randFloat() > p)
        break;

      // Add the energy we 'lose' by randomly terminating paths
      throughput *= 1.0f / p;   
    }
  }

  return col;
}

int main() {
  int w = 300;
  int h = 120;
  int spp = 8; // Samples-per-pixel
  vec3 *sceneCol = new vec3[w*h];

  // Origin of the rays
  float fov = 90.0;
  float camDist = 1.0 / tan(fov * 0.5 * PI / 180.0);
  vec3 rayOrigin = vec3(0.0);

  // Get the direction of the ray from the origin to a pixel
  for (int y=0; y<h; y++) {
    fprintf(stderr, "\rRendering (%dx%d) %5.2f%%", w, h, double(100.0*y/float(h)));
    for (int x=0; x<w; x++) {
      vec3 pixelCol = vec3(0.0);

      for (int i=0; i<spp; i++) {
        float ux = (x+randFloat() - 0.5*w)/float(w);
        float uy = (y+randFloat() - 0.5*h)/float(w);
        vec3 rayDir = normalize(vec3(ux, uy, camDist));

        pixelCol += scene(rayOrigin, rayDir) / float(spp);
      }

      // Post-process color
      pixelCol *= EXPOSURE;
      pixelCol = ACESFilm(pixelCol);
      pixelCol = LinearToSRGB(pixelCol);

      // ppm stores pixels from top down
      sceneCol[(h-y-1)*w+x] = pixelCol;
    }
  }

  // Write scene to ppm
  FILE *f = fopen("image.ppm", "w");
  fprintf(f, "P3\n%d %d\n%d\n", w, h, 255);
  for (int i=0; i<w*h; i++) {
    fprintf(f, "%d %d %d\n", toCol(sceneCol[i].x), toCol(sceneCol[i].y), toCol(sceneCol[i].z));
  }
}
