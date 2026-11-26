RaytracingAccelerationStructure Scene : register(t0);
RWTexture2D<float4> Output : register(u0);

struct Payload
{
    float4 color;
};

[shader("raygeneration")]
void RayGen()
{
    const uint2 pixel = DispatchRaysIndex().xy;
    RayDesc ray;
    ray.Origin = pixel.x == 0 ? float3(0.0, 0.0, -1.0)
                              : float3(3.0, 0.0, -1.0);
    ray.Direction = float3(0.0, 0.0, 1.0);
    ray.TMin = 0.0;
    ray.TMax = 10.0;

    Payload payload;
    payload.color = float4(0.0, 0.0, 0.0, 1.0);
    TraceRay(Scene, RAY_FLAG_NONE, 0xFF, 0, 0, 0, ray, payload);
    Output[pixel] = payload.color;
}

[shader("miss")]
void Miss(inout Payload payload)
{
    payload.color = float4(0.0, 0.0, 1.0, 1.0);
}

[shader("closesthit")]
void ClosestHit(
    inout Payload payload,
    BuiltInTriangleIntersectionAttributes attributes)
{
    payload.color = float4(0.0, 1.0, 0.0, 1.0);
}
