Texture2D<float4> sampled_texture : register(t0);
SamplerState texture_sampler : register(s0);
FeedbackTexture2D<SAMPLER_FEEDBACK_MIN_MIP> feedback : register(u1);

[numthreads(1, 1, 1)]
void main() {
  feedback.WriteSamplerFeedbackLevel(
      sampled_texture, texture_sampler, float2(0.5, 0.5), 0.0);
}
