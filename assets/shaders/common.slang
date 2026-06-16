module common;

// Mirrored struct of Vertex in Vertex.hpp
public struct VSInput {
    public float3 inPosition;
    public float2 inTexCoord;
    public float3 inColor;
    public float3 inNormal;
    public float4 inTangent;
};

// Internal struct for passing attributes to fragment shader
public struct VSOutput {
    public float4 pos : SV_Position;
    public float2 fragTexCoord;
    public float3 fragColor;
    public float3 fragNormal;
    public float4 fragTangent;
    public float3 worldPos;
};

// 3D Transformation Matrices + Inversions
public struct CameraTransforms {
    public float4x4 model;
    public float4x4 view;
    public float4x4 proj;
    public float4x4 invView;
    public float4x4 invProj;
};
[[vk::binding(0, 0)]] public ConstantBuffer<CameraTransforms> camera;

[[vk::binding(1, 0)]] public Sampler2D fallback;