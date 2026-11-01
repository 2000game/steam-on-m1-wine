float4 vs(uint id : SV_VertexID) : SV_Position { float2 p[3] = { float2(-1,-1), float2(0,3), float2(3,-1) }; return float4(p[id] * 0.9, 0, 1); }
float4 ps() : SV_Target { return float4(0.25, 0.5, 0.75, 1.0); }
