#ifdef POINT_STREAM_DIFFERENTIAL
#ifndef GS_INSTANCES
#define GS_INSTANCES 1
#endif
struct VSInput { float2 position : POSITION; float4 color : COLOR; };
struct VSOutput { float4 position : SV_Position; nointerpolation float4 tag : TAG; };
struct GSOutput { float4 position : SV_Position; nointerpolation float4 value : COLOR; };
cbuffer GeometryConstants : register(b0) { uint phase; };
VSOutput vs_main(VSInput input, uint vertex : SV_VertexID, uint instance : SV_InstanceID)
{
    VSOutput o;
    o.position = float4(input.position, 0, 1);
    o.tag = float4(input.color.x + 3 * vertex, instance, 0, 1);
    return o;
}
#if GS_INSTANCES > 1
[instance(GS_INSTANCES)]
#endif
[maxvertexcount(2)]
void gs_main(point VSOutput input[1], uint primitive : SV_PrimitiveID,
             uint gs_instance : SV_GSInstanceID, inout PointStream<GSOutput> stream)
{
    uint count = phase ? primitive % 3 : 0;
    for (uint emit = 0; emit < count; ++emit)
    {
        GSOutput o;
        float2 pixel = float2(primitive * 2 + emit + input[0].tag.y * 256 + 0.5, gs_instance + 0.5);
        o.position = float4(pixel.x / 256.0 - 1, 1 - pixel.y * (2.0 / 3.0), 0, 1);
        o.value = float4(primitive + 1, (uint(input[0].tag.x) + phase + 17 * emit) & 255,
                         input[0].tag.y + 1, gs_instance + 1);
        stream.Append(o);
        // A PointStream cut must not discard already completed points.
        stream.RestartStrip();
    }
}
uint4 ps_main(GSOutput input) : SV_Target0 { return uint4(input.value); }
#elif defined(GS_DIFFERENTIAL)
#ifndef GS_INPUT
#define GS_INPUT 3
#endif
struct VSInput { float2 position : POSITION; float4 color : COLOR; };
struct VSOutput { float4 position : SV_Position; nointerpolation uint2 tag : TAG; };
struct GSOutput { float4 position : SV_Position; nointerpolation uint4 value : COLOR; };
cbuffer GeometryConstants : register(b0) { uint phase; };

VSOutput vs_main(VSInput input, uint vertex : SV_VertexID, uint instance : SV_InstanceID)
{
    VSOutput o;
    o.position = float4(input.position, 0, 1);
    o.tag = uint2(uint(input.color.x) + 3 * vertex, instance);
    return o;
}

[maxvertexcount(8)]
#if GS_INPUT == 1
void gs_main(point VSOutput input[1], uint primitive : SV_PrimitiveID, inout TriangleStream<GSOutput> stream)
#elif GS_INPUT == 2
void gs_main(line VSOutput input[2], uint primitive : SV_PrimitiveID, inout TriangleStream<GSOutput> stream)
#elif GS_INPUT == 4
void gs_main(lineadj VSOutput input[4], uint primitive : SV_PrimitiveID, inout TriangleStream<GSOutput> stream)
#elif GS_INPUT == 6
void gs_main(triangleadj VSOutput input[6], uint primitive : SV_PrimitiveID, inout TriangleStream<GSOutput> stream)
#else
void gs_main(triangle VSOutput input[3], uint primitive : SV_PrimitiveID, inout TriangleStream<GSOutput> stream)
#endif
{
    // A suppressed primitive must leave every pixel clear.
    if (primitive == 1) return;
    uint hash = 0;
    [unroll] for (uint i = 0; i < GS_INPUT; ++i)
        hash += (i + 1) * input[i].tag.x;
    uint instance = input[0].tag.y;
    uint column = primitive + instance * 128;
    [unroll] for (uint row = 0; row < 3; row += 2)
    {
        [unroll] for (uint v = 0; v < 4; ++v)
        {
            GSOutput o;
            float2 pixel = float2(column + (v & 1), row + (v >> 1));
            o.position = float4(pixel.x / 128.0 - 1, 1 - pixel.y * (2.0 / 3.0), 0, 1);
            o.value = uint4(primitive + 1, (hash + phase) & 255, instance + 1, row + GS_INPUT);
            stream.Append(o);
        }
        // Missing restart would connect the strips across the guard row.
        stream.RestartStrip();
    }
}
uint4 ps_main(GSOutput input) : SV_Target0 { return input.value; }
#else
struct VSInput
{
    float2 position : POSITION;
    float4 color : COLOR;
};

struct VSOutput
{
    float4 position : SV_Position;
    float4 color : COLOR;
};

VSOutput vs_main(VSInput input)
{
    VSOutput output;
    output.position = float4(input.position, 0.0, 1.0);
    output.color = input.color;
    return output;
}

[maxvertexcount(3)]
void gs_main(triangle VSOutput input[3], inout TriangleStream<VSOutput> stream)
{
    for (uint i = 0; i < 3; ++i)
    {
        VSOutput output = input[i];
        output.color = float4(0.0, 1.0, 0.0, 1.0);
        stream.Append(output);
    }
}

float4 ps_main(VSOutput input) : SV_Target0
{
    return input.color;
}

#endif
