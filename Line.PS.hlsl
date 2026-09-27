struct PixelShaderInput
{
    float32_t4 position : SV_POSITION;
    float32_t4 color : COLOR0;
};

struct PixelShaderOutput
{
    float32_t4 color : SV_TARGET0;
};

PixelShaderOutput main(PixelShaderInput input)
{
    PixelShaderOutput output;
    output.color = input.color; // 受け取った色をそのまま出力
    return output;
}