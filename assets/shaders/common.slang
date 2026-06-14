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

// the indices of the scene
[[vk::binding(1, 0)]] public StructuredBuffer<uint> indexBuffer;

// the vertex colours of the scene
[[vk::binding(2, 0)]] public StructuredBuffer<float3> colourBuffer;

// the uvs of each vertex in the scene
[[vk::binding(3, 0)]] public StructuredBuffer<float2> uvBuffer;

// the normals of each vertex in the scene
[[vk::binding(4, 0)]] public StructuredBuffer<float4> nrmBuffer;

// the normals of each vertex in the scene
[[vk::binding(5, 0)]] public StructuredBuffer<float4> tangBuffer;

// Sampler of the written-to image
[[vk::binding(6, 0)]] public SamplerState sampler;

// Write access to StorageImages
[vk_image_format("rgba32f")]
[[vk::binding(8, 0)]] public RWTexture2D<float4> outImage[];

// Sample access to StorageImages
[vk_image_format("rgba32f")]
[[vk::binding(9, 0)]] public Texture2D<float4> renderTextures[];

// Sampler for texture array
[[vk::binding(0, 1)]] public SamplerState textureSampler;

// the array of the scene's textures (texture atlas)
[[vk::binding(1, 1)]] public Texture2D<float4> textures[];
