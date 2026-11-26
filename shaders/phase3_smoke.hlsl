RWStructuredBuffer<uint> Output : register(u0);

[numthreads(1, 1, 1)]
void main(uint3 dispatchThreadID : SV_DispatchThreadID)
{
    Output[dispatchThreadID.x] = dispatchThreadID.x;
}

float4 vertexMain(uint vertexID : SV_VertexID) : SV_Position
{
    const float x = vertexID == 0 ? -1.0f : 1.0f;
    return float4(x, 0.0f, 0.0f, 1.0f);
}

float4 pixelMain() : SV_Target0
{
    return float4(1.0f, 0.0f, 1.0f, 1.0f);
}
