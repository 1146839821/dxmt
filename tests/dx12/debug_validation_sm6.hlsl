struct PixelInput {
    float4 position : SV_Position;
};

float4 main(PixelInput input) : SV_Target {
    return input.position;
}
