#pragma once

#include <cstdint>

// ════════════════════════════════════════════════════════════════════
// エディタのパネル
//   kMaxCount を最後に置くと、配列のサイズが自動で決まる
// ════════════════════════════════════════════════════════════════════
/// <summary>エディタのパネル種別</summary>
enum class EditorPanel : uint32_t {
	Toolbar,    // 最上段    : 再生／一時停止、Edit / Game 切り替え（固定配置）
	View,       // 中央      : ゲーム画面（オフスクリーンの RenderTexture）
	Hierarchy,  // 左        : オブジェクト一覧
	Inspector,  // 右        : 選択中オブジェクトの詳細
	Asset,      // 下段 左   : アセット一覧
	Debug,      // 下段 中央 : ログ・FPS など
	Settings,   // 下段 右   : ライトや UV などゲーム固有の設定
	kMaxCount,  // 種別の数（配列サイズ用。これより下に増やさない）
};

// ════════════════════════════════════════════════════════════════════
// エディタの画面レイアウト
//   ・ツールバーだけは最上段に固定する（ドッキング対象から外す）
//   ・それ以外のパネルはドッキング可能。位置とサイズは ImGui が持つ
//   ・初回起動時だけ既定の配置をコードで組む。以降は imgui.ini が復元する
//
//   rects_ の意味
//     各パネルが「実際に置かれた矩形」を記録したもの。
//     こちらから位置を指示するための値ではない（ツールバーだけは例外）
// ════════════════════════════════════════════════════════════════════
class EditorLayout {
public:
	/// <summary>ホストウィンドウとドックスペースを発行する。毎フレーム最初に呼ぶ</summary>
	///   ドッキング可能なパネルを BeginPanel するより前に呼ぶ必要がある
	void Update();

	/// <summary>パネルを開く。ImGui::Begin と同じく、戻り値に関わらず EndPanel を呼ぶこと</summary>
	bool BeginPanel(EditorPanel panel);

	/// <summary>パネルを閉じる</summary>
	void EndPanel();

	/// <summary>指定した点がパネルの矩形の中にあるか</summary>
		/// <param name="panel">調べたいパネル</param>
		/// <param name="x">クライアント座標 X（= ImGui の画面座標）</param>
		/// <param name="y">クライアント座標 Y</param>
	bool IsInsidePanel(EditorPanel panel, float x, float y) const;

	/// <summary>次のフレームで既定配置に戻すよう予約する</summary>
	///   パネルを重ねすぎて戻せなくなったときの脱出口
	void RequestResetLayout() { resetLayoutRequested_ = true; }


private:
	// 1つのパネルの矩形（imgui.h に依存しないよう自前で持つ）
	struct Rect {
		float x = 0.0f;
		float y = 0.0f;
		float width = 0.0f;
		float height = 0.0f;
	};

	/// <summary>いま開いているウィンドウの実際の矩形を記録する</summary>
	///   ImGui::Begin と ImGui::End の間でだけ呼べる
	/// <param name="isVisible">Begin の戻り値。false なら矩形を空にして当たり判定を無効にする</param>
	void RecordRect(EditorPanel panel, bool isVisible);

	/// <summary>初回起動時の既定配置を組む</summary>
	///   ImGuiID は unsigned int なので、imgui.h に依存せず uint32_t で受け取る
	void BuildDefaultLayout(uint32_t dockSpaceId, float width, float height);

	Rect rects_[static_cast<uint32_t>(EditorPanel::kMaxCount)]{};

	// ────────────────────────────────────────────────────────────────
	// レイアウトの寸法
	// ────────────────────────────────────────────────────────────────
	// ツールバーの高さは ImGui のスタイル（フォントサイズ）から毎フレーム求める
	//   固定値にすると、フォントを変えたときに中身がはみ出す
	float toolbarHeight_ = 40.0f;

	// 既定配置に戻す予約
	//   ボタンが押されるのは Update より後なので、その場では実行できない
	bool resetLayoutRequested_ = false;

	// ────────────────────────────────────────────────────────────────
	// 初回起動時の既定配置の比率
	//   ドッキングは分割位置を比率で持つので、ピクセル値を渡しても意味がない
	//   ユーザーが変えた値は imgui.ini に残るため、ここは最初の1回だけ効く
	// ────────────────────────────────────────────────────────────────
	static constexpr float kBottomRatio = 0.22f;   // 全体に対する下段の高さ
	static constexpr float kHierarchyRatio = 0.13f;   // 中段に対する左の幅
	static constexpr float kInspectorRatio = 0.22f;   // 左を取った残りに対する右の幅
	static constexpr float kAssetRatio = 0.28f;   // 下段に対する左の幅
	static constexpr float kSettingsRatio = 0.35f;   // 左を取った残りに対する右の幅
};