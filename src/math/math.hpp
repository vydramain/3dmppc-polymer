// Small linear-algebra kit for the software rasterizer.
//
// Conventions:
//   * Left-handed coordinate system: +x right, +y up, +z forward (into screen).
//   * Column-vector convention: a transformed point is v' = M * v.
//   * Mat4 is stored row-major as m[row][col].
#pragma once

#include <cmath>

namespace rv_3dmppc {

struct Vec2 {
    float x = 0.0f, y = 0.0f;
};

struct Vec3 {
    float x = 0.0f, y = 0.0f, z = 0.0f;

    Vec3 operator+(const Vec3& r) const { return {x + r.x, y + r.y, z + r.z}; }
    Vec3 operator-(const Vec3& r) const { return {x - r.x, y - r.y, z - r.z}; }
    Vec3 operator*(float s) const { return {x * s, y * s, z * s}; }
};

struct Vec4 {
    float x = 0.0f, y = 0.0f, z = 0.0f, w = 1.0f;
};

inline float dot(const Vec3& a, const Vec3& b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

inline Vec3 cross(const Vec3& a, const Vec3& b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}

inline float length(const Vec3& v) { return std::sqrt(dot(v, v)); }

inline Vec3 normalize(const Vec3& v) {
    const float len = length(v);
    return len > 0.0f ? v * (1.0f / len) : v;
}

// Row-major 4x4 matrix. Identity by default.
struct Mat4 {
    float m[4][4] = {{1, 0, 0, 0}, {0, 1, 0, 0}, {0, 0, 1, 0}, {0, 0, 0, 1}};

    // Matrix * matrix.
    Mat4 operator*(const Mat4& b) const {
        Mat4 r;
        for (int i = 0; i < 4; ++i) {
            for (int j = 0; j < 4; ++j) {
                float s = 0.0f;
                for (int k = 0; k < 4; ++k) s += m[i][k] * b.m[k][j];
                r.m[i][j] = s;
            }
        }
        return r;
    }

    // Matrix * column vector.
    Vec4 operator*(const Vec4& v) const {
        return {
            m[0][0] * v.x + m[0][1] * v.y + m[0][2] * v.z + m[0][3] * v.w,
            m[1][0] * v.x + m[1][1] * v.y + m[1][2] * v.z + m[1][3] * v.w,
            m[2][0] * v.x + m[2][1] * v.y + m[2][2] * v.z + m[2][3] * v.w,
            m[3][0] * v.x + m[3][1] * v.y + m[3][2] * v.z + m[3][3] * v.w,
        };
    }
};

inline Mat4 translation(const Vec3& t) {
    Mat4 r;
    r.m[0][3] = t.x;
    r.m[1][3] = t.y;
    r.m[2][3] = t.z;
    return r;
}

inline Mat4 rotationY(float radians) {
    const float c = std::cos(radians), s = std::sin(radians);
    Mat4 r;
    r.m[0][0] = c;  r.m[0][2] = s;
    r.m[2][0] = -s; r.m[2][2] = c;
    return r;
}

inline Mat4 rotationX(float radians) {
    const float c = std::cos(radians), s = std::sin(radians);
    Mat4 r;
    r.m[1][1] = c;  r.m[1][2] = -s;
    r.m[2][1] = s;  r.m[2][2] = c;
    return r;
}

inline Mat4 rotationZ(float radians) {
    const float c = std::cos(radians), s = std::sin(radians);
    Mat4 r;
    r.m[0][0] = c;  r.m[0][1] = -s;
    r.m[1][0] = s;  r.m[1][1] = c;
    return r;
}

inline Mat4 scaling(const Vec3& s) {
    Mat4 r;
    r.m[0][0] = s.x;
    r.m[1][1] = s.y;
    r.m[2][2] = s.z;
    return r;
}

constexpr float kPi = 3.14159265358979323846f;
inline float radians(float degrees) { return degrees * (kPi / 180.0f); }

inline float clampf(float v, float lo, float hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

inline float lerpf(float a, float b, float t) { return a + (b - a) * t; }

// Left-handed look-at view matrix.
inline Mat4 lookAtLH(const Vec3& eye, const Vec3& target, const Vec3& up) {
    const Vec3 z = normalize(target - eye);   // forward
    const Vec3 x = normalize(cross(up, z));   // right
    const Vec3 y = cross(z, x);               // true up
    Mat4 r;
    r.m[0][0] = x.x; r.m[0][1] = x.y; r.m[0][2] = x.z; r.m[0][3] = -dot(x, eye);
    r.m[1][0] = y.x; r.m[1][1] = y.y; r.m[1][2] = y.z; r.m[1][3] = -dot(y, eye);
    r.m[2][0] = z.x; r.m[2][1] = z.y; r.m[2][2] = z.z; r.m[2][3] = -dot(z, eye);
    r.m[3][0] = 0;   r.m[3][1] = 0;   r.m[3][2] = 0;   r.m[3][3] = 1;
    return r;
}

// Left-handed perspective projection. NDC z maps near->0, far->1.
inline Mat4 perspectiveLH(float fovYradians, float aspect, float zNear, float zFar) {
    const float ys = 1.0f / std::tan(fovYradians * 0.5f);
    const float xs = ys / aspect;
    const float zr = zFar / (zFar - zNear);
    Mat4 r{};
    for (auto& row : r.m)
        for (float& v : row) v = 0.0f;
    r.m[0][0] = xs;
    r.m[1][1] = ys;
    r.m[2][2] = zr;
    r.m[2][3] = -zNear * zr;
    r.m[3][2] = 1.0f;  // w' = z
    return r;
}

}  // namespace rv_3dmppc
