static float2 positions[3] = float2[](
  float2(0.0, -0.5),
  float2(0.5, 0.5),
  float2(-0.5, 0.5)
);

static float3 colours[3] = float3[](
  float3(1.0, 0.0, 0.0),
  float3(0.0, 1.0, 0.0),
  float3(0.0, 0.0, 1.0)
);

struct VertexOutput {
  float3 colour;
  float4 sv_position : SV_Position;
};

[shader("vertex")]
VertexOutput vertMain(uint vid : SV_VertexID)
{
  VertexOutput output;
  output.sv_position = float4(positions[vid], 0.0, 1.0);
  output.colour = colours[vid];
  return output;
}

[shader("fragment")]
float4 fragMain(VertexOutput inVert) : SV_Target
{
  float3 colour = inVert.colour;
  return float4(colour, 1.0);
}