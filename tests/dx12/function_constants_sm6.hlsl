cbuffer color : register(b0, space7) {
    uint4 color;
};

uint4 main() : SV_Target {
    return color;
}
