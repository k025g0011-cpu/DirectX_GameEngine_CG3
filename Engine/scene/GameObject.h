#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "Component.h"
#include "MathUtility.h"   // Transform / Matrix4x4

struct EngineContext;

// ════════════════════════════════════════════════════════════════════
// シーンに置かれるオブジェクト
//   ・Transform は必ず持つ（直接メンバ）
//   ・それ以外の機能は Component を足して表現する
// ════════════════════════════════════════════════════════════════════
class GameObject {
public:
	/// <summary>初期化</summary>
	/// <param name="context">エンジンの道具。コンポーネントが借りる</param>
	/// <param name="name">ヒエラルキーに出る名前</param>
	void Initialize(const EngineContext& context, const std::string& name);

	/// <summary>毎フレームの更新。全コンポーネントに配る</summary>
	void Update();

	/// <summary>描画コマンドを積む。全コンポーネントに配る</summary>
	void Draw(const Matrix4x4& viewProjectionMatrix);

	/// <summary>インスペクタに Transform と全コンポーネントを描く</summary>
	void DrawInspector();

	// ────────────────────────────────────────────────────────────────
	// コンポーネント
	// ────────────────────────────────────────────────────────────────

	/// <summary>コンポーネントを追加して、そのポインタを返す</summary>
	/// <typeparam name="T">Component の派生クラス</typeparam>
	/// <param name="args">T のコンストラクタにそのまま渡される引数</param>
	template <class T, class... Args>
	T* AddComponent(Args&&... args);

	/// <summary>指定した型のコンポーネントを探す。無ければ nullptr</summary>
	template <class T>
	T* GetComponent();

	template <class T>
	const T* GetComponent() const;

	/// <summary>指定した型を持っているか</summary>
	template <class T>
	bool HasComponent() const { return GetComponent<T>() != nullptr; }

	// インスペクタ用（型を知らずに全部を列挙する）
	uint32_t   GetComponentCount() const { return static_cast<uint32_t>(components_.size()); }
	Component* GetComponentAt(uint32_t index) const { return components_[index].get(); }

	// ────────────────────────────────────────────────────────────────
	// Transform（コンポーネントではない）
	// ────────────────────────────────────────────────────────────────
	Transform& GetTransform() { return transform_; }
	const Transform& GetTransform() const { return transform_; }

	/// <summary>Transform から作ったワールド行列</summary>
	Matrix4x4 GetWorldMatrix() const;

	// ────────────────────────────────────────────────────────────────
	// 基本情報
	// ────────────────────────────────────────────────────────────────
	const std::string& GetName() const { return name_; }
	void SetName(const std::string& name) { name_ = name; }

	/// <summary>false なら Update / Draw ごとスキップされる</summary>
	bool IsActive() const { return isActive_; }
	void SetActive(bool active) { isActive_ = active; }

	/// <summary>エンジンの道具</summary>
	const EngineContext& GetContext() const { return *context_; }

private:
	// 借りているもの（所有していない）
	const EngineContext* context_ = nullptr;

	std::string name_;
	bool isActive_ = true;

	// 必ず持つ。scale は 1 から始める（0 だと何も見えない）
	Transform transform_{ { 1.0f, 1.0f, 1.0f }, { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } };

	// 足された機能たち。unique_ptr なので GameObject と寿命を共にする
	std::vector<std::unique_ptr<Component>> components_;
};


// ════════════════════════════════════════════════════════════════════
// テンプレートの実装
//   テンプレートは「使われた時点」でコンパイルされるので、
//   .cpp に書くとリンクエラー（LNK2019）になる。必ずヘッダに置く
// ════════════════════════════════════════════════════════════════════

template <class T, class... Args>
T* GameObject::AddComponent(Args&&... args) {
	// Component の派生でなければコンパイル時に止める
	static_assert(std::is_base_of_v<Component, T>,
		"AddComponent<T> : T must derive from Component");

	// ① 実体を作る（args はそのまま T のコンストラクタへ転送）
	std::unique_ptr<T> component = std::make_unique<T>(std::forward<Args>(args)...);

	// ② move する前に生ポインタを取っておく
	//    move した後の component は空になるので、後から .get() できない
	T* result = component.get();

	// ③ 持ち主を教える（Component の friend なので private に触れる）
	result->owner_ = this;

	// ④ 配列に入れる（ここで component は空になる）
	components_.push_back(std::move(component));

	// ⑤ owner_ が入った状態で初期化する
	//    Initialize の中で GetOwner() / GetContext() が使えるようにするため
	result->Initialize();

	return result;
}

template <class T>
T* GameObject::GetComponent() {
	for (const std::unique_ptr<Component>& component : components_) {
		// 「T に変換できるか」を実行時に試す。できなければ nullptr が返る
		if (T* result = dynamic_cast<T*>(component.get())) {
			return result;
		}
	}
	return nullptr;
}

template <class T>
const T* GameObject::GetComponent() const {
	// 非 const 版を使い回す（探索処理を2回書かないための定石）
	return const_cast<GameObject*>(this)->GetComponent<T>();
}