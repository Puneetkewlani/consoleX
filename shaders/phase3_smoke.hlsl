RWStructuredBuffer<uint> Output : register(u0);

[numthreads(1, 1, 1)]
void main(uint3 dispatchThreadID : SV_DispatchThreadID)
{
    Output[dispatchThreadID.x] = dispatchThreadID.x;
}
