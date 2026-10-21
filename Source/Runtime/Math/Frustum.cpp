#include "EnginePCH.h"
#include "Frustum.h"

static FPlane NormalizePlane(const FPlane Plane)
{
    const FVector& N = Plane.Normal;
    const float Length = sqrtf(N.X * N.X + N.Y * N.Y + N.Z * N.Z);
    assert(Length > 1.0e-6f);

    const float InvLength = 1.0f / Length;
    return {
        { N.X * InvLength, N.Y * InvLength, N.Z * InvLength },
        Plane.Distance * InvLength
    };
}

FFrustumPlanes ExtractFrustumPlanes(const FMatrix& Matrix)
{
    auto MakePlane = [&Matrix](const int ColumnA, const float ScaleA, const int ColumnB, const float ScaleB)
        {
            return NormalizePlane({
                { ScaleA * Matrix.M[0][ColumnA] + ScaleB * Matrix.M[0][ColumnB], ScaleA * Matrix.M[1][ColumnA] + ScaleB * Matrix.M[1][ColumnB], ScaleA * Matrix.M[2][ColumnA] + ScaleB * Matrix.M[2][ColumnB] },
                ScaleA * Matrix.M[3][ColumnA] + ScaleB * Matrix.M[3][ColumnB] });
        };
    FFrustumPlanes Result{};
    Result.Planes[0] = MakePlane(3, 1.0f, 0, 1.0f);
    Result.Planes[1] = MakePlane(3, 1.0f, 0, -1.0f);
    Result.Planes[2] = MakePlane(3, 1.0f, 1, 1.0f);
    Result.Planes[3] = MakePlane(3, 1.0f, 1, -1.0f);
    Result.Planes[4] = NormalizePlane({ { Matrix.M[0][2], Matrix.M[1][2], Matrix.M[2][2] }, Matrix.M[3][2] });
    Result.Planes[5] = MakePlane(3, 1.0f, 2, -1.0f);
    return Result;
}

bool IsAABBInFrustum(const FAABB& Bounds, const FFrustumPlanes& Frustum)
{
    FVectorRegister Boundreg = VectorSIMD::LoadFloat3(&Bounds.Extent.X);
    FVectorRegister Centerreg = VectorSIMD::LoadFloat3(&Bounds.Center.X);
    for (const FPlane& Plane : Frustum.Planes)
    {
        FVectorRegister Planereg = VectorSIMD::LoadFloat3(&Plane.Normal.X);
        FVectorRegister absPlanereg = VectorSIMD::Abs(Planereg);

        const float Radius = VectorSIMD::Dot(absPlanereg, Boundreg);
        const float CenterDist = VectorSIMD::Dot(Planereg, Centerreg);

        if (CenterDist + Plane.Distance + Radius < 0.0f)
        {
            return false;
        }
    }
    return true;
}


bool IsAABBInSphere(const FAABB& Bounds, const FVector& Center, float Radius)
{
    const double DX = std::max(std::abs(static_cast<double>(Center.X) - Bounds.Center.X) - Bounds.Extent.X, 0.0);
    const double DY = std::max(std::abs(static_cast<double>(Center.Y) - Bounds.Center.Y) - Bounds.Extent.Y, 0.0);
    const double DZ = std::max(std::abs(static_cast<double>(Center.Z) - Bounds.Center.Z) - Bounds.Extent.Z, 0.0);

    return DX * DX + DY * DY + DZ * DZ <= static_cast<double>(Radius) * Radius;
}

// ExtractFrustumPlanes()가 평면을 정규화하므로 Radius를 월드 거리로 비교한다.
bool IsSphereInFrustum(const FVector& Center, float Radius, const FFrustumPlanes& Frustum)
{
    for (const FPlane& Plane : Frustum.Planes)
    {
        const float Distance = FVector::Dot(Plane.Normal, Center) + Plane.Distance;

        // 접하는 구는 유지한다.
        if (Distance < -Radius)
            return false;
    }
    return true;
}

FAABB MakeWorldBounds(const FBox& Value)
{
    const FVector Center = (Value.Min + Value.Max) * 0.5f;
    const FVector Extent = (Value.Max - Value.Min) * 0.5f;
    return { Center, Extent };
}
