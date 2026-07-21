struct VSInput {
  float3 position : POSITION;
  float3 normal : NORMAL;
  float4 tangent : TANGENT;
  float2 uv : TEXCOORD0;
  float4 color : COLOR;
};

struct VSOutput {
  float4 position : SV_Position;
  float4 color : COLOR;
};

cbuffer Constants : register(b0) {
  float4 tint;
};

VSOutput VSMain(VSInput input) {
  VSOutput o;
  o.position = float4(input.position, 1.0);
  o.color = input.color * tint;
  return o;
}

float4 PSMain(VSOutput input) : SV_Target {
  return input.color;
}
