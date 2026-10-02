#include "Component.h"

#include "GameObject.h"   // ここで初めて GameObject の実体が必要になる

// ════════════════════════════════════════════════════════════════════
// エンジンの道具を owner 経由で借りる
//   GameObject の中身を見るので、ヘッダではなくここに置く
// ════════════════════════════════════════════════════════════════════
const EngineContext& Component::GetContext() const {
	return owner_->GetContext();
}