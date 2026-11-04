#include "ParticleEmitter.h"

#include "EngineContext.h"
#include "Field.h"
#include "GameObject.h"
#include "GameTime.h"
#include "PrimitiveDrawer.h"

#ifdef USE_IMGUI
#include "externals/imgui/imgui.h"
#endif

// このcppファイルの中だけで使う定数
namespace {
	// 1フレームに出せる上限
	constexpr float kMaxEmitPerFrame = 64.0f;

	// 発生範囲の線の色（黄色）
	constexpr Vector4 kEmitAreaColor{ 1.0f, 0.9f, 0.2f, 1.0f };
}


// ════════════════════════════════════════════════════════════════════
// 初期化
//   AddComponent の中で、owner_ が設定された直後に呼ばれる
// ════════════════════════════════════════════════════════════════════
void ParticleEmitter::Initialize() {

	// 乱数の種を用意する
	//   Emitter ごとに別の種なので、複数置いても同じ動きにならない
	std::random_device seedGenerator;
	randomEngine_.seed(seedGenerator());

	// 描画担当を初期化する（GPU バッファと SRV がここで作られる）
	particleSystem_.Initialize(GetContext(), texturePath_, maxParticleCount_);


	// ────────────────────────────────────────────────────────────────
	// 既定の場を1つ入れておく（+X 方向へ流す風）
	//   最初は無効にしておき、Inspector でチェックを入れて効果を見る
	// ────────────────────────────────────────────────────────────────
	std::unique_ptr<AccelerationField> wind =
		std::make_unique<AccelerationField>(Vector3{ 10.0f, 0.0f, 0.0f });

	wind->GetArea().min = { -5.0f, -5.0f, -5.0f };
	wind->GetArea().max = { 5.0f, 5.0f, 5.0f };
	wind->SetEnabled(false);

	particleSystem_.AddField(std::move(wind));
}

// ════════════════════════════════════════════════════════════════════
// 更新
//   ① 頻度に従って粒を発生させる
//   ② 既に出ている粒を進める
// ════════════════════════════════════════════════════════════════════
void ParticleEmitter::Update() {

	const float deltaTime = GetContext().gameTime->GetDeltaTime();

	// ────────────────────────────────────────────────────────────────
	// ① 発生
	//     「1秒に20個」のような小数の頻度を、端数を持ち越して実現する
	// ────────────────────────────────────────────────────────────────
	if (isPlaying_) {

		emitAccumulator_ += emitRate_ * deltaTime;

		// 出しすぎの保険
		if (emitAccumulator_ > kMaxEmitPerFrame) {
			emitAccumulator_ = kMaxEmitPerFrame;
		}

		// 1.0 たまるごとに1個出す
		while (emitAccumulator_ >= 1.0f) {
			particleSystem_.Add(MakeParticle());
			emitAccumulator_ -= 1.0f;
		}
	}

	// ────────────────────────────────────────────────────────────────
	// ② 粒を進める（移動・寿命・削除）
	// ────────────────────────────────────────────────────────────────
	particleSystem_.Update(deltaTime);
}

// ════════════════════════════════════════════════════════════════════
// 描画
//   Component::Draw ではないので、GameScene の専用パスから呼ばれる
// ════════════════════════════════════════════════════════════════════
void ParticleEmitter::DrawParticles(const CameraData& camera) {
	particleSystem_.Draw(camera);
}

// ════════════════════════════════════════════════════════════════════
// デバッグ用の線
//   ① 自分の発生範囲
//   ② 持っている場の効果範囲
// ════════════════════════════════════════════════════════════════════
void ParticleEmitter::DrawGizmos(PrimitiveDrawer& drawer) {

	if (!showGizmos_) {
		return;
	}

	// ────────────────────────────────────────────────────────────────
	// ① 発生範囲
	//     GameObject の位置を中心に、emitRange_ だけ広がった箱
	//     MakeParticle() が粒を置く範囲と一致させること
	// ────────────────────────────────────────────────────────────────
	const Vector3& position = GetOwner()->GetTransform().translate;

	AABB emitArea{};
	emitArea.min = position - emitRange_;
	emitArea.max = position + emitRange_;

	DrawAABBWireframe(drawer, emitArea, kEmitAreaColor);

	// ────────────────────────────────────────────────────────────────
	// ② 場の効果範囲（色は場自身が決める）
	// ────────────────────────────────────────────────────────────────
	for (uint32_t index = 0; index < particleSystem_.GetFieldCount(); ++index) {
		particleSystem_.GetFieldAt(index)->DrawGizmos(drawer);
	}
}

// ════════════════════════════════════════════════════════════════════
// まとめて出す
// ════════════════════════════════════════════════════════════════════
void ParticleEmitter::Burst(uint32_t count) {
	for (uint32_t index = 0; index < count; ++index) {
		particleSystem_.Add(MakeParticle());
	}
}

// ════════════════════════════════════════════════════════════════════
// 粒を1個組み立てる
//   発生位置は「GameObject の位置」を中心とした AABB の中
// ════════════════════════════════════════════════════════════════════
Particle ParticleEmitter::MakeParticle() {

	// -1.0 〜 +1.0 の素の乱数。幅を掛けて使う
	std::uniform_real_distribution<float> distUnit(-1.0f, 1.0f);

	// 最小 > 最大 にされても壊れないようにする（ImGui で自由にいじれるため）
	const float lifeTimeMax = (lifeTimeMax_ < lifeTimeMin_) ? lifeTimeMin_ : lifeTimeMax_;
	std::uniform_real_distribution<float> distLifeTime(lifeTimeMin_, lifeTimeMax);

	// Emitter の位置＝自分が付いている GameObject の位置
	const Vector3& emitterPosition = GetOwner()->GetTransform().translate;

	// 既定値（等倍・白・寿命1秒）から作り始める
	Particle particle{};

	particle.transform.scale = { particleScale_, particleScale_, particleScale_ };
	particle.transform.rotate = { 0.0f, 0.0f, 0.0f };

	// 軸ごとに別の乱数を引くので、先にオフセットを組み立ててから足す
	const Vector3 offset = {
		distUnit(randomEngine_) * emitRange_.x,
		distUnit(randomEngine_) * emitRange_.y,
		distUnit(randomEngine_) * emitRange_.z,
	};

	particle.transform.translate = emitterPosition + offset;

	particle.velocity = {
		distUnit(randomEngine_) * velocityRange_.x,
		distUnit(randomEngine_) * velocityRange_.y,
		distUnit(randomEngine_) * velocityRange_.z,
	};

	particle.lifeTime = distLifeTime(randomEngine_);
	particle.currentTime = 0.0f;

	return particle;
}

// ════════════════════════════════════════════════════════════════════
// インスペクタ
// ════════════════════════════════════════════════════════════════════
void ParticleEmitter::DrawInspector() {
#ifdef USE_IMGUI

	// ───── 発生 ─────
	ImGui::Checkbox("発生中", &isPlaying_);
	ImGui::SameLine();
	ImGui::Checkbox("範囲を表示", &showGizmos_);
	ImGui::DragFloat("頻度 (個/秒)", &emitRate_, 0.5f, 0.0f, 500.0f, "%.1f");

	ImGui::DragInt("まとめて出す数", &burstCount_, 1.0f, 1, 256);
	ImGui::SameLine();
	if (ImGui::Button("出す")) {
		Burst(static_cast<uint32_t>(burstCount_));
	}
	ImGui::SameLine();
	if (ImGui::Button("全消し")) {
		Clear();
	}

	ImGui::Separator();

	// ───── 生まれる粒の設定 ─────
	ImGui::TextDisabled("生まれる粒");
	ImGui::DragFloat3("発生範囲", &emitRange_.x, 0.05f, 0.0f, 50.0f);
	ImGui::DragFloat3("速度の幅", &velocityRange_.x, 0.05f, 0.0f, 50.0f);
	ImGui::DragFloat("寿命 最小", &lifeTimeMin_, 0.05f, 0.01f, 60.0f, "%.2f");
	ImGui::DragFloat("寿命 最大", &lifeTimeMax_, 0.05f, 0.01f, 60.0f, "%.2f");
	ImGui::DragFloat("大きさ", &particleScale_, 0.01f, 0.01f, 10.0f, "%.2f");

	ImGui::Separator();

	// ───── 見た目 ─────
	ImGui::TextDisabled("見た目");

	// 合成方法。BlendMode の並び順と一致させること
	static const char* kBlendNames[] = {
		"なし", "通常", "加算", "減算", "乗算", "スクリーン",
	};
	static_assert(IM_ARRAYSIZE(kBlendNames) ==
		static_cast<int>(BlendMode::kMaxCount), "kBlendNames の数が合っていません");

	int blendIndex = static_cast<int>(particleSystem_.GetBlendMode());
	if (ImGui::Combo("合成", &blendIndex, kBlendNames, IM_ARRAYSIZE(kBlendNames))) {
		particleSystem_.SetBlendMode(static_cast<BlendMode>(blendIndex));
	}

	bool isBillboard = particleSystem_.IsBillboard();
	if (ImGui::Checkbox("ビルボード", &isBillboard)) {
		particleSystem_.SetBillboard(isBillboard);
	}

	ImGui::ColorEdit4("粒の色", &particleSystem_.GetMaterialData()->color.x);

	ImGui::Separator();
	// ───── 場（Field） ─────
	ImGui::TextDisabled("場（Field）");

	for (uint32_t index = 0; index < particleSystem_.GetFieldCount(); ++index) {

		Field* field = particleSystem_.GetFieldAt(index);

		// 同じラベルが衝突しないよう番号で区別する
		ImGui::PushID(static_cast<int>(index));

		bool enabled = field->IsEnabled();
		if (ImGui::Checkbox("##fieldEnabled", &enabled)) {
			field->SetEnabled(enabled);
		}
		ImGui::SameLine();

		if (ImGui::CollapsingHeader(field->GetTypeName())) {

			// 範囲は全部の場に共通なので、ここで描く
			AABB& area = field->GetArea();
			ImGui::DragFloat3("範囲 min", &area.min.x, 0.1f);
			ImGui::DragFloat3("範囲 max", &area.max.x, 0.1f);

			// 場ごとの固有設定は、場自身に描かせる
			field->DrawInspector();
		}

		ImGui::PopID();
	}

	ImGui::Separator();


	// ───── 統計 ─────
	ImGui::Text("粒（CPU）: %zu", particleSystem_.GetCount());
	ImGui::Text("粒（描画）: %u / %u",
		particleSystem_.GetDrawCount(), particleSystem_.GetMaxInstanceCount());

#endif
}