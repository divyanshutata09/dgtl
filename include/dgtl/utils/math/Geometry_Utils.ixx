/*
 * Copyright (c) 2026 TataG Systems. All rights reserved.
 *
 * Project:     DGTL (Deep General-purpose Transformation Layer)
 * Owner:       Divyanshu Tata
 * Repository:  github.com/TataG-Systems/DGTL
 *
 * Component:   2D, 3D & 4D Geometry, Quaternions, Transforms & Spatial Intersections
 * Standards:   C++26 / Clang 18+ / MSVC 19.40+ / GCC 14+
 *
 * This software is the proprietary property of TataG Systems.
 * Unauthorized copying, modification, or distribution is strictly prohibited.
 */

module;

#include <cmath>
#include <cstddef>
#include <array>
#include <algorithm>
#include <limits>

export module dgtl.math.geometry;

export namespace dgtl::math::geometry {

// ============================================================================
// 1. VECTOR STRUCTURES (2D, 3D, 4D)
// ============================================================================

struct Vec2 {
    double x = 0.0, y = 0.0;

    constexpr Vec2() noexcept = default;
    constexpr Vec2(double x_, double y_) noexcept : x(x_), y(y_) {}

    constexpr Vec2 operator+(const Vec2& o) const noexcept { return {x + o.x, y + o.y}; }
    constexpr Vec2 operator-(const Vec2& o) const noexcept { return {x - o.x, y - o.y}; }
    constexpr Vec2 operator*(double s) const noexcept { return {x * s, y * s}; }
    constexpr Vec2 operator/(double s) const noexcept { return {x / s, y / s}; }

    [[nodiscard]] constexpr double dot(const Vec2& o) const noexcept { return x * o.x + y * o.y; }
    [[nodiscard]] double length() const noexcept { return std::hypot(x, y); }
    [[nodiscard]] Vec2 normalized() const noexcept {
        const double l = length();
        return (l > 1e-15) ? (*this / l) : Vec2{};
    }
};

struct Vec3 {
    double x = 0.0, y = 0.0, z = 0.0;

    constexpr Vec3() noexcept = default;
    constexpr Vec3(double x_, double y_, double z_) noexcept : x(x_), y(y_), z(z_) {}

    constexpr Vec3 operator+(const Vec3& o) const noexcept { return {x + o.x, y + o.y, z + o.z}; }
    constexpr Vec3 operator-(const Vec3& o) const noexcept { return {x - o.x, y - o.y, z - o.z}; }
    constexpr Vec3 operator*(double s) const noexcept { return {x * s, y * s, z * s}; }
    constexpr Vec3 operator/(double s) const noexcept { return {x / s, y / s, z / s}; }

    [[nodiscard]] constexpr double dot(const Vec3& o) const noexcept { return x * o.x + y * o.y + z * o.z; }
    [[nodiscard]] constexpr Vec3 cross(const Vec3& o) const noexcept {
        return {
            y * o.z - z * o.y,
            z * o.x - x * o.z,
            x * o.y - y * o.x
        };
    }
    [[nodiscard]] double length() const noexcept { return std::sqrt(x * x + y * y + z * z); }
    [[nodiscard]] Vec3 normalized() const noexcept {
        const double l = length();
        return (l > 1e-15) ? (*this / l) : Vec3{};
    }
};

struct Vec4 {
    double x = 0.0, y = 0.0, z = 0.0, w = 0.0;

    constexpr Vec4() noexcept = default;
    constexpr Vec4(double x_, double y_, double z_, double w_) noexcept : x(x_), y(y_), z(z_), w(w_) {}

    constexpr Vec4 operator+(const Vec4& o) const noexcept { return {x + o.x, y + o.y, z + o.z, w + o.w}; }
    constexpr Vec4 operator-(const Vec4& o) const noexcept { return {x - o.x, y - o.y, z - o.z, w - o.w}; }
    constexpr Vec4 operator*(double s) const noexcept { return {x * s, y * s, z * s, w * s}; }
    constexpr Vec4 operator/(double s) const noexcept { return {x / s, y / s, z / s, w / s}; }

    [[nodiscard]] constexpr double dot(const Vec4& o) const noexcept { return x * o.x + y * o.y + z * o.z + w * o.w; }
    [[nodiscard]] double length() const noexcept { return std::sqrt(x * x + y * y + z * z + w * w); }
    [[nodiscard]] Vec4 normalized() const noexcept {
        const double l = length();
        return (l > 1e-15) ? (*this / l) : Vec4{};
    }
};

// ============================================================================
// 2. MATRICES (3x3, 4x4) & AFFINE TRANSFORMS
// ============================================================================

struct Mat3x3 {
    std::array<double, 9> m{1, 0, 0,  0, 1, 0,  0, 0, 1}; // Row-major

    [[nodiscard]] static constexpr Mat3x3 identity() noexcept {
        return Mat3x3{1, 0, 0,  0, 1, 0,  0, 0, 1};
    }

    [[nodiscard]] constexpr double det() const noexcept {
        return m[0] * (m[4] * m[8] - m[5] * m[7]) -
               m[1] * (m[3] * m[8] - m[5] * m[6]) +
               m[2] * (m[3] * m[7] - m[4] * m[6]);
    }

    [[nodiscard]] Mat3x3 inverse() const noexcept {
        const double d = det();
        if (std::fabs(d) < 1e-15) return identity();
        const double inv_d = 1.0 / d;
        Mat3x3 res;
        res.m[0] =  (m[4] * m[8] - m[5] * m[7]) * inv_d;
        res.m[1] = -(m[1] * m[8] - m[2] * m[7]) * inv_d;
        res.m[2] =  (m[1] * m[5] - m[2] * m[4]) * inv_d;
        res.m[3] = -(m[3] * m[8] - m[5] * m[6]) * inv_d;
        res.m[4] =  (m[0] * m[8] - m[2] * m[6]) * inv_d;
        res.m[5] = -(m[0] * m[5] - m[2] * m[3]) * inv_d;
        res.m[6] =  (m[3] * m[7] - m[4] * m[6]) * inv_d;
        res.m[7] = -(m[0] * m[7] - m[1] * m[6]) * inv_d;
        res.m[8] =  (m[0] * m[4] - m[1] * m[3]) * inv_d;
        return res;
    }

    [[nodiscard]] Vec3 operator*(const Vec3& v) const noexcept {
        return {
            m[0] * v.x + m[1] * v.y + m[2] * v.z,
            m[3] * v.x + m[4] * v.y + m[5] * v.z,
            m[6] * v.x + m[7] * v.y + m[8] * v.z
        };
    }
};

struct Mat4x4 {
    std::array<double, 16> m{
        1, 0, 0, 0,
        0, 1, 0, 0,
        0, 0, 1, 0,
        0, 0, 0, 1
    };

    [[nodiscard]] static constexpr Mat4x4 identity() noexcept {
        return Mat4x4{};
    }

    [[nodiscard]] static Mat4x4 translation(double tx, double ty, double tz) noexcept {
        Mat4x4 r = identity();
        r.m[3] = tx; r.m[7] = ty; r.m[11] = tz;
        return r;
    }

    [[nodiscard]] static Mat4x4 scaling(double sx, double sy, double sz) noexcept {
        Mat4x4 r = identity();
        r.m[0] = sx; r.m[5] = sy; r.m[10] = sz;
        return r;
    }

    [[nodiscard]] Mat4x4 operator*(const Mat4x4& o) const noexcept {
        Mat4x4 res;
        for (std::size_t r = 0; r < 4; ++r) {
            for (std::size_t c = 0; c < 4; ++c) {
                double s = 0.0;
                for (std::size_t k = 0; k < 4; ++k) {
                    s += m[r * 4 + k] * o.m[k * 4 + c];
                }
                res.m[r * 4 + c] = s;
            }
        }
        return res;
    }

    [[nodiscard]] Vec4 operator*(const Vec4& v) const noexcept {
        return {
            m[0] * v.x + m[1] * v.y + m[2] * v.z + m[3] * v.w,
            m[4] * v.x + m[5] * v.y + m[6] * v.z + m[7] * v.w,
            m[8] * v.x + m[9] * v.y + m[10] * v.z + m[11] * v.w,
            m[12] * v.x + m[13] * v.y + m[14] * v.z + m[15] * v.w
        };
    }

    [[nodiscard]] static Mat4x4 look_at(const Vec3& eye, const Vec3& target, const Vec3& up) noexcept {
        const Vec3 f = (target - eye).normalized();
        const Vec3 s = f.cross(up).normalized();
        const Vec3 u = s.cross(f);

        Mat4x4 r;
        r.m[0] = s.x;  r.m[1] = s.y;  r.m[2] = s.z;  r.m[3] = -s.dot(eye);
        r.m[4] = u.x;  r.m[5] = u.y;  r.m[6] = u.z;  r.m[7] = -u.dot(eye);
        r.m[8] = -f.x; r.m[9] = -f.y; r.m[10] = -f.z; r.m[11] = f.dot(eye);
        r.m[12] = 0;   r.m[13] = 0;   r.m[14] = 0;    r.m[15] = 1.0;
        return r;
    }

    [[nodiscard]] static Mat4x4 perspective(double fov_rad, double aspect, double near_z, double far_z) noexcept {
        const double tan_half = std::tan(0.5 * fov_rad);
        Mat4x4 r{};
        r.m[0] = 1.0 / (aspect * tan_half);
        r.m[5] = 1.0 / tan_half;
        r.m[10] = -(far_z + near_z) / (far_z - near_z);
        r.m[11] = -(2.0 * far_z * near_z) / (far_z - near_z);
        r.m[14] = -1.0;
        return r;
    }
};

// ============================================================================
// 3. QUATERNIONS & 3D/4D ROTATIONS
// ============================================================================

struct Quat {
    double w = 1.0, x = 0.0, y = 0.0, z = 0.0;

    constexpr Quat() noexcept = default;
    constexpr Quat(double w_, double x_, double y_, double z_) noexcept : w(w_), x(x_), y(y_), z(z_) {}

    [[nodiscard]] static Quat from_axis_angle(const Vec3& axis, double angle_rad) noexcept {
        const double half = 0.5 * angle_rad;
        const double s = std::sin(half);
        const Vec3 a = axis.normalized();
        return Quat(std::cos(half), a.x * s, a.y * s, a.z * s);
    }

    [[nodiscard]] constexpr Quat conjugate() const noexcept {
        return Quat(w, -x, -y, -z);
    }

    [[nodiscard]] double norm() const noexcept {
        return std::sqrt(w * w + x * x + y * y + z * z);
    }

    [[nodiscard]] Quat normalized() const noexcept {
        const double n = norm();
        return (n > 1e-15) ? Quat(w / n, x / n, y / n, z / n) : Quat{};
    }

    [[nodiscard]] Quat operator*(const Quat& q) const noexcept {
        return Quat(
            w * q.w - x * q.x - y * q.y - z * q.z,
            w * q.x + x * q.w + y * q.z - z * q.y,
            w * q.y - x * q.z + y * q.w + z * q.x,
            w * q.z + x * q.y - y * q.x + z * q.w
        );
    }

    [[nodiscard]] Vec3 rotate(const Vec3& v) const noexcept {
        const Quat p(0.0, v.x, v.y, v.z);
        const Quat r = (*this) * p * conjugate();
        return Vec3(r.x, r.y, r.z);
    }

    /**
     * @brief Spherical Linear Interpolation (SLERP) between two unit quaternions.
     */
    [[nodiscard]] static Quat slerp(Quat q1, Quat q2, double t) noexcept {
        double dot = q1.w * q2.w + q1.x * q2.x + q1.y * q2.y + q1.z * q2.z;
        if (dot < 0.0) {
            dot = -dot;
            q2 = Quat(-q2.w, -q2.x, -q2.y, -q2.z);
        }

        if (dot > 0.9995) {
            // Linear interpolation for tiny angles
            return Quat(
                q1.w + t * (q2.w - q1.w),
                q1.x + t * (q2.x - q1.x),
                q1.y + t * (q2.y - q1.y),
                q1.z + t * (q2.z - q1.z)
            ).normalized();
        }

        const double theta_0 = std::acos(dot);
        const double theta = theta_0 * t;
        const double sin_theta = std::sin(theta);
        const double sin_theta_0 = std::sin(theta_0);

        const double s1 = std::cos(theta) - dot * sin_theta / sin_theta_0;
        const double s2 = sin_theta / sin_theta_0;

        return Quat(
            s1 * q1.w + s2 * q2.w,
            s1 * q1.x + s2 * q2.x,
            s1 * q1.y + s2 * q2.y,
            s1 * q1.z + s2 * q2.z
        );
    }
};

// ============================================================================
// 4. RAY TRACING & SPATIAL INTERSECTIONS
// ============================================================================

struct Ray {
    Vec3 origin;
    Vec3 direction; // Normalized
};

struct AABB {
    Vec3 min_pt;
    Vec3 max_pt;
};

/**
 * @brief Kay-Kajiya slab test for Ray-AABB intersection.
 */
[[nodiscard]] inline bool intersect_ray_aabb(const Ray& ray, const AABB& box, double& t_min, double& t_max) noexcept {
    t_min = -std::numeric_limits<double>::infinity();
    t_max =  std::numeric_limits<double>::infinity();

    const auto check_axis = [&](double o, double d, double min_val, double max_val) {
        if (std::fabs(d) < 1e-15) {
            return (o >= min_val && o <= max_val);
        }
        double t1 = (min_val - o) / d;
        double t2 = (max_val - o) / d;
        if (t1 > t2) std::swap(t1, t2);
        t_min = std::max(t_min, t1);
        t_max = std::min(t_max, t2);
        return t_min <= t_max;
    };

    if (!check_axis(ray.origin.x, ray.direction.x, box.min_pt.x, box.max_pt.x)) return false;
    if (!check_axis(ray.origin.y, ray.direction.y, box.min_pt.y, box.max_pt.y)) return false;
    if (!check_axis(ray.origin.z, ray.direction.z, box.min_pt.z, box.max_pt.z)) return false;

    return t_max >= std::max(0.0, t_min);
}

/**
 * @brief Möller-Trumbore Ray-Triangle Intersection.
 * Computes intersection distance t and barycentric coordinates (u, v).
 */
[[nodiscard]] inline bool intersect_ray_triangle(
    const Ray& ray, const Vec3& v0, const Vec3& v1, const Vec3& v2,
    double& out_t, double& out_u, double& out_v) noexcept {

    const Vec3 edge1 = v1 - v0;
    const Vec3 edge2 = v2 - v0;
    const Vec3 pvec = ray.direction.cross(edge2);
    const double det = edge1.dot(pvec);

    if (std::fabs(det) < 1e-15) return false;
    const double inv_det = 1.0 / det;

    const Vec3 tvec = ray.origin - v0;
    out_u = tvec.dot(pvec) * inv_det;
    if (out_u < 0.0 || out_u > 1.0) return false;

    const Vec3 qvec = tvec.cross(edge1);
    out_v = ray.direction.dot(qvec) * inv_det;
    if (out_v < 0.0 || out_u + out_v > 1.0) return false;

    out_t = edge2.dot(qvec) * inv_det;
    return out_t >= 0.0;
}

// ============================================================================
// 6. 4D GEOMETRY, HYPERPLANES, HYPERSPHERES & 4D ROTATIONS
// ============================================================================

struct Hyperplane4D {
    Vec4 normal{};
    double d = 0.0;

    [[nodiscard]] constexpr double signed_distance(const Vec4& p) const noexcept {
        return normal.dot(p) + d;
    }
};

struct Hypersphere4D {
    Vec4 center{};
    double radius = 1.0;

    [[nodiscard]] double hypervolume() const noexcept {
        constexpr double half_pi_sq = 4.9348022005446793094;
        return half_pi_sq * radius * radius * radius * radius;
    }

    [[nodiscard]] double hypersurface_area() const noexcept {
        constexpr double two_pi_sq = 19.739208802178717238;
        return two_pi_sq * radius * radius * radius;
    }

    [[nodiscard]] bool contains(const Vec4& p) const noexcept {
        return (p - center).length() <= radius;
    }
};

struct AABB4D {
    Vec4 min_pt;
    Vec4 max_pt;

    [[nodiscard]] double hypervolume() const noexcept {
        const double dx = max_pt.x - min_pt.x;
        const double dy = max_pt.y - min_pt.y;
        const double dz = max_pt.z - min_pt.z;
        const double dw = max_pt.w - min_pt.w;
        return (dx > 0 && dy > 0 && dz > 0 && dw > 0) ? (dx * dy * dz * dw) : 0.0;
    }

    [[nodiscard]] bool contains(const Vec4& p) const noexcept {
        return p.x >= min_pt.x && p.x <= max_pt.x &&
               p.y >= min_pt.y && p.y <= max_pt.y &&
               p.z >= min_pt.z && p.z <= max_pt.z &&
               p.w >= min_pt.w && p.w <= max_pt.w;
    }

    [[nodiscard]] bool intersects(const AABB4D& o) const noexcept {
        return max_pt.x >= o.min_pt.x && min_pt.x <= o.max_pt.x &&
               max_pt.y >= o.min_pt.y && min_pt.y <= o.max_pt.y &&
               max_pt.z >= o.min_pt.z && min_pt.z <= o.max_pt.z &&
               max_pt.w >= o.min_pt.w && min_pt.w <= o.max_pt.w;
    }
};

[[nodiscard]] inline Mat4x4 rotation_4d_plane(std::size_t plane, double angle) noexcept {
    Mat4x4 r = Mat4x4::identity();
    const double c = std::cos(angle);
    const double s = std::sin(angle);
    switch (plane) {
        case 0: // XY
            r.m[0] = c;  r.m[1] = -s;
            r.m[4] = s;  r.m[5] = c;
            break;
        case 1: // XZ
            r.m[0] = c;  r.m[2] = -s;
            r.m[8] = s;  r.m[10] = c;
            break;
        case 2: // XW
            r.m[0] = c;  r.m[3] = -s;
            r.m[12] = s; r.m[15] = c;
            break;
        case 3: // YZ
            r.m[5] = c;  r.m[6] = -s;
            r.m[9] = s;  r.m[10] = c;
            break;
        case 4: // YW
            r.m[5] = c;  r.m[7] = -s;
            r.m[13] = s; r.m[15] = c;
            break;
        case 5: // ZW
            r.m[10] = c; r.m[11] = -s;
            r.m[14] = s; r.m[15] = c;
            break;
    }
    return r;
}

[[nodiscard]] inline Mat4x4 rotation_4d_isoclinic(double theta_xy, double theta_zw) noexcept {
    const Mat4x4 r1 = rotation_4d_plane(0, theta_xy);
    const Mat4x4 r2 = rotation_4d_plane(5, theta_zw);
    return r1 * r2;
}

// ============================================================================
// 7. 3D VOLUMETRIC PRIMITIVES: SPHERE, PLANE & FRUSTUM
// ============================================================================

struct Sphere3D {
    Vec3 center{};
    double radius = 1.0;

    [[nodiscard]] double volume() const noexcept {
        constexpr double four_thirds_pi = 4.1887902047863909846;
        return four_thirds_pi * radius * radius * radius;
    }

    [[nodiscard]] bool contains(const Vec3& p) const noexcept {
        return (p - center).length() <= radius;
    }

    [[nodiscard]] bool intersects_aabb(const AABB& box) const noexcept {
        double d_min = 0.0;
        const auto check = [&](double c, double min_v, double max_v) {
            if (c < min_v) {
                const double d = min_v - c;
                d_min += d * d;
            } else if (c > max_v) {
                const double d = c - max_v;
                d_min += d * d;
            }
        };
        check(center.x, box.min_pt.x, box.max_pt.x);
        check(center.y, box.min_pt.y, box.max_pt.y);
        check(center.z, box.min_pt.z, box.max_pt.z);
        return d_min <= radius * radius;
    }
};

struct Plane3D {
    Vec3 normal{0.0, 1.0, 0.0};
    double d = 0.0;

    [[nodiscard]] constexpr double distance_to(const Vec3& p) const noexcept {
        return normal.dot(p) + d;
    }
};

struct Frustum3D {
    std::array<Plane3D, 6> planes;

    [[nodiscard]] bool contains_point(const Vec3& p) const noexcept {
        for (const auto& plane : planes) {
            if (plane.distance_to(p) < 0.0) return false;
        }
        return true;
    }

    [[nodiscard]] bool intersects_sphere(const Sphere3D& s) const noexcept {
        for (const auto& plane : planes) {
            if (plane.distance_to(s.center) < -s.radius) return false;
        }
        return true;
    }
};

} // namespace dgtl::math::geometry
