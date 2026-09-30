AppendStructuredBuffer<uint> output : register(u0);

[numthreads(1, 1, 1)]
void main()
{
    output.Append(77);
}
