# AdvancedVAT — Script・アーキテクチャドキュメント

> 使用言語: C++ / HLSL  
> 制作期間: 約2ヶ月  
> 制作時期: 2025/09〜2025/11  
> 制作形態: 個人制作

---

## 目次

1. [Scriptコード抜粋](#1-scriptコード抜粋)
2. [システム全体像](#2-システム全体像)
3. [アーキテクチャ詳細](#3-アーキテクチャ詳細)
   - [ディレクトリ構成](#31-ディレクトリ構成)
   - [クラス・モジュール設計](#32-クラスモジュール設計)
   - [シェーダーファイル一覧](#33-シェーダーファイル一覧)
   - [外部ライブラリ依存](#34-外部ライブラリ依存)
   - [GoFデザインパターン対応表](#35-gofデザインパターン対応表)
   - [既知の問題・要改善点](#36-既知の問題要改善点)

---

## 1. Scriptコード抜粋

### ① リアルタイム音声解析 — `VAT_demo.cpp`（VATAudio）

WinMMコールバックでPCMサンプルからRMS（音量）とゼロクロス数（ピッチ推定）を計算し `std::atomic` に書き込む。描画ループはロックなしで最新値を読む。`Update()` で線形補間（smooth=0.15）してガクガクを防ぐ。

```cpp
// コールバック内：バッファが埋まるたびに呼ばれる
static void CALLBACK WaveInProc(HWAVEIN hwi, UINT msg, DWORD_PTR, DWORD_PTR hdr_, DWORD_PTR) {
    if (msg != WIM_DATA) return;
    WAVEHDR* hdr = (WAVEHDR*)hdr_;
    const int16_t* samples = (const int16_t*)hdr->lpData;
    int count = hdr->dwBytesRecorded / sizeof(int16_t);

    // RMS（音量）
    double acc = 0.0;
    for (int i = 0; i < count; ++i) acc += (double)samples[i] * samples[i];
    s_lastRms.store((float)std::sqrt(acc / count) / 32768.0f);

    // ゼロクロス（ピッチ推定）
    int crossings = 0;
    for (int i = 1; i < count; ++i)
        if ((samples[i-1] >= 0) != (samples[i] >= 0)) ++crossings;
    float hz = (float)crossings * kSampleRate / (2.0f * count);
    s_lastHz.store(hz);

    waveInAddBuffer(hwi, hdr, sizeof(WAVEHDR)); // バッファを再投入
}

// 毎フレーム：スムージングして描画側に渡す
static void Update() {
    const float smooth = 0.15f;
    s_amp.store(  Lerp(s_amp.load(),   RmsToAmp(s_lastRms.load()),   smooth) );
    s_speed.store(Lerp(s_speed.load(), HzToSpeed(s_lastHz.load()),  smooth) );
}
```

**設計ポイント:**
- `std::atomic` によるロックフリー設計 → コールバック内でも描画ループからも安全にアクセス
- Lerp（smooth=0.15）でフレーム間のガタつきを除去し、滑らかな映像変化を実現
- JunkShooting のレイテンシ問題（1〜3秒）を WinMM コールバック方式に切り替えて約46msに改善

---

### ② VAT頂点シェーダー + ディザリング — `vat_vs.hlsl` / `lighting_ps.hlsl`

VSはSV_VertexIDとframeでVATテクスチャをサンプリングしampで頂点Yを動かす。PSはditherStrengthで格子ドットマスクをかけclip()で穴を開ける。

```hlsl
// vat_vs.hlsl
VS_OUTPUT main(float3 Pos : POSITION, uint vid : SV_VertexID) {
    // VAT テクスチャから変位と法線を取得
    float4 vat = Vat.Load(int3(min(vid, width-1), frame, 0));
    // 音量(amp) で頂点 Y 座標を変形
    float4 pos = float4(Pos.x, Pos.y + vat.w * amp, Pos.z, 1.0);
    output.Normal = normalize(mul(float4(vat.xyz, 0), World).xyz);
    output.Position = mul(pos, WVP);
    return output;
}

// lighting_ps.hlsl（USE_TEXTURE==2 : ディザリング + HSV）
float strength = saturate(CameraPos.w);    // ditherStrength を w チャンネルに流用
if (strength > 0.001) {
    // ドット格子マスク
    float cellPx   = lerp(18.0, 10.0, strength); // 音量大 = セルが細かくなる
    float dotRatio = lerp(0.55, 0.35, strength); // 音量大 = 間引きが増える
    float2 scrollPx = float2(timeSec*30.0, timeSec*17.0); // 流れるグリッド
    float m = SquareDotMask(input.Position.xy, cellPx, dotRatio, scrollPx);
    clip(m - 0.5); // ドット外のピクセルを破棄（ディザリング）
}

// HSV → RGB でレインボーカラー
float hue = frac(input.WorldPos.x * 0.08 + timeSec * 0.10);
float sat = 0.8 + strength * 0.05;
float val = 0.9 + strength * 0.05;
float3 baseCol = HSVtoRGB(float3(hue, sat, val));
```

---

### ③ JunkShootingからの発展 — 音声設計の変化

| | JunkShooting（前作） | AdvancedVAT（本作） |
|--|---------------------|---------------------|
| **方式** | Python外部実行 → txtファイル → C++読み込み | WinMMコールバック → atomic → 描画ループ直読み |
| **レイテンシ** | 1〜3秒 | フレームレベルで同期（約46ms） |
| **スレッド** | `std::thread + std::mutex` | `std::atomic`（ロックフリー） |
| **課題** | ゲーム中の切替が遅延する | 解決済み |

```cpp
// JunkShooting（前作）の方式 ─ レイテンシが課題だった
// CreateProcessW → voice_output.txt → std::mutex 読み取り → 1〜3s ラグ

// AdvancedVAT（本作） ─ WinMM コールバックで即時取得
waveInOpen(&s_hWaveIn, WAVE_MAPPER, &wf, (DWORD_PTR)&VATAudio::WaveInProc, 0, CALLBACK_FUNCTION);
// 描画ループ側：lockなし、1行で最新値を取得
const float amp   = Scene::GetVATAmp();   // atomic.load()
const float speed = Scene::GetVATSpeed(); // atomic.load()
```

---

## 2. システム全体像

### データフロー

```
マイク
  │
  ▼
WinMM CALLBACK_FUNCTION（WaveInProc）
  ├── RMS（音量）計算      → s_lastRms.store()
  └── ゼロクロス（ピッチ） → s_lastHz.store()
          │
          ▼ 毎フレーム Update()
  Lerp スムージング（smooth=0.15）
  ├── s_amp.store()
  └── s_speed.store()
          │
          ▼ GPU転送（CBV 流用）
  ┌─────────────────────────────┐
  │ Params.w       = amp        │  → VAT-VS：vat.w × amp → 頂点Y変形
  │ CameraPos.w    = dither     │  → Dither-PS：clip() → 格子外ピクセル破棄
  │ LightPos.w     = timeSec    │  → HSV-PS：H=位置/時間、S,V=音量 → レインボー
  └─────────────────────────────┘
```

### 定数バッファ流用戦略

既存のCBVパラメータの未使用チャンネル（w成分）を活用し、追加の定数バッファを増やさずに音声パラメータを転送。

| CBVフィールド | 本来の用途 | 流用内容 |
|--------------|-----------|---------|
| `LightPos.w` | パディング | `timeSec`（ディザアニメ用時刻） |
| `CameraPos.w` | パディング | `ditherStrength`（ディザ強度） |
| `Params.w` | 未使用 | `amp`（音量→頂点変形） |

### 音声パラメータ → HSVマッピング

| パラメータ | 値の算出 | 効果 |
|----------|---------|------|
| 色相 H | `WorldPos × time` | 位置と時間で色相が流れる |
| 彩度 S | `ditherStrength` | 喋るたびに彩度が上がる |
| 明度 V | `ditherStrength` | 喋るたびに明度が上がる |

---

## 3. アーキテクチャ詳細

### 3.1 ディレクトリ構成

```
VATChallengeProgram/
├── shader/                     # HLSL シェーダー（ビルド時 → .cso に変換）
│   ├── vat_vs.hlsl             # VAT 頂点変形 VS
│   ├── lighting_vs.hlsl        # 標準 Phong 照明 VS
│   ├── lighting_ps.hlsl        # ディザリング・HSV・Phong PS（USE_TEXTURE 3モード）
│   ├── copyback_vs.hlsl        # スクリーンクワッド変換（ポストプロセス用）
│   └── copyback_ps.hlsl        # オフスクリーン RT → スクリーンコピー
├── include/                    # C++ ヘッダー群
│   ├── dx12_util.h             # DX12 デバイス・コマンドキュー・フェンス
│   ├── dx12_resource.h         # VB/IB/CB/深度バッファ基底
│   ├── primitive.h             # ドローコール・DrawFunc enum
│   ├── shader.h                # VS+PS シェーダーペア
│   ├── root_signature.h        # GPU バインディング仕様
│   ├── camera.h                # ビュー・プロジェクション行列
│   ├── texture.h               # テクスチャ読み込み
│   ├── shadow.h                # シャドウマップ
│   └── offscreen.h             # オフスクリーンレンダリング
├── source/                     # 実装 (.cpp)
├── scene.cpp                   # シーン初期化・カメラ・VAT 平面生成
├── scene_draw.cpp              # AppVAT()：メインレンダリングループ
├── scene_resources.cpp         # std::map によるリソースレジストリ
├── scene_util.cpp              # PSO 初期化・スプライン等ユーティリティ
├── VAT_demo.cpp                # WinMM 音声キャプチャ + VAT テクスチャ生成
└── x64/Release/
    ├── dx12_simple.exe         # 実行ファイル（exeがあるディレクトリから起動可能）
    ├── *.cso                   # コンパイル済みシェーダー（exe と同一ディレクトリ）
    └── assimp-vc140-mt.dll     # 3Dモデルインポート用DLL
```

> **起動方法:** `x64/Release/dx12_simple.exe` をダブルクリックで直接起動可能。  
> シェーダーパスは `GetModuleFileNameW()` で exe の場所を取得し動的に解決するため、  
> バッチファイルや作業ディレクトリの設定は不要。

---

### 3.2 クラス・モジュール設計

| クラス / モジュール | ファイル | 責務 |
|-------------------|---------|------|
| `Dx12Util` | `dx12_util.h/.cpp` | DirectX12 デバイス・スワップチェーン・コマンドキュー・フェンス同期を一元管理。`Begin()/End()` でフレームライフサイクルを提供 |
| `SimpleResourceSet` | `dx12_resource.h` | VB/IB/CB/深度バッファ・デスクリプタヒープを保持する基底リソースクラス |
| `SimpleObject` | `primitive.h/.cpp` | `SimpleResourceSet` を継承。`DrawFunc` enum に基づき `DrawCommand()` でドローコールを実行 |
| `SimpleShader_VS_PS` | `shader.h/.cpp` | VS + PS シェーダーバイトコードペアを保持。`static Initialize()` が `.cso` ファイルから生成 |
| `RootSignature` + 派生6クラス | `root_signature.h/.cpp` | GPU リソースバインディング仕様（CBV/SRV/Sampler スロット構成）を定義。`CreateDefaultPipelineState()` で PSO 生成 |
| `SimpleCamera` | `camera.h/.cpp` | 視点/注視/上ベクトルを保持。ビュー・プロジェクション行列生成とマウス制御 |
| `VATAudio`（内部クラス） | `VAT_demo.cpp` | WinMM コールバックで PCM サンプルから RMS（音量）とゼロクロス（ピッチ）を計算し `std::atomic` に書き込む。ロックフリー設計 |

---

### 3.3 シェーダーファイル一覧

| ファイル | 種別 | 機能概要 |
|---------|------|---------|
| `vat_vs.hlsl` | Vertex Shader | `SV_VertexID × frame` で VAT テクスチャをサンプリング。`vat.w × amp` で頂点 Y 座標を変形。法線も VAT テクスチャから取得 |
| `lighting_vs.hlsl` | Vertex Shader | 標準 Phong 照明用。位置・法線・UV をワールド空間へ変換して出力 |
| `lighting_ps.hlsl` | Pixel Shader（3モード） | `USE_TEXTURE==0`: Lambert拡散光 / `USE_TEXTURE==1`: Blinn-Phong鏡面反射 / `USE_TEXTURE==2`: ディザリング + HSVレインボー（VATデモ用） |
| `copyback_vs.hlsl` | Vertex Shader | スクリーンクワッド変換（ポストプロセス用） |
| `copyback_ps.hlsl` | Pixel Shader | オフスクリーン RT をスクリーンにそのままコピー |

---

### 3.4 外部ライブラリ依存

| ライブラリ | 用途 | 主な使用箇所 |
|-----------|------|------------|
| **DirectX 12** | GPU描画API（デバイス・コマンドリスト・スワップチェーン） | `dx12_util.cpp`、`dx12_resource.h` 全体 |
| **DirectXMath** | ベクトル・行列演算（`XMVECTOR / XMMATRIX`） | カメラ・ライト・ワールド行列計算 全般 |
| **DirectXTex** | テクスチャファイル読み込み（PNG/JPG等）・フォーマット変換 | `texture.cpp` |
| **WinMM** | マイク音声キャプチャ（PCM コールバック） | `VAT_demo.cpp`（`VATAudio::WaveInProc`） |
| **ImGui + DX12バックエンド** | デバッグUI・パラメータ表示オーバーレイ | `dx12_imgui.cpp` |
| **Assimp** | FBX/OBJ 等 3D モデルのインポート | `fbx_load.cpp` |
| **C++ STL** | `atomic / map / vector / string` | リソースレジストリ・スレッドセーフ音声状態管理 |

---

### 3.5 GoFデザインパターン対応表

| # | パターン | 該当 | 実装箇所 | 説明 |
|---|---------|:----:|---------|------|
| 1 | **Template Method** | ✅ | `RootSignature` 基底クラス | 基底が `CreateDefaultPipelineState()` の骨格を定義。派生6クラスが CBV/SRV/Sampler スロット構成をカスタマイズ |
| 2 | **Factory Method** | ✅ | `SimpleShader_VS_PS::Initialize()` | `static Initialize(vs, ps)` が `.cso` パスからシェーダーオブジェクトを生成するファクトリ |
| 3 | **Strategy** | ✅ | `DrawFunc enum / DrawCommand()` | 6種描画モード（`CONSTANT/TEX/NV/LIT/LIT_TEX/COPYBACK`）を enum で切り替え。PSO・ルートシグネチャを動的差し替え |
| 4 | **Facade** | ✅ | `Dx12Util::Begin() / End()` | 複雑な DX12 フレーム管理（コマンドリスト・フェンス・スワップチェーン）を Begin/End の 2 API で隠蔽 |
| 5 | **Service Locator** | ✅ | `scene_resources.cpp`（`std::map<string,T*>`） | シェーダー・PSO・テクスチャ・シグネチャを名前文字列で一元管理。`AddXxx() / GetXxx()` API |
| 6 | **PIMPL** | ✅ | `ShadowMapUtil / OffscreenUtil` | 内部 Desc 構造体をポインタで隠蔽。内部変更がパブリック API に影響しない |
| 7 | **Data-Driven** | ✅ | `VATAudio → CBV → シェーダー` | 音声 RMS/ピッチを CBV 経由で GPU に転送するデータドリブンなオーディオビジュアルマッピング |

> **凡例:** ✅ = GoFの定義に忠実な実装 / △ = 役割・意図が対応するが厳密なGoF実装ではない

---

### 3.6 既知の問題・要改善点

| 優先度 | 問題 | 場所 | 対応方針 |
|--------|------|------|---------|
| 高 | ~~シェーダーパスがマクロにハードコード~~ **→ 修正済み** | `scene.cpp` | `GetModuleFileNameW()` で exe ディレクトリを動的取得するよう変更。exe 直接起動が可能に |
| 高 | VAT スケール係数 `500.0f` がマジックナンバー | `VAT_demo.cpp CalcNormal()` | 名前付き定数化または ImGui での実行時調整 |
| 中 | グローバルスコープの静的変数（`g_pDx12` 等） | `scene.cpp` 全体 | `SceneContext` クラスへのカプセル化 |
| 中 | WinMM コールバックに有界キューなし | `VATAudio::WaveInProc` | 固定サイズリングバッファで遅延ストールを防止 |
| 中 | ルートパラメータインデックスのマジックナンバー | `primitive.cpp DrawCommand()` 内 | 名前付き定数 or enum 化 |
| 低 | スマートポインタ未使用（手動 new/delete） | `scene.cpp InitXxx() / Release()` | `unique_ptr / shared_ptr` への移行 |
| 低 | エラー時の即 exit（シェーダーコンパイル失敗等） | `shader.cpp` | フォールバックシェーダーまたはエラーダイアログ |

---

*このドキュメントは `AdvancedVAT_architecture.md` として管理しています。コード変更時は対応するセクションを更新してください。*
