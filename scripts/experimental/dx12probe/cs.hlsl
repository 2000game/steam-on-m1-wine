RWStructuredBuffer<uint> o : register(u0);
[numthreads(64,1,1)] void cs(uint i : SV_DispatchThreadID) { o[i] = i * i + 7; }
