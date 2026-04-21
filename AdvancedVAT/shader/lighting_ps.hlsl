
#define USE_TEXTURE 2

//0:通常ライト 1:スペキュラー付きライト 2:ディザリング付きライト 3:テクスチャ付きスペキュラーライト

//通常のライト計算
#if USE_TEXTURE==0


cbuffer global : register(b1)
{
    float4 LightPos;
    float4 DiffuseColor;
};

struct VS_OUTPUT
{
    float4 Position : SV_POSITION;
    float3 WorldPos : TEXCOORD0;
    float3 Normal : TEXCOORD1;
};

float4 main(VS_OUTPUT input) : SV_Target
{
	// Lambert
    float3 lightDir = normalize(LightPos.xyz - input.WorldPos);
    float NdotL = max(dot(input.Normal, lightDir), 0.0);
    float4 color = float4(DiffuseColor.rgb * NdotL, DiffuseColor.a);
    return color;
}



///スペキュラーハイライト付きライト計算
#elif USE_TEXTURE==1




cbuffer global : register(b1)
{
    //ライト計算に必要な情報をCPUから送ってもらい
    float4 LightPos; //ライト方向
    float4 DiffuseColor;
    
    float4 CameraPos; //視線ベクトル？
    float intensity; //光の強さ 
    float power; //光の鋭さ
    float2 pad;
};

struct VS_OUTPUT
{
    float4 Position : SV_POSITION;
    float3 WorldPos : TEXCOORD0;
    float3 Normal : TEXCOORD1;
   
};

float4 main(VS_OUTPUT input) : SV_Target
{
    // Lambert
    //float3 lightDir = normalize(LightPos.xyz-input.WorldPos);
    //float NdotL= max(dot(input.Normal, lightDir), 0.0);
    //float4 color = float4(DiffuseColor.rgb* NdotL, DiffuseColor.a);
    //return color;
    
  
    
    float3 N = normalize(input.Normal);

    // ライト方向
    float3 L = normalize(LightPos.xyz - input.WorldPos);

    // 視線方向
    float3 V = normalize(CameraPos.xyz - input.WorldPos);

    // 拡散(Lambert)
    float NdotL = max(dot(N, L), 0.0); //表面法線と半ベクトルの角度
    float3 diffuse = DiffuseColor.rgb * NdotL;

    // Blinn-Phong スペキュラー
    float3 H = normalize(L + V); // 半ベクトル
    float NdotH = max(dot(N, H), 0.0);
    float spec = pow(NdotH, power) * intensity;
    float3 specular = spec * 1.0.xxx; // 白ハイライト
    
    float3 finalColor = diffuse + specular;

    return float4(finalColor, DiffuseColor.a);
    
    //ライトベクトルLと視線ベクトルVの半分のベクトルHを求める
    //法線NとHの角度を調べる
    //ハイライトの鋭さをpowで調整
    
}

//ディザリング処理付きライト計算
#elif USE_TEXTURE==2




cbuffer global : register(b1)
{
    float4 LightPos; // xyz: light pos, w: timeSec (from CPU)
    float4 DiffuseColor; // rgb: color, a: alpha
    float4 CameraPos; // xyz: camera pos, w: ditherStrength (0..1)
    float intensity;
    float power;
    float2 _unused; // ← “pad” という名前にしない（C++側に無いなら合わせない）
};

struct VS_OUTPUT
{
    float4 Position : SV_POSITION;
    float3 WorldPos : TEXCOORD0;
    float3 Normal : TEXCOORD1;
};

float Hash21(float2 p)
{
    // 低コスト適当ハッシュ（ディザ用途）
    p = frac(p * float2(0.1031, 0.1030));
    p += dot(p, p.yx + 33.33);
    return frac((p.x + p.y) * p.x);
}

float3 HSVtoRGB(float3 hsv)
{
    // hsv: (h=0..1, s=0..1, v=0..1)
    float3 rgb = saturate(abs(frac(hsv.x + float3(0, 2.0 / 3.0, 1.0 / 3.0)) * 6.0 - 3.0) - 1.0);
    return hsv.z * lerp(float3(1, 1, 1), rgb, hsv.y);
}

// 四角ドット（網点）: 1=塗る, 0=抜く
float SquareDotMask(float2 screenPos, float cellPx, float dotRatio, float2 scrollPx)
{
    // screenPos: pixel coords
    float2 p = screenPos + scrollPx;
    float2 g = frac(p / cellPx); // 0..1 within cell
    float2 d = abs(g - 0.5); // center distance
    float r = dotRatio * 0.5; // 0..0.5
    // 正方形ドット：中心から r 以内なら 1
    float inside = (d.x <= r) && (d.y <= r);
    return inside ? 1.0 : 0.0;
}


float4 main(VS_OUTPUT input) : SV_Target
{
    // ここは元のライティングがあるなら残してOKだが、板をレインボー塗りしたいので上書きする

    float timeSec = LightPos.w; // C++から渡してる time
    float strength = saturate(CameraPos.w); // C++から渡してる ditherStrength (0..1)

    // --- Rainbow base color ------------------------------------
    // 位置で色相が変わる + 時間で少し流す（板が白でも勝手に虹になる）
    float hue = frac(input.WorldPos.x * 0.08 + input.WorldPos.z * 0.06 + timeSec * 0.10);
    float sat = 0.95;
    float val = 1.00;

    // 音が強いほど彩度/明度をほんの少し上げる（気持ちいい寄せ）
    sat = saturate(sat + strength * 0.05);
    val = saturate(val + strength * 0.05);

    float3 baseCol = HSVtoRGB(float3(hue, sat, val));

    // --- grid dithering only when audio is "active" -------------
    // あなたの画像みたいに「格子で抜ける」表現は clip が一番近い
    if (strength > 0.001)
    {
        // 画像っぽい大きめの網点（数値は好みで調整）
        float cellPx = lerp(18.0, 10.0, strength); // 音が強いほど細かく
        float dotRatio = lerp(0.55, 0.35, strength); // 音が強いほど穴が増える（抜けやすく）

        // “流体っぽい”＝格子自体をゆっくり流す（スクロール）
        float2 scrollPx = float2(timeSec * 30.0, timeSec * 17.0);

        float m = SquareDotMask(input.Position.xy, cellPx, dotRatio, scrollPx);

        // m=0 を捨てる（透ける）
        clip(m - 0.5);
    }

    return float4(baseCol, 1.0);
}

//テクスチャも使ったスペキュラーハイライト付きライト計算
#else

Texture2D gTexture : register(t0);
SamplerState gSampler : register(s0);


cbuffer global : register(b1)
{
    //ライト計算に必要な情報をCPUから送ってもらい
    float4 LightPos; //ライト方向
    float4 DiffuseColor;
    
    float4 CameraPos; //視線ベクトル？
    float intensity; //光の強さ 
    float power; //光の鋭さ
    float2 pad;
};

struct VS_OUTPUT
{
    float4 Position : SV_POSITION;
    float3 WorldPos : TEXCOORD0;
    float3 Normal : TEXCOORD1;
    float2 UV : TEXCOORD2;
};

float4 main(VS_OUTPUT input) : SV_Target
{
    // Lambert
    //float3 lightDir = normalize(LightPos.xyz-input.WorldPos);
    //float NdotL= max(dot(input.Normal, lightDir), 0.0);
    //float4 color = float4(DiffuseColor.rgb* NdotL, DiffuseColor.a);
    //return color;
    
    float3 albedo = gTexture.Sample(gSampler, input.UV).rgb;
    
    float3 N = normalize(input.Normal);

    // ライト方向
    float3 L = normalize(LightPos.xyz - input.WorldPos);

    // 視線方向
    float3 V = normalize(CameraPos.xyz - input.WorldPos);

    // 拡散(Lambert)
    float NdotL = max(dot(N, L), 0.0); //表面法線と半ベクトルの角度
    float3 diffuse = DiffuseColor.rgb * NdotL;

    // Blinn-Phong スペキュラー
    float3 H = normalize(L + V); // 半ベクトル
    float NdotH = max(dot(N, H), 0.0);
    float spec = pow(NdotH, power) * intensity;
    float3 specular = spec * 1.0.xxx; // 白ハイライト
    float3 finalColor = diffuse * albedo + specular;
    //float3 finalColor = diffuse + specular;

    return float4(finalColor, DiffuseColor.a);
    
    //ライトベクトルLと視線ベクトルVの半分のベクトルHを求める
    //法線NとHの角度を調べる
    //ハイライトの鋭さをpowで調整
    
}
#endif