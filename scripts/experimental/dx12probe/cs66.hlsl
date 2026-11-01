RWStructuredBuffer<uint> o : register(u0);
[numthreads(64,1,1)] void cs(uint i : SV_DispatchThreadID) {
  uint lanes = WaveActiveSum(1u);           // all lanes active -> lanes == WaveGetLaneCount()
  uint64_t big = (uint64_t)i * 4294967296ull; // int64 op
  o[i] = i * i + 7 + (lanes - WaveGetLaneCount()) + (uint)(big >> 32) - i;
}
