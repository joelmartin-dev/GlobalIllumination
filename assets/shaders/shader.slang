struct VertexInput {
  float2 inPosition;
  float3 inColour;
};

struct UniformBuffer {
    float4x4 model;
    float4x4 view;
    float4x4 proj;
}
ConstantBuffer<UniformBuffer> ubo;

struct VertexOutput {
  float4 position : SV_Position;
  float3 colour;
};

[shader("vertex")]
VertexOutput vertMain(VertexInput input)
{
  VertexOutput output;
  output.position = mul(ubo.proj, mul(ubo.view, mul(ubo.model, float4(input.inPosition, 0.0, 1.0))));
  output.colour = input.inColour;
  return output;
}

[shader("fragment")]
float4 fragMain(VertexOutput inVert) : SV_Target
{
  return float4(inVert.colour, 1.0);
}