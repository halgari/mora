// Minimal SimpleMath.h shim.
//
// CommonLibSSE-NG's RE/S/State.h and a handful of graphics-adjacent headers
// expect DirectX::SimpleMath types (Vector3, Vector4, Matrix) for struct
// layout. The Mora runtime never touches any of these values at runtime —
// the structs are only mapped for offset bookkeeping — so we just need the
// types to have the right sizes and layouts.
//
// This shim provides trivial POD types that match the on-the-wire sizes of
// the real DirectXTK SimpleMath types so the struct layouts in State.h stay
// correct. Any method that would be called on these types (only used in
// code paths we don't link) would fail to link, which is fine — we don't
// reference them.
#pragma once

namespace DirectX {
namespace SimpleMath {

struct Vector3 {
    float x;
    float y;
    float z;
    Vector3() = default;
    Vector3(float X, float Y, float Z) : x(X), y(Y), z(Z) {}
    explicit Vector3(const float* a) : x(a[0]), y(a[1]), z(a[2]) {}
};
static_assert(sizeof(Vector3) == 12, "SimpleMath::Vector3 shim size mismatch");

struct Vector4 {
    float x;
    float y;
    float z;
    float w;
    Vector4() = default;
    Vector4(float X, float Y, float Z, float W) : x(X), y(Y), z(Z), w(W) {}
    // DirectX::SimpleMath::Vector4 inherits XMFLOAT4 which has a float*
    // ctor; this matches for State.h's `(Vector4)m.m[0]` style casts.
    Vector4(const float* a) : x(a[0]), y(a[1]), z(a[2]), w(a[3]) {}
};
static_assert(sizeof(Vector4) == 16, "SimpleMath::Vector4 shim size mismatch");

// 4x4 row-major matrix. Field layout matches DirectX::XMFLOAT4X4, which
// SimpleMath::Matrix derives from.
struct Matrix {
    union {
        struct {
            float _11, _12, _13, _14;
            float _21, _22, _23, _24;
            float _31, _32, _33, _34;
            float _41, _42, _43, _44;
        };
        float m[4][4];
    };
};
static_assert(sizeof(Matrix) == 64, "SimpleMath::Matrix shim size mismatch");

} // namespace SimpleMath
} // namespace DirectX
