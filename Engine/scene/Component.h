#pragma once

#include "MathUtility.h"   // Matrix4x4

// ────────────────────────────────────────────────────────────────────
// 前方宣言
//   GameObject は「持たれる側から指すだけ」なので実体は不要。
//   ここで #include すると GameObject.h ⇔ Component.h の循環になる
// ────────────────────────────────────────────────────────────────────
class GameObject;
struct EngineContext;

// ════════════════════════════════════════════════════════════════════
// コンポーネントの基底
//   GameObject に付けて機能を足すための共通インターフェース
//   ・付ける側（GameObject）は中身を知らなくていい
//   ・エディタは GetTypeName() と DrawInspector() だけ知っていればいい
// ════════════════════════════════════════════════════════════════════
class Component {
public:
	// ────────────────────────────────────────────────────────────────
	// 特殊メンバ
	// ────────────────────────────────────────────────────────────────

	// unique_ptr で派生を持つので仮想デストラクタは必須
	//   これが無いと派生のデストラクタが呼ばれずリークする
	virtual ~Component() = default;

	// コピー禁止。owner_ が付け替わって壊れるため
	Component(const Component&) = delete;
	Component& operator=(const Component&) = delete;

	// ────────────────────────────────────────────────────────────────
	// 派生が必要に応じて上書きするもの
	//   空実装にしてあるので、使わないものは書かなくてよい
	// ────────────────────────────────────────────────────────────────

	/// <summary>GameObject に付けられた直後に1回だけ呼ばれる</summary>
	virtual void Initialize() {}

	/// <summary>毎フレーム。値を変えるだけで描画コマンドは積まない</summary>
	virtual void Update() {}

	/// <summary>描画コマンドを積む。描画しないコンポーネントは上書き不要</summary>
	/// <param name="viewProjectionMatrix">今のカメラのビュー×射影</param>
	virtual void Draw(const Matrix4x4& viewProjectionMatrix) { (void)viewProjectionMatrix; }

	/// <summary>インスペクタに自分の設定項目を描く</summary>
	virtual void DrawInspector() {}

	/// <summary>インスペクタの見出しに出す名前。実装必須</summary>
	virtual const char* GetTypeName() const = 0;

	// ────────────────────────────────────────────────────────────────
	// 共通の機能
	// ────────────────────────────────────────────────────────────────

	/// <summary>自分が付いている GameObject</summary>
	GameObject* GetOwner() const { return owner_; }

	/// <summary>エンジンの道具（owner 経由で借りる）</summary>
	const EngineContext& GetContext() const;

	/// <summary>false なら Update / Draw がスキップされる</summary>
	bool IsEnabled() const { return isEnabled_; }
	void SetEnabled(bool enabled) { isEnabled_ = enabled; }

protected:
	// 直接 new させない。派生クラスからだけ構築できる
	//   （コピーコンストラクタを delete すると暗黙の既定コンストラクタが
	//     消えるので、明示的に書く必要がある）
	Component() = default;

private:
	// owner_ を設定できるのは GameObject だけ
	friend class GameObject;

	// 自分を持っている GameObject。所有していないので delete しない
	GameObject* owner_ = nullptr;

	bool isEnabled_ = true;
};