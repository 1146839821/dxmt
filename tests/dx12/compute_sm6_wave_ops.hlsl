RWStructuredBuffer<uint> output : register(u0);

[numthreads(32, 1, 1)]
void main(uint3 tid : SV_DispatchThreadID)
{
    const uint lane = WaveGetLaneIndex();
    const uint lane_count = WaveGetLaneCount();
    const bool any_true = WaveActiveAnyTrue(lane == 0);
    const bool all_true = WaveActiveAllTrue(lane < lane_count);
    const uint4 ballot = WaveActiveBallot(lane == 0);
    const uint lane_at = WaveReadLaneAt(lane, lane_count - 1);
    const uint first = WaveReadLaneFirst(lane);
    const uint4 match = WaveMatch(lane & 1);
    const uint sum = WaveActiveSum(lane);
    const uint product = WaveActiveProduct((lane & 1) ? 2 : 1);
    const uint minimum = WaveActiveMin(lane);
    const uint maximum = WaveActiveMax(lane);
    const uint prefix_sum = WavePrefixSum(lane);
    const uint prefix_sum_range = WavePrefixSum(lane + 1);
    const uint prefix_product = WavePrefixProduct(lane + 1);

    if (lane == 0) {
        const bool passed = lane_count == 32 && any_true && all_true && ballot.x == 1 && lane_at == 31 &&
                            first == 0 && match.x == 0x55555555 && match.y == 0 && sum == 496 && product == 65536 &&
                            minimum == 0 && maximum == 31 &&
                            prefix_sum == 0 && prefix_product == 1;
        output[0] = passed ? 0x00C0FFEE : 0;
    }

    if (lane == 1) {
        output[3] = match.x;
        output[4] = match.y;
        output[5] = 0x00C0FFEE;
    }

    if (lane == 31) {
        output[1] = prefix_product;
        output[2] = prefix_sum_range;
    }
}
